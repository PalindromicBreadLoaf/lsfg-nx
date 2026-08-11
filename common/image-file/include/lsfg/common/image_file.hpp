// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <lsfg/common/error.hpp>
#include <lsfg/common/image_graph.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// A single image in a file so that an image produced on the console and an image
// produced somewhere else can be put side by side.
namespace lsfg::image {

inline constexpr std::uint32_t dump_magic = 0x4449534CU;  // "LSID"
inline constexpr std::uint32_t dump_version = 1;

inline constexpr std::uint32_t unknown_index = 0xFFFF'FFFFU;
inline constexpr std::size_t label_capacity = 32;

struct alignas(4) DumpHeader {
    std::uint32_t magic{dump_magic};
    std::uint32_t version{dump_version};

    std::uint32_t width{};
    std::uint32_t height{};

    std::uint32_t bytes{};
    std::uint32_t crc{};

    // Where the image sat in the chain.
    std::uint32_t image{unknown_index};
    std::uint32_t frame{unknown_index};

    std::uint8_t format{};
    std::uint8_t role{};
    std::uint16_t reserved0_{};
    std::uint32_t reserved1_{};

    std::array<char, label_capacity> label{};
};

static_assert(sizeof(DumpHeader) == 72);

struct Dump {
    DumpHeader header;
    std::vector<std::uint8_t> pixels;
};

[[nodiscard]] std::uint64_t pixel_bytes(
    graph::Extent extent,
    graph::Format format) noexcept;

struct Description {
    graph::Extent extent{};
    graph::Format format{graph::Format::rgba8};
    graph::ImageRole role{graph::ImageRole::internal};
    std::uint32_t image{unknown_index};
    std::uint32_t frame{unknown_index};
    std::string_view label;
};

// Fills in the sizes and the checksum from the pixels themselves.
[[nodiscard]] DumpHeader describe(
    const Description& description,
    std::span<const std::uint8_t> pixels) noexcept;

[[nodiscard]] ErrorCode encode(
    const DumpHeader& header,
    std::span<const std::uint8_t> pixels,
    std::vector<std::uint8_t>& out);

[[nodiscard]] ErrorCode decode(std::span<const std::uint8_t> bytes, Dump& out);

[[nodiscard]] std::string_view label_of(const DumpHeader& header) noexcept;

inline constexpr std::size_t dds_header_bytes = 128;

[[nodiscard]] ErrorCode encode_dds(
    const DumpHeader& header,
    std::span<const std::uint8_t> pixels,
    std::vector<std::uint8_t>& out);

[[nodiscard]] ErrorCode decode_dds(
    std::span<const std::uint8_t> bytes,
    const Description& description,
    Dump& out);

// The deterministic pair of real frames to run every comparison against.
inline constexpr std::uint32_t test_frame_bar_width = 96;
inline constexpr std::uint32_t test_frame_bar_first = 160;
inline constexpr std::uint32_t test_frame_bar_step = 48;

// Writes one frame as rgba8.
[[nodiscard]] ErrorCode draw_test_frame(
    std::span<std::uint8_t> pixels,
    graph::Extent extent,
    std::uint32_t frame) noexcept;

struct Bar {
    bool found{};
    float centre{};
};

[[nodiscard]] Bar find_bar(
    std::span<const std::uint8_t> pixels,
    graph::Extent extent) noexcept;

} // namespace lsfg::image
