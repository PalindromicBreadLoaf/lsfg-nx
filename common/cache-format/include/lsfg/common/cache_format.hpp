// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <lsfg/common/error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace lsfg::cache {

inline constexpr std::uint32_t manifest_magic = 0x4746'534CU; // "LSFG"

// Bump on any change to the manifest layout or to what the runtime expects a
// cached module to contain.
inline constexpr std::uint32_t abi_version = 1;

inline constexpr std::size_t digest_size = 32;
inline constexpr std::size_t revision_size = 20;
inline constexpr std::uint32_t max_passes = 64;

using Digest = std::array<std::uint8_t, digest_size>;
using Revision = std::array<std::uint8_t, revision_size>;

struct alignas(8) ManifestHeader {
    std::uint32_t magic{};
    std::uint32_t abi_version{};

    Digest dll_hash{};
    std::uint64_t dll_size{};

    std::uint32_t shader_set{};
    std::uint32_t extractor_version{};

    Revision spirv_cross_revision{};
    Revision uam_revision{};

    std::uint32_t translation_options{};
    std::uint32_t backend_abi_version{};
    std::uint32_t pass_count{};
    // Covers every pass entry that follows.
    std::uint32_t payload_crc32{};
};

struct alignas(8) PassEntry {
    std::uint32_t resource_id{};
    std::uint32_t dksh_size{};
    Digest dksh_hash{};

    std::uint16_t workgroup_x{};
    std::uint16_t workgroup_y{};
    std::uint16_t workgroup_z{};
    std::uint16_t reserved0_{};

    std::uint32_t sampled_texture_mask{};
    std::uint32_t storage_image_mask{};
    std::uint32_t uniform_buffer_mask{};
    std::uint32_t storage_buffer_mask{};

    // Image extent as a fraction of the output extent, so a manifest stays
    // valid across resolutions instead of pinning one.
    std::uint16_t width_numerator{};
    std::uint16_t width_denominator{};
    std::uint16_t height_numerator{};
    std::uint16_t height_denominator{};

    std::uint32_t image_format{};
    std::uint32_t scratch_memory_bytes{};
};

static_assert(sizeof(ManifestHeader) == 112);
static_assert(sizeof(PassEntry) == 80);
static_assert(alignof(ManifestHeader) == 8);
static_assert(alignof(PassEntry) == 8);

[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t seed = 0) noexcept;

void initialize(ManifestHeader& header) noexcept;

[[nodiscard]] ErrorCode validate(const ManifestHeader& header) noexcept;
[[nodiscard]] ErrorCode validate(const PassEntry& entry) noexcept;

// Checks the header, every entry, and the payload CRC together.
[[nodiscard]] ErrorCode validate(const ManifestHeader& header, std::span<const PassEntry> passes) noexcept;

} // namespace lsfg::cache
