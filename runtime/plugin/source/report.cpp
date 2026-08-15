// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "report.hpp"

#include "observations.hpp"

#include <switch.h>

#include <ipc.h>

#define NANOPRINTF_IMPLEMENTATION
#define NANOPRINTF_VISIBILITY_STATIC
#include <nanoprintf.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstring>

namespace lsfg::plugin::report {
namespace {

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_verbose{false};
std::atomic<std::uint32_t> g_period{0};
std::atomic<std::uint32_t> g_presents{0};
std::atomic_flag g_report_lock{};

constexpr std::uint64_t command_end_session = 0;
constexpr std::uint64_t command_sdcard_fopen = 20;
constexpr std::uint64_t command_sdcard_fclose = 22;
constexpr std::uint64_t command_sdcard_fwrite = 26;
constexpr const char* log_path = "sdmc:/SaltySD/saltynx_core.log";

class Client {
public:
    [[nodiscard]] bool connect() noexcept {
        for (std::uint32_t attempt = 0; attempt < 200; ++attempt) {
            if (R_SUCCEEDED(svcConnectToNamedPort(&m_handle, "SaltySD"))) {
                return true;
            }
            svcSleepThread(1'000'000);
        }
        return false;
    }

    void close() noexcept {
        if (m_handle == INVALID_HANDLE) {
            return;
        }

        IpcCommand command;
        ipcInitialize(&command);
        ipcSendPid(&command);

        struct Request {
            std::uint64_t magic;
            std::uint64_t command_id;
            std::uint64_t zero;
            std::uint64_t reserved[2];
        };

        auto* const request = static_cast<Request*>(ipcPrepareHeader(&command, sizeof(Request)));
        *request = Request{SFCI_MAGIC, command_end_session, 0, {0, 0}};
        ipcDispatch(m_handle);
        svcCloseHandle(m_handle);
        m_handle = INVALID_HANDLE;
    }

    [[nodiscard]] bool print(const char* const format, ...) noexcept {
        std::array<char, 512> text{};

        va_list arguments;
        va_start(arguments, format);
        const int required = npf_vsnprintf(text.data(), text.size(), format, arguments);
        va_end(arguments);

        if (required <= 0) {
            return false;
        }

        const std::size_t length = std::min(
            static_cast<std::size_t>(required), text.size() - 1);
        const std::uint32_t file = open_log();
        if (file == 0) {
            return false;
        }

        const bool written = write(file, text.data(), length);
        close_file(file);
        return written;
    }

private:
    [[nodiscard]] std::uint32_t open_log() noexcept {
        IpcCommand command;
        ipcInitialize(&command);
        ipcSendPid(&command);
        ipcAddSendBuffer(
            &command, log_path, std::strlen(log_path) + 1, BufferType_Normal);

        struct Request {
            std::uint64_t magic;
            std::uint64_t command_id;
            char mode[4];
        };

        auto* const request = static_cast<Request*>(ipcPrepareHeader(&command, sizeof(Request)));
        *request = Request{SFCI_MAGIC, command_sdcard_fopen, {'a', 'b', '\0', '\0'}};
        if (R_FAILED(ipcDispatch(m_handle))) {
            return 0;
        }

        IpcParsedCommand response;
        ipcParse(&response);
        struct Reply {
            std::uint64_t magic;
            std::uint64_t result;
            std::uint32_t id;
        };
        const auto* const reply = static_cast<const Reply*>(response.Raw);
        return reply->magic == SFCO_MAGIC && R_SUCCEEDED(reply->result) ? reply->id : 0;
    }

    [[nodiscard]] bool write(
        const std::uint32_t file, const void* const data, const std::size_t size) noexcept {
        IpcCommand command;
        ipcInitialize(&command);
        ipcSendPid(&command);
        ipcAddSendBuffer(&command, data, size, BufferType_Normal);

        struct Request {
            std::uint64_t magic;
            std::uint64_t command_id;
            std::uint64_t size;
            std::uint64_t count;
            std::uint32_t id;
        };

        auto* const request = static_cast<Request*>(ipcPrepareHeader(&command, sizeof(Request)));
        *request = Request{SFCI_MAGIC, command_sdcard_fwrite, size, 1, file};
        if (R_FAILED(ipcDispatch(m_handle))) {
            return false;
        }

        IpcParsedCommand response;
        ipcParse(&response);
        struct Reply {
            std::uint64_t magic;
            std::uint64_t result;
            std::uint64_t count;
        };
        const auto* const reply = static_cast<const Reply*>(response.Raw);
        return reply->magic == SFCO_MAGIC && R_SUCCEEDED(reply->result) && reply->count == 1;
    }

    void close_file(const std::uint32_t file) noexcept {
        IpcCommand command;
        ipcInitialize(&command);
        ipcSendPid(&command);

        struct Request {
            std::uint64_t magic;
            std::uint64_t command_id;
            std::uint32_t id;
        };

        auto* const request = static_cast<Request*>(ipcPrepareHeader(&command, sizeof(Request)));
        *request = Request{SFCI_MAGIC, command_sdcard_fclose, file};
        ipcDispatch(m_handle);
    }

    Handle m_handle{INVALID_HANDLE};
};

class Session {
public:
    Session() noexcept {
        if (!g_enabled.load(std::memory_order_relaxed)) {
            return;
        }
        while (g_report_lock.test_and_set(std::memory_order_acquire)) {
            asm volatile("yield" ::: "memory");
        }
        locked_ = true;
        connected_ = client_.connect();
    }

    ~Session() {
        if (connected_) {
            client_.close();
        }
        if (locked_) {
            g_report_lock.clear(std::memory_order_release);
        }
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return connected_;
    }

    template <typename... Args>
    void print(const char* const format, Args... args) noexcept {
        (void)client_.print(format, args...);
    }

private:
    Client client_{};
    bool locked_{};
    bool connected_{};
};

void print_stat(
    Session& session, const char* const name, const instrument::Stat& stat) noexcept {
    session.print(
        "lsfg-nx:   %-18s n=%lu min=%lu us mean=%lu us max=%lu us\n",
        name,
        stat.count,
        stat.min,
        stat.mean(),
        stat.max);
}

void write_discovery_trace(Session& session) noexcept {
    const Observations& observations = peek();
    const auto events = observations.trace.view();

    session.print(
        "lsfg-nx: discovery trace, %u events, %lu dropped\n",
        static_cast<unsigned>(events.size()),
        observations.trace.dropped());

    const std::uint64_t first = events.empty() ? 0 : events.front().tick;
    for (const instrument::Event& event : events) {
        const auto name = instrument::event_name(event.kind);
        session.print(
            "lsfg-nx:   +%lu us %-30s object=%016lx detail=%ld\n",
            instrument::ticks_to_us(event.tick - first),
            name.data(),
            event.object,
            static_cast<long>(event.detail));
    }
}

void write_pacing(Session& session) noexcept {
    const Observations& observations = peek();
    const instrument::PresentTimeline& timeline = observations.timeline;

    session.print(
        "lsfg-nx: presents=%lu acquires=%lu submits=%lu fences=%lu waits=%lu\n",
        timeline.presents(),
        timeline.acquires(),
        timeline.submits(),
        timeline.fences(),
        timeline.sync_waits());
    session.print(
        "lsfg-nx: pacing outliers=%lu sequence faults=%lu foreign presents=%lu "
        "swapchains=%lu\n",
        timeline.outliers(),
        timeline.sequence_faults(),
        observations.foreign_window_presents,
        observations.swapchain_replacements);

    print_stat(session, "present interval", timeline.present_interval());
    print_stat(session, "acquire block", timeline.acquire_block());
    print_stat(session, "acquire to present", timeline.acquire_to_present());
    print_stat(session, "submit to present", timeline.submit_to_present());
    print_stat(session, "sync wait", timeline.sync_wait_block());

    const auto histogram = timeline.histogram();
    for (std::size_t bucket = 0; bucket < histogram.size(); ++bucket) {
        if (histogram[bucket] == 0) {
            continue;
        }
        session.print(
            "lsfg-nx:   interval %lu to %lu ms: %u\n",
            (bucket * instrument::PresentTimeline::histogram_bucket_us) / 1000U,
            ((bucket + 1U) * instrument::PresentTimeline::histogram_bucket_us) / 1000U,
            histogram[bucket]);
    }
}

} // namespace

void configure(const bool enabled, const bool verbose,
    const std::uint32_t presents_between_reports) noexcept {
    g_enabled.store(enabled, std::memory_order_relaxed);
    g_verbose.store(verbose, std::memory_order_relaxed);
    g_period.store(enabled ? presents_between_reports : 0, std::memory_order_relaxed);
}

void on_install(const ErrorCode result) noexcept {
    Session session;
    if (!session) {
        return;
    }
    session.print(
        "lsfg-nx: graphics hooks %s (%s)\n",
        succeeded(result) ? "installed" : "not installed",
        error_name(result).data());
}

void on_queries_resolved(const std::uint32_t missing) noexcept {
    Session session;
    if (!session) {
        return;
    }
    session.print("lsfg-nx: graphics queries resolved, %u absent\n", missing);
}

void on_swapchain(const instrument::SwapchainMap& map) noexcept {
    Session session;
    if (!session) {
        return;
    }
    session.print(
        "lsfg-nx: swapchain %016lx textures=%u resolved=%u interval=%u active=%u uniform=%u\n",
        map.window,
        map.texture_count,
        map.resolved_count,
        static_cast<unsigned>(map.present_interval),
        static_cast<unsigned>(map.active_textures),
        map.uniform ? 1U : 0U);

    for (std::uint32_t index = 0; index < map.texture_count; ++index) {
        const instrument::TextureRecord& texture = map.textures[index];
        if (!texture.valid()) {
            session.print(
                "lsfg-nx:   [%u] %016lx unresolved\n", index, map.handles[index]);
            continue;
        }
        session.print(
            "lsfg-nx:   [%u] %016lx %ux%ux%u levels=%u samples=%u format=%u flags=0x%x "
            "target=%u stride=%u\n",
            index,
            texture.handle,
            texture.width,
            texture.height,
            texture.depth,
            texture.levels,
            texture.samples,
            texture.format,
            texture.flags,
            texture.target,
            texture.stride);
        session.print(
            "lsfg-nx:       pool=%016lx gpu=%016lx cpu=%016lx texture=%016lx\n",
            texture.memory_pool,
            texture.pool_address,
            texture.pool_cpu_address,
            texture.texture_address);
        session.print(
            "lsfg-nx:       pool_size=%lu pool_flags=0x%x offset=%lu size=%lu "
            "alignment=%lu class=%u\n",
            texture.pool_size,
            texture.pool_flags,
            texture.storage_offset,
            texture.storage_size,
            texture.storage_alignment,
            texture.storage_class);
    }

    if (g_verbose.load(std::memory_order_relaxed)) {
        write_discovery_trace(session);
    }
}

void on_present() noexcept {
    const std::uint32_t period = g_period.load(std::memory_order_relaxed);
    if (period == 0) {
        return;
    }

    const std::uint32_t count = g_presents.fetch_add(1, std::memory_order_relaxed) + 1U;
    if (count % period == 0) {
        pacing();
    }
}

void discovery_trace() noexcept {
    Session session;
    if (session) {
        write_discovery_trace(session);
    }
}

void pacing() noexcept {
    Session session;
    if (session) {
        write_pacing(session);
    }
}

} // namespace lsfg::plugin::report
