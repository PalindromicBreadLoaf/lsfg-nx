// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/backend/binding.hpp>
#include <lsfg/backend/cache_load.hpp>
#include <lsfg/backend/device.hpp>
#include <lsfg/backend/executor.hpp>
#include <lsfg/common/cache_format.hpp>
#include <lsfg/common/cache_store.hpp>
#include <lsfg/common/image_graph.hpp>
#include <lsfg/common/ring_log.hpp>
#include <lsfg/common/shader_set.hpp>

#include <switch.h>

#include <dirent.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* cache_root = "sdmc:/switch/lsfg-nx/cache";

// Handheld first to limit the pixel workload before anything is measured.
constexpr lsfg::graph::Extent handheld_extent{.width = 1280, .height = 720};

// The pass that opens the chain.
constexpr std::string_view first_pass = "mipmaps";

constexpr std::uint32_t no_dispatch = 0xFFFF'FFFFU;

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

// A checkerboard with a gradient over it and a bar that moves between the two
// frames.
void draw_frame(
    const std::span<std::uint8_t> pixels,
    const lsfg::graph::Extent extent,
    const std::uint32_t frame) {
    constexpr std::uint32_t bar_width = 96;
    const std::uint32_t bar = 160U + (frame * 48U);

    for (std::uint32_t y = 0; y < extent.height; ++y) {
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            const bool light = (((x >> 5U) + (y >> 5U)) & 1U) != 0;
            const bool in_bar = x >= bar && x < bar + bar_width;

            const std::size_t offset = (static_cast<std::size_t>(y) * extent.width + x) * 4U;
            pixels[offset + 0] = in_bar ? 255 : static_cast<std::uint8_t>(light ? 200 : 40);
            pixels[offset + 1] = in_bar
                ? 255
                : static_cast<std::uint8_t>((x * 255U) / (extent.width - 1U));
            pixels[offset + 2] = in_bar
                ? 255
                : static_cast<std::uint8_t>((y * 255U) / (extent.height - 1U));
            pixels[offset + 3] = 255;
        }
    }
}

struct Summary {
    std::uint32_t crc{};
    std::uint32_t written{};
    std::uint32_t minimum{};
    std::uint32_t maximum{};
    std::uint32_t mean{};
};

Summary summarise(const std::span<const std::uint8_t> bytes) {
    Summary out{
        .crc = lsfg::cache::crc32(bytes),
        .minimum = 255,
    };

    std::uint64_t total = 0;
    for (const std::uint8_t value : bytes) {
        out.minimum = std::min<std::uint32_t>(out.minimum, value);
        out.maximum = std::max<std::uint32_t>(out.maximum, value);
        out.written += value != 0 ? 1U : 0U;
        total += value;
    }
    if (!bytes.empty()) {
        out.mean = static_cast<std::uint32_t>(total / bytes.size());
    }
    return out;
}

std::uint32_t find_dispatch(const lsfg::graph::Graph& graph, const std::string_view name) {
    const std::span<const lsfg::shaders::ChainSlot> slots = lsfg::shaders::chain_slots();

    for (std::uint32_t index = 0; index < graph.dispatches.size(); ++index) {
        const std::uint32_t slot = graph.dispatches[index].slot;
        if (slot < slots.size() && slots[slot].name == name) {
            return index;
        }
    }
    return no_dispatch;
}

std::uint64_t image_bytes(const lsfg::backend::ImagePlan& image) {
    return static_cast<std::uint64_t>(image.extent.width) * image.extent.height
        * lsfg::graph::bytes_per_pixel(image.format);
}

// The two real frames written once and left alone for the rest of the run.
bool upload_frames(
    lsfg::backend::Executor& executor,
    lsfg::backend::Staging& staging,
    const lsfg::backend::Plan& plan) {
    const std::uint64_t frame_bytes = image_bytes(plan.images[0]);

    for (std::uint32_t frame = 0; frame < lsfg::graph::history_image_count; ++frame) {
        if (plan.images[frame].role != lsfg::graph::ImageRole::history
            || image_bytes(plan.images[frame]) != frame_bytes) {
            report_failure(lsfg::ErrorCode::invalid_state, "finding the real frames");
            return false;
        }
    }

    for (std::uint32_t frame = 0; frame < lsfg::graph::history_image_count; ++frame) {
        draw_frame(
            staging.bytes().subspan(frame * frame_bytes, frame_bytes),
            plan.images[frame].extent,
            frame);
    }

    executor.begin();
    for (std::uint32_t frame = 0; frame < lsfg::graph::history_image_count; ++frame) {
        if (const lsfg::ErrorCode code
            = executor.record_upload(frame, staging, frame * frame_bytes);
            !lsfg::succeeded(code)) {
            report_failure(code, "recording a frame copy");
            return false;
        }
    }

    if (const lsfg::ErrorCode code = executor.run(); !lsfg::succeeded(code)) {
        report_failure(code, "copying the frames in");
        return false;
    }
    return true;
}

// Runs the dispatch at one real frame index and reads back everything it wrote.
bool run_pass(
    lsfg::backend::Executor& executor,
    lsfg::backend::Staging& staging,
    const lsfg::backend::Plan& plan,
    const lsfg::backend::DispatchBinding& binding,
    const std::uint32_t dispatch,
    const std::uint32_t phase,
    std::vector<Summary>& out,
    std::uint64_t& elapsed_ns) {
    const std::uint64_t started = armGetSystemTick();

    executor.begin();
    if (const lsfg::ErrorCode code = executor.record(dispatch, phase); !lsfg::succeeded(code)) {
        report_failure(code, "recording the dispatch");
        return false;
    }
    if (const lsfg::ErrorCode code = executor.run(); !lsfg::succeeded(code)) {
        report_failure(code, "running the dispatch");
        return false;
    }

    elapsed_ns = armTicksToNs(armGetSystemTick() - started);

    executor.begin();
    executor.barrier();

    std::uint64_t offset = 0;
    for (std::uint32_t index = 0; index < binding.storage_count; ++index) {
        const std::uint32_t image = binding.storages[index].image;
        if (const lsfg::ErrorCode code = executor.record_download(image, staging, offset);
            !lsfg::succeeded(code)) {
            report_failure(code, "recording a readback");
            return false;
        }
        offset += image_bytes(plan.images[image]);
    }

    if (const lsfg::ErrorCode code = executor.run(); !lsfg::succeeded(code)) {
        report_failure(code, "reading the result back");
        return false;
    }

    out.clear();
    offset = 0;
    for (std::uint32_t index = 0; index < binding.storage_count; ++index) {
        const std::uint64_t bytes = image_bytes(plan.images[binding.storages[index].image]);
        out.push_back(summarise(staging.bytes().subspan(offset, bytes)));
        offset += bytes;
    }
    return true;
}

bool dispatch_first_pass(
    lsfg::backend::Executor& executor,
    lsfg::backend::Staging& staging,
    const lsfg::cache::Loaded& cache,
    const lsfg::backend::Plan& plan,
    const lsfg::backend::DescriptorLayout& descriptors) {
    const std::uint32_t dispatch = find_dispatch(cache.graph, first_pass);
    if (dispatch == no_dispatch) {
        report_failure(lsfg::ErrorCode::shader_set_unknown, "finding the first pass");
        return false;
    }

    lsfg::backend::DispatchBinding binding;
    if (const lsfg::ErrorCode code
        = lsfg::backend::bind(cache, plan, descriptors, dispatch, 0, binding);
        !lsfg::succeeded(code)) {
        report_failure(code, "binding the first pass");
        return false;
    }

    std::printf(
        "\n%.*s: dispatch %u, module %u, %u textures, %u storage images, %ux%u groups\n",
        static_cast<int>(first_pass.size()),
        first_pass.data(),
        dispatch,
        binding.pass,
        binding.texture_count,
        binding.storage_count,
        binding.groups_x,
        binding.groups_y);
    consoleUpdate(nullptr);

    if (!upload_frames(executor, staging, plan)) {
        return false;
    }

    std::array<std::vector<Summary>, 3> results;
    std::array<std::uint64_t, 3> elapsed{};
    constexpr std::array<std::uint32_t, 3> phases{0, 1, 0};

    for (std::size_t run = 0; run < results.size(); ++run) {
        if (!run_pass(
                executor, staging, plan, binding, dispatch, phases[run], results[run], elapsed[run])) {
            return false;
        }
    }

    std::printf("\nlevel  extent      mean  min  max  written  frame 0     frame 1\n");
    for (std::size_t level = 0; level < results[0].size(); ++level) {
        const lsfg::graph::Extent extent
            = plan.images[binding.storages[level].image].extent;
        const Summary& first = results[0][level];
        const Summary& second = results[1][level];
        const std::uint64_t pixels = static_cast<std::uint64_t>(extent.width) * extent.height;

        std::printf(
            "%zu      %4ux%-4u  %4u  %3u  %3u  %5llu%%  %08lx  %08lx\n",
            level,
            extent.width,
            extent.height,
            first.mean,
            first.minimum,
            first.maximum,
            static_cast<unsigned long long>((first.written * 100ULL) / std::max<std::uint64_t>(pixels, 1)),
            static_cast<unsigned long>(first.crc),
            static_cast<unsigned long>(second.crc));
    }

    bool wrote_everything = true;
    bool motion_is_visible = false;
    bool repeatable = results[0].size() == results[2].size();
    for (std::size_t level = 0; level < results[0].size(); ++level) {
        wrote_everything = wrote_everything && results[0][level].written != 0;
        motion_is_visible = motion_is_visible || results[0][level].crc != results[1][level].crc;
        repeatable = repeatable && results[0][level].crc == results[2][level].crc;
    }

    std::printf("\n%s\n", wrote_everything ? "every storage image was written" : "AN IMAGE WAS LEFT ZERO");
    std::printf("%s\n", motion_is_visible ? "the two frames give different results" : "BOTH FRAMES GAVE THE SAME RESULT");
    std::printf("%s\n", repeatable ? "the same frame gives the same result twice" : "THE SAME FRAME GAVE TWO RESULTS");
    std::printf(
        "dispatch and wait %llu us, %llu us, %llu us\n",
        static_cast<unsigned long long>(elapsed[0] / 1000ULL),
        static_cast<unsigned long long>(elapsed[1] / 1000ULL),
        static_cast<unsigned long long>(elapsed[2] / 1000ULL));

    return wrote_everything && motion_is_visible && repeatable;
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
        .copyable_images = true,
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
    consoleUpdate(nullptr);

    message_log.push(
        armGetSystemTick(), lsfg::LogLevel::info, lsfg::ErrorCode::ok, "chain allocated");

    lsfg::backend::Executor executor;
    if (const lsfg::ErrorCode code
        = executor.create(device, cache, plan, resources, lsfg::backend::ExecutorOptions{});
        !lsfg::succeeded(code)) {
        report_failure(code, "creating the command buffer");
        return false;
    }

    // Enough for both real frames at once.
    lsfg::backend::Staging staging;
    if (const lsfg::ErrorCode code = staging.create(
            device, lsfg::graph::history_image_count * image_bytes(plan.images[0]));
        !lsfg::succeeded(code)) {
        report_failure(code, "allocating the staging buffer");
        return false;
    }

    if (!dispatch_first_pass(executor, staging, cache, plan, resources.descriptors())) {
        if (!device.last_error().empty()) {
            std::printf("%s\n", device.last_error().data());
        }
        return false;
    }

    message_log.push(
        armGetSystemTick(), lsfg::LogLevel::info, lsfg::ErrorCode::ok, "first pass dispatched");
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
