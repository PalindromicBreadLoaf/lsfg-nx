// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/common/image_file.hpp>

#include <lsfg/common/cache_format.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace lsfg::image {

namespace {

void write_u32(const std::span<std::uint8_t> out, const std::size_t offset, const std::uint32_t value) noexcept {
    out[offset + 0] = static_cast<std::uint8_t>(value);
    out[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
    out[offset + 2] = static_cast<std::uint8_t>(value >> 16U);
    out[offset + 3] = static_cast<std::uint8_t>(value >> 24U);
}

} // namespace

std::uint64_t pixel_bytes(const graph::Extent extent, const graph::Format format) noexcept {
    return static_cast<std::uint64_t>(extent.width) * extent.height
        * graph::bytes_per_pixel(format);
}

DumpHeader describe(
    const Description& description,
    const std::span<const std::uint8_t> pixels) noexcept {
    DumpHeader header;
    header.width = description.extent.width;
    header.height = description.extent.height;
    header.bytes = static_cast<std::uint32_t>(pixels.size());
    header.crc = cache::crc32(pixels);
    header.image = description.image;
    header.frame = description.frame;
    header.format = static_cast<std::uint8_t>(description.format);
    header.role = static_cast<std::uint8_t>(description.role);

    const std::size_t copied = std::min(description.label.size(), label_capacity - 1U);
    std::memcpy(header.label.data(), description.label.data(), copied);
    return header;
}

ErrorCode encode(
    const DumpHeader& header,
    const std::span<const std::uint8_t> pixels,
    std::vector<std::uint8_t>& out) {
    if (header.bytes != pixels.size()) {
        return ErrorCode::invalid_argument;
    }

    out.resize(sizeof(DumpHeader) + pixels.size());
    std::memcpy(out.data(), &header, sizeof(DumpHeader));
    if (!pixels.empty()) {
        std::memcpy(out.data() + sizeof(DumpHeader), pixels.data(), pixels.size());
    }
    return ErrorCode::ok;
}

ErrorCode decode(const std::span<const std::uint8_t> bytes, Dump& out) {
    if (bytes.size() < sizeof(DumpHeader)) {
        return ErrorCode::cache_integrity_failure;
    }

    DumpHeader header;
    std::memcpy(&header, bytes.data(), sizeof(DumpHeader));

    if (header.magic != dump_magic) {
        return ErrorCode::cache_integrity_failure;
    }
    if (header.version != dump_version) {
        return ErrorCode::cache_version_mismatch;
    }
    if (header.format > static_cast<std::uint8_t>(graph::Format::rgba16f)
        || header.role > static_cast<std::uint8_t>(graph::ImageRole::constant)) {
        return ErrorCode::cache_integrity_failure;
    }

    const graph::Extent extent{.width = header.width, .height = header.height};
    const auto format = static_cast<graph::Format>(header.format);
    if (pixel_bytes(extent, format) != header.bytes) {
        return ErrorCode::cache_integrity_failure;
    }
    if (bytes.size() - sizeof(DumpHeader) != header.bytes) {
        return ErrorCode::cache_integrity_failure;
    }

    const std::span<const std::uint8_t> payload = bytes.subspan(sizeof(DumpHeader));
    if (cache::crc32(payload) != header.crc) {
        return ErrorCode::cache_integrity_failure;
    }

    out.header = header;
    out.pixels.assign(payload.begin(), payload.end());
    return ErrorCode::ok;
}

std::string_view label_of(const DumpHeader& header) noexcept {
    const std::string_view whole{header.label.data(), header.label.size()};
    const std::size_t end = whole.find('\0');
    return end == std::string_view::npos ? whole : whole.substr(0, end);
}

ErrorCode encode_dds(
    const DumpHeader& header,
    const std::span<const std::uint8_t> pixels,
    std::vector<std::uint8_t>& out) {
    if (header.format != static_cast<std::uint8_t>(graph::Format::rgba8)) {
        return ErrorCode::unsupported;
    }
    if (header.bytes != pixels.size()) {
        return ErrorCode::invalid_argument;
    }

    out.assign(dds_header_bytes + pixels.size(), 0);
    const std::span<std::uint8_t> whole{out};

    write_u32(whole, 0, 0x2053'4444U);  // "DDS "
    write_u32(whole, 4, 124);
    // Caps, height, width, pitch, and pixel format are all present.
    write_u32(whole, 8, 0x0000'100FU);
    write_u32(whole, 12, header.height);
    write_u32(whole, 16, header.width);
    write_u32(whole, 20, header.width * 4U);

    write_u32(whole, 76, 32);
    // Uncompressed RGBA.
    write_u32(whole, 80, 0x0000'0041U);
    write_u32(whole, 88, 32);
    write_u32(whole, 92, 0x0000'00FFU);
    write_u32(whole, 96, 0x0000'FF00U);
    write_u32(whole, 100, 0x00FF'0000U);
    write_u32(whole, 104, 0xFF00'0000U);

    write_u32(whole, 108, 0x0000'1000U);

    if (!pixels.empty()) {
        std::memcpy(out.data() + dds_header_bytes, pixels.data(), pixels.size());
    }
    return ErrorCode::ok;
}

ErrorCode decode_dds(
    const std::span<const std::uint8_t> bytes,
    const Description& description,
    Dump& out) {
    if (bytes.size() < dds_header_bytes) {
        return ErrorCode::cache_integrity_failure;
    }

    const auto field = [bytes](const std::size_t offset) {
        return static_cast<std::uint32_t>(bytes[offset])
            | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U)
            | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U)
            | (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
    };

    if (field(0) != 0x2053'4444U || field(4) != 124) {
        return ErrorCode::cache_integrity_failure;
    }

    // A four character code means a compressed format or a DX10 header.
    // A bit count or mask that is not this one means a different channel order.
    if (field(84) != 0 || field(88) != 32 || field(92) != 0x0000'00FFU
        || field(96) != 0x0000'FF00U || field(100) != 0x00FF'0000U
        || field(104) != 0xFF00'0000U) {
        return ErrorCode::unsupported;
    }

    const graph::Extent extent{.width = field(16), .height = field(12)};
    if (extent.width == 0 || extent.height == 0) {
        return ErrorCode::cache_integrity_failure;
    }

    const std::uint64_t wanted = pixel_bytes(extent, graph::Format::rgba8);
    if (bytes.size() - dds_header_bytes < wanted) {
        return ErrorCode::cache_integrity_failure;
    }

    const std::span<const std::uint8_t> payload
        = bytes.subspan(dds_header_bytes, static_cast<std::size_t>(wanted));

    Description filled = description;
    filled.extent = extent;
    filled.format = graph::Format::rgba8;

    out.header = describe(filled, payload);
    out.pixels.assign(payload.begin(), payload.end());
    return ErrorCode::ok;
}

ErrorCode draw_test_frame(
    const std::span<std::uint8_t> pixels,
    const graph::Extent extent,
    const std::uint32_t frame) noexcept {
    if (extent.width < 2 || extent.height < 2) {
        return ErrorCode::invalid_argument;
    }
    if (pixels.size() != pixel_bytes(extent, graph::Format::rgba8)) {
        return ErrorCode::invalid_argument;
    }

    const std::uint32_t bar = test_frame_bar_first + (frame * test_frame_bar_step);

    for (std::uint32_t y = 0; y < extent.height; ++y) {
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            const bool light = (((x >> 5U) + (y >> 5U)) & 1U) != 0;
            const bool in_bar = x >= bar && x < bar + test_frame_bar_width;

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
    return ErrorCode::ok;
}

Bar find_bar(
    const std::span<const std::uint8_t> pixels,
    const graph::Extent extent) noexcept {
    constexpr std::uint32_t background = 210;

    if (pixels.size() != pixel_bytes(extent, graph::Format::rgba8)) {
        return Bar{};
    }

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

} // namespace lsfg::image
