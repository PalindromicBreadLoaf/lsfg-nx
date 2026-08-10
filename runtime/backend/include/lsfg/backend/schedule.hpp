// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <lsfg/common/error.hpp>
#include <lsfg/common/image_graph.hpp>

#include <cstdint>
#include <vector>

// The order the chain's dispatches run in, and how many real frames have to go
// through it before what it produces is built entirely out of real input.
namespace lsfg::backend {

struct StageRange {
    std::uint32_t first{};
    std::uint32_t count{};
};

inline constexpr std::uint32_t max_frame_cycle = 64;

struct Schedule {
    // Index 0 is the prepass every generated frame shares, and index 1 + n
    // belongs to generated frame n.
    std::vector<StageRange> stages;

    // Real frame indices repeat after this many frames, because every dispatch
    // picks its variant by the index modulo its own variant count.
    std::uint32_t cycle{1};

    // Real frames that have to run before every frame after them reads only
    // what has been written.
    std::uint32_t warmup_frames{};

    [[nodiscard]] std::uint32_t dispatches() const noexcept;

    [[nodiscard]] std::uint32_t generated_frames() const noexcept;
};

// Refuses a graph whose stages are not each one contiguous run in dispatch
// order, since that is what lets a stage be recorded as a range, and one whose
// dispatches read an image nothing ever writes.
[[nodiscard]] ErrorCode schedule(const graph::Graph& graph, Schedule& out);

} // namespace lsfg::backend
