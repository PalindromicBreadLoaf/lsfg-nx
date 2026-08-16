// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "report.hpp"

#include "observations.hpp"

#include <lsfg/common/protocol.hpp>

#include <switch.h>

#include <saltysd_ipc.h>

#define NANOPRINTF_IMPLEMENTATION
#define NANOPRINTF_VISIBILITY_STATIC
#include <nanoprintf.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace lsfg::plugin::report {
namespace {

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_verbose{false};
std::atomic<std::uint32_t> g_period{0};
std::atomic<std::uint32_t> g_presents{0};
std::atomic_flag g_report_lock{};

constexpr std::size_t format_size = 512;
constexpr std::size_t salty_shared_size = 0x1000;
constexpr std::size_t salty_reserved_prefix = 4;
constexpr std::size_t report_reservation_size
    = salty_reserved_prefix + sizeof(protocol::ReportBlock)
    + alignof(protocol::ReportBlock) - 1;

SharedMemory g_shared_memory{};
protocol::ReportBlock* g_shared_reports{};

void lock_reports() noexcept {
    while (g_report_lock.test_and_set(std::memory_order_acquire)) {
        asm volatile("yield" ::: "memory");
    }
}

void unlock_reports() noexcept {
    g_report_lock.clear(std::memory_order_release);
}

class Session {
public:
    Session() noexcept {
        if (!g_enabled.load(std::memory_order_relaxed)) {
            return;
        }
        lock_reports();
        locked_ = true;
        writable_ = g_shared_reports != nullptr;
    }

    ~Session() {
        if (locked_) {
            unlock_reports();
        }
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return writable_;
    }

    template <typename... Args>
    void print(const char* const format, Args... args) noexcept {
        std::array<char, format_size> text{};
        const int required = npf_snprintf(text.data(), text.size(), format, args...);
        if (required <= 0) {
            return;
        }
        const std::size_t length
            = std::min(static_cast<std::size_t>(required), text.size() - 1);
        (void)protocol::push_report(*g_shared_reports, {text.data(), length});
    }

private:
    bool locked_{};
    bool writable_{};
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

bool prepare_shared_transport() noexcept {
    ptrdiff_t offset = -1;
    if (R_FAILED(SaltySD_CheckIfSharedMemoryAvailable(
            &offset, report_reservation_size))
        || offset < 0) {
        return false;
    }

    Handle handle = INVALID_HANDLE;
    if (R_FAILED(SaltySD_GetSharedMemoryHandle(&handle)) || handle == INVALID_HANDLE) {
        return false;
    }

    shmemLoadRemote(&g_shared_memory, handle, salty_shared_size, Perm_Rw);
    if (R_FAILED(shmemMap(&g_shared_memory))) {
        shmemClose(&g_shared_memory);
        g_shared_memory = {};
        return false;
    }

    const std::uintptr_t base
        = reinterpret_cast<std::uintptr_t>(shmemGetAddr(&g_shared_memory));
    const std::uintptr_t reservation = base + static_cast<std::uintptr_t>(offset);
    const std::uintptr_t aligned
        = (reservation + salty_reserved_prefix + alignof(protocol::ReportBlock) - 1)
        & ~(static_cast<std::uintptr_t>(alignof(protocol::ReportBlock)) - 1);
    if (aligned + sizeof(protocol::ReportBlock) > reservation + report_reservation_size
        || aligned + sizeof(protocol::ReportBlock) > base + salty_shared_size) {
        shmemClose(&g_shared_memory);
        g_shared_memory = {};
        return false;
    }

    g_shared_reports = reinterpret_cast<protocol::ReportBlock*>(aligned);
    protocol::initialize(*g_shared_reports);
    return true;
}

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
