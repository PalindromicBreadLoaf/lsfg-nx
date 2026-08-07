// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/common/cache_store.hpp>
#include <lsfg/common/image_graph.hpp>
#include <lsfg/common/prepare.hpp>
#include <lsfg/common/ring_log.hpp>
#include <lsfg/common/sha256.hpp>
#include <lsfg/common/version.hpp>

#include <switch.h>

#include <malloc.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

namespace {

constexpr const char* dll_path = "sdmc:/switch/lsfg-nx/Lossless.dll";
constexpr const char* cache_root = "sdmc:/switch/lsfg-nx/cache";

// The extent the first target runs at in handheld mode, used only to report
// what the chain would allocate.
constexpr lsfg::graph::Extent handheld_extent{.width = 1280, .height = 720};

lsfg::RingLog message_log;

void report_failure(const lsfg::ErrorCode error, const char* const stage) {
    const std::string_view name = lsfg::error_name(error);
    std::printf("\n%s failed: %.*s\n", stage, static_cast<int>(name.size()), name.data());
    message_log.push(armGetSystemTick(), lsfg::LogLevel::error, error, stage);
    consoleUpdate(nullptr);
}

// What this app is actually holding. The kernel's used-memory figure would not
// move: libnx reserves the whole heap at startup, so only the allocator knows.
std::size_t heap_in_use() {
    return mallinfo().uordblks;
}

std::vector<std::uint8_t> read_dll() {
    std::FILE* const file = std::fopen(dll_path, "rb");
    if (file == nullptr) {
        return {};
    }

    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(file);
        return {};
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    const std::size_t read = std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);

    if (read != bytes.size()) {
        return {};
    }
    return bytes;
}

bool prepare_dll() {
    const std::size_t startup_heap = heap_in_use();

    const std::vector<std::uint8_t> image = read_dll();
    if (image.empty()) {
        report_failure(lsfg::ErrorCode::io_error, "reading the DLL");
        std::printf("Copy your own Lossless.dll to\n%s\n", dll_path);
        return false;
    }

    std::printf("size      %zu bytes\n", image.size());
    consoleUpdate(nullptr);

    const lsfg::prepare::Options options;
    lsfg::prepare::Result result;

    // The NRO holds the DLL, the GLSL, and uam at once, and this is the only
    // machine whose memory ceiling matters. Sampling between modules cannot see
    // inside a compile, so the figure is a floor rather than the true peak.
    std::size_t peak_heap = heap_in_use();

    const std::uint64_t started = armGetSystemTick();
    const lsfg::ErrorCode prepared = lsfg::prepare::run(
        image,
        options,
        result,
        [&peak_heap](const lsfg::prepare::ModuleReport& module) {
            const std::size_t in_use = heap_in_use();
            peak_heap = peak_heap > in_use ? peak_heap : in_use;

            std::printf(
                "%-10s %6zu B GLSL %5zu B DKSH %3u regs\n",
                module.name.c_str(),
                module.glsl_bytes,
                module.dksh_bytes,
                module.registers);
            consoleUpdate(nullptr);
        });

    if (const std::size_t in_use = heap_in_use(); in_use > peak_heap) {
        peak_heap = in_use;
    }

    // The hash and the key are known even when a later stage refuses the DLL,
    // and they are the first thing worth reporting about an unknown one.
    std::printf("sha256    %s\n", lsfg::to_hex(result.dll_hash).data());
    std::printf("cache key %s\n\n", lsfg::to_hex(result.key).data());
    consoleUpdate(nullptr);

    if (!lsfg::succeeded(prepared)) {
        report_failure(prepared, "preparation");
        return false;
    }

    const std::uint64_t elapsed_ms = armTicksToNs(armGetSystemTick() - started) / 1'000'000ULL;

    std::size_t glsl_bytes = 0;
    std::size_t dksh_bytes = 0;
    std::uint32_t worst_gprs = 0;
    std::uint32_t worst_scratch = 0;
    for (const lsfg::prepare::ModuleReport& module : result.reports) {
        glsl_bytes += module.glsl_bytes;
        dksh_bytes += module.dksh_bytes;
        worst_gprs = worst_gprs > module.registers ? worst_gprs : module.registers;
        worst_scratch = worst_scratch > module.scratch_bytes ? worst_scratch : module.scratch_bytes;
    }

    std::printf(
        "\n%zu modules: %zu B of GLSL, %zu B of DKSH in %llu ms\n",
        result.reports.size(),
        glsl_bytes,
        dksh_bytes,
        static_cast<unsigned long long>(elapsed_ms));
    std::printf("most registers %u, most scratch %u B per warp\n", worst_gprs, worst_scratch);
    std::printf(
        "peak heap %zu KiB between modules, %zu KiB at startup\n",
        peak_heap / 1024U,
        startup_heap / 1024U);

    const lsfg::graph::Graph& graph = result.contents.graph;
    std::printf(
        "%zu dispatches over %zu images, %llu KiB at %ux%u\n\n",
        graph.dispatches.size(),
        graph.images.size(),
        static_cast<unsigned long long>(lsfg::graph::owned_memory_bytes(graph, handheld_extent) / 1024U),
        handheld_extent.width,
        handheld_extent.height);
    consoleUpdate(nullptr);

    const std::string directory = lsfg::prepare::directory_for(cache_root, result);

    // A cache already here was compiled from the same DLL by the same compiler,
    // so this run says whether compiling twice on this console lands in the
    // same place.
    lsfg::cache::Loaded existing;
    if (lsfg::succeeded(lsfg::cache::read(directory, existing))) {
        const lsfg::cache::Comparison comparison
            = lsfg::cache::compare(existing, result.contents);

        if (comparison.identical()) {
            std::printf("second run: all %u modules identical\n", comparison.modules);
        } else if (!comparison.same_shape) {
            std::printf("second run: DIFFERENT MODULES, not the same chain\n");
        } else {
            std::printf(
                "second run: %u of %u modules differ, %u bytes in total\n",
                comparison.differing_modules,
                comparison.modules,
                comparison.differing_bytes);
        }

        message_log.push(
            armGetSystemTick(),
            comparison.identical() ? lsfg::LogLevel::info : lsfg::LogLevel::warning,
            lsfg::ErrorCode::ok,
            "second run comparison");
        consoleUpdate(nullptr);
    }

    if (const lsfg::ErrorCode result_code = lsfg::cache::write(directory, result.contents);
        !lsfg::succeeded(result_code)) {
        report_failure(result_code, "writing the cache");
        return false;
    }

    lsfg::cache::Loaded written;
    if (const lsfg::ErrorCode result_code = lsfg::cache::read(directory, written);
        !lsfg::succeeded(result_code)) {
        report_failure(result_code, "reading the cache back");
        return false;
    }
    if (!lsfg::cache::same_modules(written, result.contents)) {
        report_failure(lsfg::ErrorCode::cache_integrity_failure, "verifying the cache");
        return false;
    }

    std::printf("\ncache written and verified:\n%s\n", directory.c_str());
    message_log.push(armGetSystemTick(), lsfg::LogLevel::info, lsfg::ErrorCode::ok, "cache written");
    return true;
}

} // namespace

int main() {
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    PadState pad{};
    padInitializeDefault(&pad);

    std::printf("LSFG-NX preparation app\n\n");
    std::printf("file      %s\n", dll_path);
    consoleUpdate(nullptr);

    static_cast<void>(prepare_dll());

    std::printf("\nPress + to exit.\n");

    while (appletMainLoop()) {
        padUpdate(&pad);
        if ((padGetButtonsDown(&pad) & HidNpadButton_Plus) != 0U) {
            break;
        }
        consoleUpdate(nullptr);
    }

    consoleExit(nullptr);
    return 0;
}
