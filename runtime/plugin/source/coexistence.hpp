// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <deko3d.h>

#include <cstddef>
#include <cstdint>

namespace lsfg::plugin::coexistence {

enum class Stage : std::uint32_t {
    not_started,
    nvdrv_session,
    device,
    external_layout,
    queue,
    code_memory,
    shader,
    result_memory,
    command_memory,
    command_buffer,
    command_list,
    submitted,
    waiting,
    timed_out,
    wait_failed,
    completed,
    cleanup,
    verified,
};

struct ExternalLayout {
    std::uint64_t storage_offset{};
    std::uint64_t storage_size{};
    std::uint64_t pool_size{};
    std::uint64_t storage_alignment{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t depth{};
    std::uint32_t levels{};
    std::uint32_t samples{};
    std::uint32_t format{};
    std::uint32_t flags{};
    std::uint32_t target{};
    std::uint32_t stride{};
    std::uint32_t pool_flags{};
    std::uint32_t storage_class{};

    [[nodiscard]] bool valid() const noexcept;
};

struct Result {
    Stage stage{Stage::not_started};
    std::uint32_t value{};
    std::size_t arena_bytes{};
    std::uint64_t layout_size{};
    std::uint32_t layout_alignment{};
    std::uint32_t layout_kind{};
    DkResult layout_result{DkResult_Fail};
    bool layout_passed{};
    bool passed{};
};

using ProgressCallback = void (*)(Stage stage) noexcept;

[[nodiscard]] Result run(
    const ExternalLayout& external_layout, ProgressCallback progress) noexcept;

[[nodiscard]] const char* stage_name(Stage stage) noexcept;

inline constexpr std::uint32_t expected_value = 0x4c53'4647U;

} // namespace lsfg::plugin::coexistence
