// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <lsfg/common/error.hpp>
#include <lsfg/common/image_graph.hpp>

#include <cstdint>
#include <vector>

// How the chain's images, descriptors, and buffers are laid out in memory.
namespace lsfg::backend {

inline constexpr std::uint32_t no_descriptor = 0xFFFF'FFFFU;

struct ImageDescriptors {
    std::uint32_t sampled{no_descriptor};
    std::uint32_t storage{no_descriptor};
};

// Translation gives every module a sampler for the image it only asks the size
// of.
inline constexpr std::uint32_t introduced_sampler
    = static_cast<std::uint32_t>(graph::Sampler::edge) + 1U;
inline constexpr std::uint32_t sampler_descriptor_count = introduced_sampler + 1U;

struct DescriptorLayout {
    // One entry per image in the graph, in the graph's order.
    std::vector<ImageDescriptors> images;

    std::uint32_t image_descriptors{};
    std::uint32_t sampled_images{};
    std::uint32_t storage_images{};
};

// Gives every image the descriptors the chain actually reaches it through.
[[nodiscard]] ErrorCode describe(const graph::Graph& graph, DescriptorLayout& out);

// A memory block's size is a multiple of this, and an offset into one is 32
// bits wide.
inline constexpr std::uint64_t memory_block_alignment = 0x1000;
inline constexpr std::uint64_t max_memory_block_bytes = 0xFFFF'F000ULL;

// The stride uniform buffers are bound at.
inline constexpr std::uint32_t uniform_buffer_stride = 0x100;

static_assert(uniform_buffer_stride >= sizeof(graph::ConstantBuffer));

// Places allocations one after another inside a single memory block.
class Arena {
public:
    [[nodiscard]] std::uint32_t place(std::uint64_t size, std::uint32_t alignment) noexcept;

    [[nodiscard]] std::uint64_t used() const noexcept {
        return used_;
    }

    [[nodiscard]] std::uint32_t block_size() const noexcept;

    [[nodiscard]] bool overflowed() const noexcept {
        return overflowed_;
    }

private:
    std::uint64_t used_{};
    bool overflowed_{};
};

} // namespace lsfg::backend
