// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/common/image_graph.hpp>
#include <lsfg/common/shader_set.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

lsfg::graph::Graph quality_graph() {
    lsfg::graph::Graph graph;
    require(
        lsfg::succeeded(lsfg::graph::build(lsfg::graph::Config{}, graph)),
        "the default configuration builds");
    return graph;
}

void test_configuration_is_checked() {
    lsfg::graph::Graph graph;
    require(
        !lsfg::succeeded(lsfg::graph::build(lsfg::graph::Config{.generated_frames = 0}, graph)),
        "a graph generating nothing is refused");
    require(
        !lsfg::succeeded(lsfg::graph::build(
            lsfg::graph::Config{.generated_frames = lsfg::graph::max_generated_frames + 1U}, graph)),
        "too many generated frames are refused");
    require(
        !lsfg::succeeded(lsfg::graph::build(
            lsfg::graph::Config{.flow_numerator = 2, .flow_denominator = 1}, graph)),
        "a flow extent above the output extent is refused");
}

void test_dispatch_count() {
    const lsfg::graph::Graph graph = quality_graph();
    require(graph.dispatches.size() == 100, "the quality chain is 100 dispatches");

    std::map<std::string, std::uint32_t> per_stage;
    for (const lsfg::graph::DispatchEntry& dispatch : graph.dispatches) {
        const std::string name{lsfg::shaders::chain_slots()[dispatch.slot].name};
        per_stage[name.substr(0, name.find('.'))] += 1U;
    }

    require(per_stage["mipmaps"] == 1, "one mipmap dispatch");
    require(per_stage["alpha"] == 28, "seven alpha levels of four dispatches");
    require(per_stage["beta"] == 5, "five beta dispatches");
    require(per_stage["gamma"] == 35, "seven gamma levels of five dispatches");
    require(per_stage["delta"] == 30, "three delta levels of ten dispatches");
    require(per_stage["generate"] == 1, "one generate dispatch");

    std::uint32_t prepass = 0;
    for (const lsfg::graph::DispatchEntry& dispatch : graph.dispatches) {
        prepass += dispatch.stage == lsfg::graph::prepass_stage ? 1U : 0U;
    }
    require(prepass == 34, "the prepass is shared by every generated frame");
}

void test_performance_preset_is_smaller() {
    lsfg::graph::Graph performance;
    require(
        lsfg::succeeded(lsfg::graph::build(lsfg::graph::Config{.performance = true}, performance)),
        "the performance configuration builds");

    const lsfg::graph::Graph quality = quality_graph();
    require(
        performance.dispatches.size() == quality.dispatches.size(),
        "both presets run the same dispatches");
    require(performance.images.size() < quality.images.size(), "the performance preset holds fewer images");
}

void test_second_generated_frame_shares_the_prepass() {
    lsfg::graph::Graph one = quality_graph();
    lsfg::graph::Graph two;
    require(
        lsfg::succeeded(lsfg::graph::build(lsfg::graph::Config{.generated_frames = 2}, two)),
        "a two-frame graph builds");

    require(two.dispatches.size() == one.dispatches.size() + 66U, "a second frame adds only its own passes");
    require(two.uniform_buffer_count == 3, "one uniform buffer per generated frame plus the prepass");
}

// Every module the chain dispatches declares its descriptor counts in its own
// SPIR-V.
void test_bindings_match_the_modules() {
    const std::vector<lsfg::graph::ModuleInterface> modules{
        {.block_index = 1, .textures = 1, .storage_images = 7, .samplers = 1, .uniform_buffers = 1},
        {.block_index = 2, .textures = 5, .storage_images = 1, .samplers = 2, .uniform_buffers = 1},
        {.block_index = 3, .textures = 9, .storage_images = 3, .samplers = 2, .uniform_buffers = 1},
        {.block_index = 4, .textures = 10, .storage_images = 2, .samplers = 2, .uniform_buffers = 1},
        {.block_index = 5, .textures = 3, .storage_images = 4, .samplers = 1, .uniform_buffers = 0},
        {.block_index = 6, .textures = 4, .storage_images = 4, .samplers = 1, .uniform_buffers = 0},
        {.block_index = 7, .textures = 4, .storage_images = 4, .samplers = 1, .uniform_buffers = 0},
        {.block_index = 8, .textures = 6, .storage_images = 1, .samplers = 2, .uniform_buffers = 1},
        {.block_index = 9, .textures = 3, .storage_images = 4, .samplers = 1, .uniform_buffers = 0},
        {.block_index = 10, .textures = 4, .storage_images = 4, .samplers = 1, .uniform_buffers = 0},
        {.block_index = 11, .textures = 4, .storage_images = 4, .samplers = 1, .uniform_buffers = 0},
        {.block_index = 12, .textures = 6, .storage_images = 1, .samplers = 2, .uniform_buffers = 1},
        {.block_index = 13, .textures = 1, .storage_images = 2, .samplers = 1, .uniform_buffers = 0},
        {.block_index = 14, .textures = 2, .storage_images = 2, .samplers = 1, .uniform_buffers = 0},
        {.block_index = 15, .textures = 2, .storage_images = 4, .samplers = 1, .uniform_buffers = 0},
        {.block_index = 16, .textures = 4, .storage_images = 4, .samplers = 1, .uniform_buffers = 0},
        {.block_index = 17, .textures = 2, .storage_images = 2, .samplers = 1, .uniform_buffers = 0},
        {.block_index = 18, .textures = 2, .storage_images = 2, .samplers = 1, .uniform_buffers = 0},
        {.block_index = 19, .textures = 2, .storage_images = 2, .samplers = 1, .uniform_buffers = 0},
        {.block_index = 20, .textures = 3, .storage_images = 1, .samplers = 2, .uniform_buffers = 1},
        {.block_index = 21, .textures = 12, .storage_images = 2, .samplers = 1, .uniform_buffers = 0},
        {.block_index = 22, .textures = 2, .storage_images = 2, .samplers = 1, .uniform_buffers = 0},
        {.block_index = 23, .textures = 2, .storage_images = 2, .samplers = 1, .uniform_buffers = 0},
        {.block_index = 24, .textures = 2, .storage_images = 2, .samplers = 1, .uniform_buffers = 0},
        {.block_index = 25, .textures = 2, .storage_images = 6, .samplers = 1, .uniform_buffers = 1},
    };

    const lsfg::graph::Graph graph = quality_graph();
    require(
        lsfg::succeeded(lsfg::graph::check_against_modules(graph, modules)),
        "every dispatch binds what its module declares");

    std::vector<lsfg::graph::ModuleInterface> altered = modules;
    altered.front().storage_images = 6;
    require(
        !lsfg::succeeded(lsfg::graph::check_against_modules(graph, altered)),
        "a module that lost an output is caught");

    std::vector<lsfg::graph::ModuleInterface> missing = modules;
    missing.pop_back();
    require(
        !lsfg::succeeded(lsfg::graph::check_against_modules(graph, missing)),
        "a module the chain needs but the DLL lacks is caught");
}

void test_extents_at_720p() {
    const lsfg::graph::Graph graph = quality_graph();
    const lsfg::graph::Extent output{.width = 1280, .height = 720};
    const lsfg::graph::Extent flow = lsfg::graph::flow_extent(graph.config, output);
    require(flow.width == 1280 && flow.height == 720, "an unscaled flow extent is the output extent");

    const lsfg::graph::ImageDesc first = graph.images.front();
    const lsfg::graph::Extent history = lsfg::graph::evaluate(first, output, flow);
    require(history.width == 1280 && history.height == 720, "a real frame is the output extent");

    // Halving rounds up and shifting rounds down.
    lsfg::graph::ImageDesc quarter{};
    quarter.base = static_cast<std::uint8_t>(lsfg::graph::ExtentBase::flow);
    quarter.ceil_halvings = 2;
    const lsfg::graph::Extent odd
        = lsfg::graph::evaluate(quarter, output, lsfg::graph::Extent{.width = 1279, .height = 719});
    require(odd.width == 320 && odd.height == 180, "halving rounds up");

    lsfg::graph::ImageDesc shifted{};
    shifted.base = static_cast<std::uint8_t>(lsfg::graph::ExtentBase::flow);
    shifted.shift_before = 2;
    const lsfg::graph::Extent floored
        = lsfg::graph::evaluate(shifted, output, lsfg::graph::Extent{.width = 1279, .height = 719});
    require(floored.width == 319 && floored.height == 179, "shifting rounds down");

    for (const lsfg::graph::ImageDesc& desc : graph.images) {
        const lsfg::graph::Extent extent = lsfg::graph::evaluate(desc, output, flow);
        require(extent.width != 0 && extent.height != 0, "no image collapses at 720p");
    }

    const std::uint64_t memory = lsfg::graph::owned_memory_bytes(graph, output);
    require(memory > 0, "the chain allocates something");
    require(memory < 256U * 1024U * 1024U, "the chain fits in a sane memory budget at 720p");
}

void test_generated_frames_never_feed_back() {
    lsfg::graph::Graph graph = quality_graph();
    require(lsfg::succeeded(lsfg::graph::validate(graph)), "a built graph validates");

    // Point the last dispatch's first texture at the frame it generates.
    const lsfg::graph::DispatchEntry& last = graph.dispatches.back();
    const lsfg::graph::VariantEntry& variant = graph.variants[last.variant_first];
    graph.bindings[variant.binding_first] = lsfg::graph::history_image_count;
    require(
        !lsfg::succeeded(lsfg::graph::validate(graph)),
        "reading a generated frame back into the chain is refused");
}

void test_constant_buffer_places_the_frame_midway() {
    const lsfg::graph::ConstantBuffer buffer
        = lsfg::graph::constant_buffer(1, lsfg::graph::Config{});
    require(buffer.timestamp == 0.5F, "one generated frame sits between the two real ones");
    require(buffer.resolution_inverse_scale == 1.0F, "an unscaled flow needs no correction");

    const lsfg::graph::ConstantBuffer second
        = lsfg::graph::constant_buffer(2, lsfg::graph::Config{.generated_frames = 3});
    require(second.timestamp == 0.5F, "the middle of three generated frames sits at the midpoint");
}

} // namespace

int main() {
    test_configuration_is_checked();
    test_dispatch_count();
    test_performance_preset_is_smaller();
    test_second_generated_frame_shares_the_prepass();
    test_bindings_match_the_modules();
    test_extents_at_720p();
    test_generated_frames_never_feed_back();
    test_constant_buffer_places_the_frame_midway();

    std::cout << "graph tests passed\n";
    return EXIT_SUCCESS;
}
