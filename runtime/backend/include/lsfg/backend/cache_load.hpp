// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <lsfg/common/cache_format.hpp>
#include <lsfg/common/cache_store.hpp>
#include <lsfg/common/error.hpp>
#include <lsfg/common/image_graph.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

// Whether a prepared cache is one this build can run, and what it would have
// to allocate to run it.
namespace lsfg::backend {

// What the executor can bind to one dispatch.
struct Limits {
    std::uint32_t textures{32};
    std::uint32_t storage_images{8};
    std::uint32_t uniform_buffers{16};
    std::uint32_t workgroup_invocations{1024};
    std::uint32_t shared_memory_bytes{48U * 1024U};
};

// Images the chain owns, above which the runtime refuses rather than taking
// memory the game is going to want. The chain is roughly 20 MiB at 720p and
// 46 MiB at 1080p, so this leaves docked headroom and still catches a graph
// that would take an unreasonable share of the process.
inline constexpr std::uint64_t default_memory_budget_bytes = 64ULL * 1024U * 1024U;

struct Request {
    graph::Config config;
    cache::Precision precision{cache::Precision::high};
    // The extent frames are presented at.
    graph::Extent output{};
    std::uint64_t memory_budget_bytes{default_memory_budget_bytes};
    Limits limits{};
};

struct ImagePlan {
    graph::Extent extent;
    std::uint64_t bytes{};
    graph::Format format{};
    graph::ImageRole role{};
};

struct DispatchPlan {
    // Index into the cache's passes, so the executor never searches by block.
    std::uint32_t pass{};
    std::uint32_t groups_x{};
    std::uint32_t groups_y{};
};

// Everything the executor allocates and dispatches, at one output extent.
struct Plan {
    graph::Extent output;
    graph::Extent flow;

    std::vector<ImagePlan> images;
    std::vector<DispatchPlan> dispatches;

    // History and generated images come from presentation rather than from
    // here, so only the rest is counted against the budget.
    std::uint64_t owned_image_bytes{};
    std::uint32_t owned_images{};
    std::uint32_t imported_images{};

    std::uint32_t descriptor_sets{};
    std::uint32_t uniform_buffers{};

    std::uint32_t prepass_dispatches{};
    std::uint32_t generated_frame_dispatches{};

    std::uint32_t max_registers{};
    std::uint32_t max_scratch_bytes_per_warp{};
    std::uint32_t max_shared_memory_bytes{};
};

inline constexpr std::uint32_t no_pass = 0xFFFF'FFFFU;

// Why a cache was refused.
struct Rejection {
    ErrorCode code{ErrorCode::ok};
    std::string_view reason;
    std::uint32_t pass{no_pass};
    std::uint64_t observed{};
    std::uint64_t allowed{};
};

// Checks a loaded cache against what this build can run and turns it into what
// the executor has to allocate.
[[nodiscard]] bool accept(
    const cache::Loaded& cache,
    const Request& request,
    Plan& out,
    Rejection& why);

// Reads the cache the key names and accepts it.
[[nodiscard]] bool load(
    std::string_view root,
    const Digest& key,
    const Request& request,
    cache::Loaded& cache,
    Plan& out,
    Rejection& why);

} // namespace lsfg::backend
