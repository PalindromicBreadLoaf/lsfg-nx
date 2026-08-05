// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <lsfg/common/error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace lsfg::protocol {

// Bump on any layout or semantic change.
inline constexpr std::uint32_t abi_version = 1;

inline constexpr std::size_t message_capacity = 96;

enum class GenerationMode : std::uint8_t {
    off = 0,
    interpolate_2x = 1,
};

enum class QualityPreset : std::uint8_t {
    conservative = 0,
    balanced = 1,
    performance = 2,
};

enum class DebugView : std::uint8_t {
    none = 0,
    generated_only = 1,
    frame_identifier = 2,
    history_age = 3,
};

enum class RuntimeState : std::uint8_t {
    unavailable = 0,
    bypass = 1,
    warming = 2,
    active = 3,
    degraded = 4,
    error = 5,
};

enum class GraphicsApi : std::uint8_t {
    unknown = 0,
    nvn = 1,
    vulkan = 2,
};

// Written by the overlay, read by the runtime.
struct alignas(8) ControlBlock {
    std::uint32_t abi_version{};
    std::uint32_t struct_size{};
    std::uint64_t sequence{};

    std::uint8_t enabled{};
    GenerationMode mode{GenerationMode::off};
    QualityPreset preset{QualityPreset::conservative};
    std::uint8_t strict_targeting{1};
    DebugView debug_view{DebugView::none};
    // Sampled before any generated presentation work rather than at the normal
    // control boundary, so it cannot be delayed by a frame in flight.
    std::uint8_t emergency_bypass{};
    std::uint8_t reserved0_[2]{};

    std::uint32_t reset_history_counter{};
    std::uint32_t max_generation_time_us{};
};

// Written by the runtime, read by the overlay.
struct alignas(8) StatusBlock {
    std::uint32_t abi_version{};
    std::uint32_t struct_size{};
    std::uint64_t sequence{};

    std::uint32_t runtime_version{};
    std::uint32_t acted_reset_counter{};

    std::uint64_t title_id{};
    std::uint64_t build_id_low{};
    std::uint64_t build_id_high{};
    std::uint64_t missed_deadlines{};

    RuntimeState state{RuntimeState::unavailable};
    GraphicsApi api{GraphicsApi::unknown};
    std::uint8_t history_valid{};
    std::uint8_t swapchain_buffers{};
    std::uint8_t reserved0_[4]{};

    std::uint32_t swapchain_width{};
    std::uint32_t swapchain_height{};
    std::uint32_t swapchain_format{};

    std::uint32_t real_fps_millis{};
    std::uint32_t present_fps_millis{};

    std::uint32_t generation_time_avg_us{};
    std::uint32_t generation_time_max_us{};

    std::uint32_t imported_allocations{};
    std::uint32_t gpu_memory_bytes{};

    ErrorCode last_error{ErrorCode::ok};
    std::uint16_t message_size{};
    // Sized so the block ends on an 8-byte boundary without implicit padding.
    std::uint8_t reserved1_[6]{};
    std::array<char, message_capacity> message{};

    [[nodiscard]] std::string_view message_view() const noexcept {
        return {message.data(), message_size};
    }
};

static_assert(sizeof(ControlBlock) == 32);
static_assert(sizeof(StatusBlock) == 208);
static_assert(alignof(ControlBlock) == 8);
static_assert(alignof(StatusBlock) == 8);

[[nodiscard]] constexpr bool sequence_is_stable(const std::uint64_t sequence) noexcept {
    return (sequence & 1U) == 0U;
}

void initialize(ControlBlock& control) noexcept;
void initialize(StatusBlock& status) noexcept;

[[nodiscard]] ErrorCode validate(const ControlBlock& control) noexcept;
[[nodiscard]] ErrorCode validate(const StatusBlock& status) noexcept;

void set_message(StatusBlock& status, std::string_view message) noexcept;

[[nodiscard]] std::string_view state_name(RuntimeState state) noexcept;

} // namespace lsfg::protocol
