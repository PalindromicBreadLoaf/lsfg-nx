// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <lsfg/common/error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Per-title records
namespace lsfg::profile {

inline constexpr std::size_t max_builds = 8;
inline constexpr std::size_t name_capacity = 64;
inline constexpr std::size_t version_capacity = 16;

inline constexpr std::size_t max_text_size = 8192;

struct BuildEntry {
    std::array<char, version_capacity> version{};
    std::uint8_t version_size{};
    std::uint64_t build_id{};

    [[nodiscard]] std::string_view version_view() const noexcept {
        return {version.data(), version_size};
    }
};

// Every field is optional and zero means unrecorded.
struct Presentation {
    std::uint8_t present_interval{};
    std::uint8_t swapchain_buffers{};
    std::uint32_t texture_format{};
    std::uint32_t handheld_width{};
    std::uint32_t handheld_height{};
};

struct Profile {
    std::array<char, name_capacity> name{};
    std::uint8_t name_size{};
    std::uint64_t title_id{};
    bool supported{};
    std::array<BuildEntry, max_builds> builds{};
    std::uint8_t build_count{};
    Presentation presentation{};

    [[nodiscard]] std::string_view name_view() const noexcept {
        return {name.data(), name_size};
    }

    [[nodiscard]] const BuildEntry* find_build(std::uint64_t build_id) const noexcept;
};

enum class Targeting : std::uint8_t {
    strict = 0,
    permissive = 1,
};

// Ignore unknown values, but fail on a bad known value.
[[nodiscard]] ErrorCode parse(std::string_view text, Profile& out) noexcept;

[[nodiscard]] ErrorCode check(
    const Profile& profile,
    std::uint64_t title_id,
    std::uint64_t build_id,
    Targeting targeting) noexcept;

[[nodiscard]] bool path_for(
    std::string_view root,
    std::uint64_t title_id,
    std::array<char, 128>& out) noexcept;

void format_id(std::uint64_t value, std::array<char, 17>& out) noexcept;

} // namespace lsfg::profile
