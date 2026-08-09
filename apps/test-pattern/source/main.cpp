// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/backend/cache_load.hpp>
#include <lsfg/backend/device.hpp>
#include <lsfg/common/cache_store.hpp>
#include <lsfg/common/image_graph.hpp>
#include <lsfg/common/ring_log.hpp>

#include <switch.h>

#include <dirent.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* cache_root = "sdmc:/switch/lsfg-nx/cache";

// Handheld first to limit the pixel workload before anything is measured.
constexpr lsfg::graph::Extent handheld_extent{.width = 1280, .height = 720};

lsfg::RingLog message_log;

void report_failure(const lsfg::ErrorCode error, const char* const stage) {
    const std::string_view name = lsfg::error_name(error);
    std::printf("\n%s failed: %.*s\n", stage, static_cast<int>(name.size()), name.data());
    message_log.push(armGetSystemTick(), lsfg::LogLevel::error, error, stage);
    consoleUpdate(nullptr);
}

std::vector<std::string> cache_directories() {
    std::vector<std::string> directories;

    DIR* const root = opendir(cache_root);
    if (root == nullptr) {
        return directories;
    }

    while (const dirent* const entry = readdir(root)) {
        if (entry->d_type != DT_DIR || entry->d_name[0] == '.') {
            continue;
        }
        directories.emplace_back(std::string(cache_root) + "/" + entry->d_name);
    }

    closedir(root);
    return directories;
}

bool find_cache(lsfg::cache::Loaded& cache, lsfg::backend::Plan& plan, std::string& directory) {
    const lsfg::backend::Request request{
        .config = lsfg::graph::Config{},
        .precision = lsfg::cache::Precision::high,
        .output = handheld_extent,
    };

    for (const std::string& candidate : cache_directories()) {
        lsfg::cache::Loaded loaded;
        if (const lsfg::ErrorCode code = lsfg::cache::read(candidate, loaded);
            !lsfg::succeeded(code)) {
            std::printf("skipped %s: %.*s\n",
                candidate.c_str(),
                static_cast<int>(lsfg::error_name(code).size()),
                lsfg::error_name(code).data());
            continue;
        }

        lsfg::backend::Rejection why;
        if (!lsfg::backend::accept(loaded, request, plan, why)) {
            std::printf(
                "refused %s: %.*s\n",
                candidate.c_str(),
                static_cast<int>(why.reason.size()),
                why.reason.data());
            continue;
        }

        cache = std::move(loaded);
        directory = candidate;
        return true;
    }

    return false;
}

void report_allocation(const lsfg::backend::Plan& plan, const lsfg::backend::Allocation& taken) {
    const auto kib = [](const std::uint64_t bytes) {
        return static_cast<unsigned long long>(bytes / 1024U);
    };

    std::printf(
        "images     %u of %zu, %llu KiB\n",
        taken.images,
        plan.images.size(),
        kib(taken.owned_image_bytes + taken.imported_image_bytes));
    std::printf(
        "  chain    %llu KiB laid out, %llu KiB of pixels\n",
        kib(taken.owned_image_bytes),
        kib(plan.owned_image_bytes));
    std::printf("  frames   %llu KiB of stand-ins\n", kib(taken.imported_image_bytes));
    std::printf("descriptors %u, %llu KiB\n", taken.image_descriptors, kib(taken.descriptor_bytes));
    std::printf("uniforms   %llu KiB\n", kib(taken.uniform_bytes));
    std::printf("modules    %u, %llu KiB of code\n", taken.modules, kib(taken.code_bytes));
    std::printf("total      %llu KiB\n", kib(taken.total()));
}

bool run() {
    lsfg::cache::Loaded cache;
    lsfg::backend::Plan plan;
    std::string directory;

    if (!find_cache(cache, plan, directory)) {
        report_failure(lsfg::ErrorCode::cache_missing, "finding a cache");
        std::printf("Run lsfg-prepare.nro against your own Lossless.dll first.\n");
        return false;
    }

    std::printf("cache      %s\n", directory.c_str());
    std::printf(
        "%zu modules, %zu dispatches, %u descriptor sets at %ux%u\n\n",
        cache.passes.size(),
        plan.dispatches.size(),
        plan.descriptor_sets,
        plan.output.width,
        plan.output.height);
    consoleUpdate(nullptr);

    const std::uint64_t started = armGetSystemTick();

    lsfg::backend::Device device;
    const lsfg::backend::DeviceOptions device_options{
        .per_warp_scratch_bytes = plan.max_scratch_bytes_per_warp,
    };
    if (const lsfg::ErrorCode code = device.create(device_options); !lsfg::succeeded(code)) {
        report_failure(code, "creating the deko device");
        if (!device.last_error().empty()) {
            std::printf("%s\n", device.last_error().data());
        }
        return false;
    }

    const std::uint64_t device_ready = armGetSystemTick();

    lsfg::backend::Resources resources;
    const lsfg::backend::ResourceOptions resource_options{
        // Nothing presents here, so the chain runs entirely on images this
        // process owns.
        .own_imported_images = true,
    };
    if (const lsfg::ErrorCode code = resources.create(device, cache, plan, resource_options);
        !lsfg::succeeded(code)) {
        report_failure(code, "allocating the chain");
        if (!device.last_error().empty()) {
            std::printf("%s\n", device.last_error().data());
        }
        return false;
    }

    const std::uint64_t allocated = armGetSystemTick();

    report_allocation(plan, resources.allocation());
    std::printf(
        "\ndevice %llu ms, allocation %llu ms\n",
        static_cast<unsigned long long>(armTicksToNs(device_ready - started) / 1'000'000ULL),
        static_cast<unsigned long long>(armTicksToNs(allocated - device_ready) / 1'000'000ULL));

    message_log.push(
        armGetSystemTick(), lsfg::LogLevel::info, lsfg::ErrorCode::ok, "chain allocated");
    return true;
}

} // namespace

int main() {
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    PadState pad{};
    padInitializeDefault(&pad);

    std::printf("LSFG-NX test pattern\n\n");
    consoleUpdate(nullptr);

    static_cast<void>(run());

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
