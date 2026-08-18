// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>

namespace lsfg::plugin::coexistence {

enum class Stage : std::uint32_t {
    not_started,
    nvdrv_session,
    device,
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

struct Result {
    Stage stage{Stage::not_started};
    std::uint32_t value{};
    std::size_t arena_bytes{};
    bool passed{};
};

using ProgressCallback = void (*)(Stage stage) noexcept;

[[nodiscard]] Result run(ProgressCallback progress) noexcept;

[[nodiscard]] const char* stage_name(Stage stage) noexcept;

inline constexpr std::uint32_t expected_value = 0x4c53'4647U;

} // namespace lsfg::plugin::coexistence
