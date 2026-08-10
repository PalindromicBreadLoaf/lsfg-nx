// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/backend/cache_load.hpp>
#include <lsfg/backend/device.hpp>
#include <lsfg/backend/executor.hpp>
#include <lsfg/backend/schedule.hpp>
#include <lsfg/common/cache_format.hpp>
#include <lsfg/common/cache_store.hpp>
#include <lsfg/common/image_graph.hpp>
#include <lsfg/common/ring_log.hpp>

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

constexpr std::uint32_t no_image = 0xFFFF'FFFFU;

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

void name_config(const lsfg::graph::Config& config, const std::span<char> out) {
    std::snprintf(
        out.data(),
        out.size(),
        "%s %u:%u",
        config.performance ? "performance" : "quality",
        config.flow_numerator,
        config.flow_denominator);
}

bool load_cache(
    const std::string& directory,
    lsfg::cache::Loaded& cache,
    lsfg::backend::Plan& plan) {
    lsfg::cache::Loaded loaded;
    if (const lsfg::ErrorCode code = lsfg::cache::read(directory, loaded);
        !lsfg::succeeded(code)) {
        std::printf("skipped %s: %.*s\n",
            directory.c_str(),
            static_cast<int>(lsfg::error_name(code).size()),
            lsfg::error_name(code).data());
        return false;
    }

    const lsfg::backend::Request request{
        .config = loaded.graph.config,
        .precision = lsfg::cache::Precision::high,
        .output = handheld_extent,
    };

    lsfg::backend::Rejection why;
    if (!lsfg::backend::accept(loaded, request, plan, why)) {
        std::printf(
            "refused %s: %.*s\n",
            directory.c_str(),
            static_cast<int>(why.reason.size()),
            why.reason.data());
        return false;
    }

    cache = std::move(loaded);
    return true;
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

struct Bar {
    bool found{};
    float centre{};
};

Bar find_bar(const std::span<const std::uint8_t> pixels, const lsfg::graph::Extent extent) {
    constexpr std::uint32_t background = 210;

    double weight_total = 0;
    double column_total = 0;
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            const std::size_t offset = (static_cast<std::size_t>(y) * extent.width + x) * 4U;
            if (pixels[offset] <= background) {
                continue;
            }
            const double weight = pixels[offset] - background;
            weight_total += weight;
            column_total += weight * x;
        }
    }

    if (weight_total == 0) {
        return Bar{};
    }
    return Bar{.found = true, .centre = static_cast<float>(column_total / weight_total)};
}

void print_bar(const Bar& bar) {
    if (!bar.found) {
        std::printf("   none");
        return;
    }
    const auto tenths = static_cast<std::uint32_t>((bar.centre * 10.0F) + 0.5F);
    std::printf("%5u.%u", tenths / 10U, tenths % 10U);
}

// One configuration measured end to end.
struct Row {
    lsfg::graph::Config config{};
    lsfg::graph::Extent flow{};
    std::uint32_t images{};
    std::uint64_t memory_kib{};
    // The fastest of the sampled frames.
    std::uint64_t frame_ns{};
    // What went wrong (ideally nothing).
    const char* failed{};
    bool measured{};
};

struct FrameCost {
    std::uint64_t elapsed_ns{};
    // Building the command list on the CPU, which the elapsed figure contains.
    std::uint64_t recording_ns{};
    std::uint32_t barriers{};
};

// One real frame's worth of work.
bool run_frame(
    lsfg::backend::Executor& executor,
    const lsfg::backend::Schedule& order,
    const std::uint32_t frame,
    FrameCost& cost) {
    const std::uint64_t started = armGetSystemTick();

    executor.begin();
    if (const lsfg::ErrorCode code = executor.record_chain(order, frame); !lsfg::succeeded(code)) {
        report_failure(code, "recording the chain");
        return false;
    }

    const std::uint64_t recorded = armGetSystemTick();

    if (const lsfg::ErrorCode code = executor.run(); !lsfg::succeeded(code)) {
        report_failure(code, "running the chain");
        return false;
    }

    cost.elapsed_ns = armTicksToNs(armGetSystemTick() - started);
    cost.recording_ns = armTicksToNs(recorded - started);
    cost.barriers = executor.recorded_barriers();
    return true;
}

bool run_frame_by_stage(
    lsfg::backend::Executor& executor,
    const lsfg::backend::Schedule& order,
    const std::uint32_t frame,
    const std::span<std::uint64_t> elapsed_ns) {
    for (std::uint32_t stage = 0; stage < order.stages.size(); ++stage) {
        const std::uint64_t started = armGetSystemTick();

        executor.begin();
        if (const lsfg::ErrorCode code = executor.record_stage(order, stage, frame);
            !lsfg::succeeded(code)) {
            report_failure(code, "recording a stage");
            return false;
        }
        if (const lsfg::ErrorCode code = executor.run(); !lsfg::succeeded(code)) {
            report_failure(code, "running a stage");
            return false;
        }

        elapsed_ns[stage] = armTicksToNs(armGetSystemTick() - started);
    }
    return true;
}

bool read_image(
    lsfg::backend::Executor& executor,
    lsfg::backend::Staging& staging,
    const std::uint32_t image) {

    executor.begin();

    if (const lsfg::ErrorCode code = executor.record_download(image, staging, 0);
        !lsfg::succeeded(code)) {
        report_failure(code, "recording a readback");
        return false;
    }
    if (const lsfg::ErrorCode code = executor.run(); !lsfg::succeeded(code)) {
        report_failure(code, "reading the generated frame back");
        return false;
    }
    return true;
}

std::uint32_t find_generated_image(const lsfg::backend::Plan& plan) {
    for (std::uint32_t index = 0; index < plan.images.size(); ++index) {
        if (plan.images[index].role == lsfg::graph::ImageRole::generated) {
            return index;
        }
    }
    return no_image;
}

bool dispatch_whole_chain(
    lsfg::backend::Executor& executor,
    lsfg::backend::Staging& staging,
    const lsfg::cache::Loaded& cache,
    const lsfg::backend::Plan& plan,
    const bool verbose,
    Row& row) {
    lsfg::backend::Schedule order;
    if (const lsfg::ErrorCode code = lsfg::backend::schedule(cache.graph, order);
        !lsfg::succeeded(code)) {
        report_failure(code, "putting the chain in order");
        return false;
    }

    const std::uint32_t generated = find_generated_image(plan);
    if (generated == no_image) {
        report_failure(lsfg::ErrorCode::invalid_state, "finding the generated frame");
        return false;
    }

    if (verbose) {
        std::printf("\nchain      %u dispatches: %u in the prepass",
            order.dispatches(), order.stages[0].count);
        for (std::uint32_t stage = 1; stage < order.stages.size(); ++stage) {
            std::printf(", %u for generated frame %u", order.stages[stage].count, stage - 1U);
        }
        std::printf(
            "\nreal frames repeat every %u, after %u of warm-up\n",
            order.cycle,
            order.warmup_frames);
        consoleUpdate(nullptr);
    }

    if (!upload_frames(executor, staging, plan)) {
        return false;
    }

    const std::uint64_t frame_bytes = image_bytes(plan.images[0]);
    std::array<Bar, lsfg::graph::history_image_count> real{};
    for (std::uint32_t frame = 0; frame < real.size(); ++frame) {
        real[frame] = find_bar(
            staging.bytes().subspan(frame * frame_bytes, frame_bytes),
            plan.images[frame].extent);
    }
    if (!real[0].found || !real[1].found) {
        report_failure(lsfg::ErrorCode::invalid_state, "finding the bar in the real frames");
        return false;
    }

    const std::array<std::uint32_t, 3> frames{order.cycle, order.cycle + 1U, 2U * order.cycle};
    std::array<Summary, 3> summaries{};
    std::array<Bar, 3> bars{};
    std::array<FrameCost, 3> cost{};

    const lsfg::graph::Extent extent = plan.images[generated].extent;
    const std::uint64_t generated_bytes = image_bytes(plan.images[generated]);

    std::uint64_t warming_ns = 0;
    std::size_t next = 0;

    for (std::uint32_t frame = 0; frame <= frames.back(); ++frame) {
        FrameCost this_frame;
        if (!run_frame(executor, order, frame, this_frame)) {
            return false;
        }
        if (frame < order.cycle) {
            warming_ns += this_frame.elapsed_ns;
        }
        if (next >= frames.size() || frame != frames[next]) {
            continue;
        }

        cost[next] = this_frame;
        if (!read_image(executor, staging, generated)) {
            return false;
        }

        const std::span<const std::uint8_t> pixels = staging.bytes().subspan(0, generated_bytes);
        summaries[next] = summarise(pixels);
        bars[next] = find_bar(pixels, extent);
        ++next;
    }

    std::vector<std::uint64_t> per_stage(order.stages.size(), 0);
    if (verbose && !run_frame_by_stage(executor, order, frames[0], per_stage)) {
        return false;
    }

    if (verbose) {
        std::printf("\nframe  mean  min  max  nonzero  crc         bar\n");
        for (std::size_t run = 0; run < frames.size(); ++run) {
            std::printf(
                "%5u  %4u  %3u  %3u  %5llu%%  %08lx  ",
                frames[run],
                summaries[run].mean,
                summaries[run].minimum,
                summaries[run].maximum,
                static_cast<unsigned long long>(
                    (summaries[run].written * 100ULL)
                    / std::max<std::uint64_t>(generated_bytes, 1)),
                static_cast<unsigned long>(summaries[run].crc));
            print_bar(bars[run]);
            std::printf("\n");
        }

        std::printf("%-41s", "real");
        print_bar(real[0]);
        std::printf(" and");
        print_bar(real[1]);
        std::printf("\n");
    }

    const float lower = std::min(real[0].centre, real[1].centre);
    const float upper = std::max(real[0].centre, real[1].centre);

    bool interpolated = true;
    for (std::size_t run = 0; run < frames.size(); ++run) {
        interpolated = interpolated && bars[run].found && bars[run].centre > lower
            && bars[run].centre < upper;
    }

    const bool wrote_anything = summaries[0].written != 0;
    const bool variants_rotate = summaries[0].crc != summaries[1].crc;
    const bool repeatable = summaries[0].crc == summaries[2].crc;

    // The first thing that went wrong is the one worth carrying forward.
    row.failed = nullptr;
    if (!wrote_anything) {
        row.failed = "LEFT ZERO";
    } else if (!interpolated) {
        row.failed = "BAR OUTSIDE";
    } else if (!variants_rotate) {
        row.failed = "NO ROTATION";
    } else if (!repeatable) {
        row.failed = "NOT REPEATABLE";
    }

    row.frame_ns = std::min({cost[0].elapsed_ns, cost[1].elapsed_ns, cost[2].elapsed_ns});
    row.measured = true;

    const auto us = [](const std::uint64_t ns) {
        return static_cast<unsigned long long>(ns / 1000ULL);
    };

    if (!verbose) {
        return row.failed == nullptr;
    }

    std::printf(
        "\n%s\n",
        wrote_anything ? "the generated frame was written" : "THE GENERATED FRAME WAS LEFT ZERO");
    std::printf(
        "%s\n",
        interpolated ? "the bar lands between the two real frames"
                     : "THE BAR IS NOT BETWEEN THE TWO REAL FRAMES");
    std::printf(
        "%s\n",
        variants_rotate ? "consecutive frames interpolate opposite ways"
                        : "CONSECUTIVE FRAMES GAVE THE SAME RESULT");
    std::printf(
        "%s\n",
        repeatable ? "a cycle apart gives the same result" : "A CYCLE APART GAVE TWO RESULTS");

    std::printf(
        "\nwhole chain %llu us, %llu us, %llu us, against 16667 us at 60 Hz\n",
        us(cost[0].elapsed_ns),
        us(cost[1].elapsed_ns),
        us(cost[2].elapsed_ns));
    std::printf(
        "  recording %llu us of that, %u dispatches and %u barriers\n",
        us(cost[0].recording_ns),
        order.dispatches(),
        cost[0].barriers);
    std::printf("warm-up     %llu us over %u frames\n", us(warming_ns), order.cycle);
    std::printf("  prepass   %llu us over %u dispatches\n", us(per_stage[0]), order.stages[0].count);
    for (std::uint32_t stage = 1; stage < order.stages.size(); ++stage) {
        std::printf(
            "  frame %u   %llu us over %u dispatches\n",
            stage - 1U,
            us(per_stage[stage]),
            order.stages[stage].count);
    }

    return row.failed == nullptr;
}

// Everything one cache costs.
bool measure_cache(const std::string& directory, const bool verbose, Row& row) {
    lsfg::cache::Loaded cache;
    lsfg::backend::Plan plan;

    if (!load_cache(directory, cache, plan)) {
        return false;
    }

    row.config = cache.graph.config;
    row.flow = lsfg::graph::flow_extent(row.config, plan.output);
    row.images = static_cast<std::uint32_t>(plan.images.size());
    row.memory_kib = plan.owned_image_bytes / 1024U;

    if (verbose) {
        std::printf("cache      %s\n", directory.c_str());
        std::printf(
            "%zu modules, %zu dispatches, %u descriptor sets at %ux%u\n\n",
            cache.passes.size(),
            plan.dispatches.size(),
            plan.descriptor_sets,
            plan.output.width,
            plan.output.height);
        consoleUpdate(nullptr);
    }

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

    if (verbose) {
        report_allocation(plan, resources.allocation());
        std::printf(
            "\ndevice %llu ms, allocation %llu ms\n",
            static_cast<unsigned long long>(armTicksToNs(device_ready - started) / 1'000'000ULL),
            static_cast<unsigned long long>(armTicksToNs(allocated - device_ready) / 1'000'000ULL));
        consoleUpdate(nullptr);
    }

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

    if (!dispatch_whole_chain(executor, staging, cache, plan, verbose, row)) {
        if (!device.last_error().empty()) {
            std::printf("%s\n", device.last_error().data());
        }
        return false;
    }

    message_log.push(
        armGetSystemTick(), lsfg::LogLevel::info, lsfg::ErrorCode::ok, "whole chain dispatched");
    return true;
}

// Runs every cache on the card.
bool run() {
    constexpr std::uint64_t budget_us = 16667;

    const std::vector<std::string> directories = cache_directories();
    if (directories.empty()) {
        report_failure(lsfg::ErrorCode::cache_missing, "finding a cache");
        std::printf("Run lsfg-prepare.nro against your own Lossless.dll first.\n");
        return false;
    }

    std::printf("%zu caches to measure at %ux%u\n",
        directories.size(), handheld_extent.width, handheld_extent.height);
    consoleUpdate(nullptr);

    std::vector<Row> rows;
    rows.reserve(directories.size());

    for (std::size_t index = 0; index < directories.size(); ++index) {
        Row row;
        // Only the first one reports in full, so the detail that found the last
        // round of bugs is still there to read.
        static_cast<void>(measure_cache(directories[index], index == 0, row));
        if (row.measured) {
            rows.push_back(row);
        }
    }

    if (rows.empty()) {
        report_failure(lsfg::ErrorCode::cache_missing, "measuring any cache");
        return false;
    }

    std::sort(rows.begin(), rows.end(), [](const Row& left, const Row& right) {
        return left.frame_ns < right.frame_ns;
    });

    std::printf("\nconfig           flow ext   images     KiB   chain us  of 60Hz\n");

    const Row* best = nullptr;
    for (const Row& row : rows) {
        std::array<char, 24> name{};
        name_config(row.config, name);

        std::array<char, 16> extent{};
        std::snprintf(extent.data(), extent.size(), "%ux%u", row.flow.width, row.flow.height);

        const auto frame_us = static_cast<unsigned long long>(row.frame_ns / 1000ULL);

        std::printf(
            "%-15s %9s %8u %7llu %10llu  %4llu.%02llux",
            name.data(),
            extent.data(),
            row.images,
            static_cast<unsigned long long>(row.memory_kib),
            frame_us,
            frame_us / budget_us,
            ((frame_us * 100ULL) / budget_us) % 100ULL);

        if (row.failed != nullptr) {
            std::printf("  %s", row.failed);
        } else if (frame_us <= budget_us) {
            std::printf("  UNDER");
            // Sorted fastest first, so the last one to fit is the most
            // expensive one that does, which is the one worth running.
            best = &row;
        }
        std::printf("\n");
        consoleUpdate(nullptr);
    }

    if (best != nullptr) {
        std::array<char, 24> name{};
        name_config(best->config, name);
        std::printf(
            "\nbest configuration inside the budget: %s at %llu us\n",
            name.data(),
            static_cast<unsigned long long>(best->frame_ns / 1000ULL));
        return true;
    }

    std::array<char, 24> name{};
    name_config(rows.front().config, name);
    std::printf(
        "\nnothing reached %llu us; %s came closest at %llu us\n",
        static_cast<unsigned long long>(budget_us),
        name.data(),
        static_cast<unsigned long long>(rows.front().frame_ns / 1000ULL));
    return false;
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
