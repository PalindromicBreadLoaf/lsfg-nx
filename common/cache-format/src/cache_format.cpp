// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/common/cache_format.hpp>

#include <bit>

namespace lsfg::cache {
namespace {

constexpr std::uint32_t crc32_polynomial = 0xEDB8'8320U;

} // namespace

std::uint32_t crc32(const std::span<const std::uint8_t> data, const std::uint32_t seed) noexcept {
    std::uint32_t remainder = ~seed;
    for (const std::uint8_t byte : data) {
        remainder ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = static_cast<std::uint32_t>(-static_cast<std::int32_t>(remainder & 1U));
            remainder = (remainder >> 1U) ^ (crc32_polynomial & mask);
        }
    }
    return ~remainder;
}

Digest cache_key(const CacheKeyInputs& inputs) noexcept {
    Sha256 hasher;
    hasher.update_field(inputs.dll_bytes);
    hasher.update_field(inputs.extractor_version);
    hasher.update_field(inputs.spirv_cross_revision);
    hasher.update_field(inputs.uam_revision);
    hasher.update_field(inputs.translation_options);
    hasher.update_field(inputs.backend_abi_version);
    return hasher.finish();
}

void initialize(ManifestHeader& header) noexcept {
    header = ManifestHeader{};
    header.magic = manifest_magic;
    header.abi_version = abi_version;
}

ErrorCode validate(const ManifestHeader& header) noexcept {
    if (header.magic != manifest_magic) {
        return ErrorCode::cache_integrity_failure;
    }
    if (header.abi_version != abi_version) {
        return ErrorCode::cache_version_mismatch;
    }
    if (header.dll_size == 0) {
        return ErrorCode::cache_integrity_failure;
    }
    if (header.pass_count == 0 || header.pass_count > max_passes) {
        return ErrorCode::cache_integrity_failure;
    }
    return ErrorCode::ok;
}

ErrorCode validate(const PassEntry& entry) noexcept {
    if (entry.dksh_size == 0) {
        return ErrorCode::cache_integrity_failure;
    }
    if (entry.workgroup_x == 0 || entry.workgroup_y == 0 || entry.workgroup_z == 0) {
        return ErrorCode::shader_interface_mismatch;
    }
    if (entry.width_denominator == 0 || entry.height_denominator == 0) {
        return ErrorCode::shader_interface_mismatch;
    }
    if (entry.width_numerator == 0 || entry.height_numerator == 0) {
        return ErrorCode::shader_interface_mismatch;
    }
    return ErrorCode::ok;
}

ErrorCode validate(const ManifestHeader& header, const std::span<const PassEntry> passes) noexcept {
    if (const ErrorCode code = validate(header); !succeeded(code)) {
        return code;
    }
    if (passes.size() != header.pass_count) {
        return ErrorCode::cache_integrity_failure;
    }
    for (const PassEntry& entry : passes) {
        if (const ErrorCode code = validate(entry); !succeeded(code)) {
            return code;
        }
    }

    const std::span<const std::uint8_t> payload{
        std::bit_cast<const std::uint8_t*>(passes.data()),
        passes.size() * sizeof(PassEntry)};
    if (crc32(payload) != header.payload_crc32) {
        return ErrorCode::cache_integrity_failure;
    }
    return ErrorCode::ok;
}

} // namespace lsfg::cache
