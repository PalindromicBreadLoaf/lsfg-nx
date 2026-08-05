// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <lsfg/common/error.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace lsfg {

enum class LogLevel : std::uint8_t {
    debug,
    info,
    warning,
    error,
};

inline constexpr std::size_t log_message_capacity = 160;
inline constexpr std::size_t ring_log_capacity = 256;

struct LogEntry {
    std::uint64_t sequence{};
    std::uint64_t timestamp_us{};
    ErrorCode error{ErrorCode::ok};
    LogLevel level{LogLevel::info};
    std::uint16_t message_size{};
    bool truncated{};
    std::array<char, log_message_capacity> message{};

    [[nodiscard]] std::string_view message_view() const noexcept {
        return {message.data(), message_size};
    }
};

// A bounded, allocation-free log suitable for hook and crash breadcrumb paths.
class RingLog final {
public:
    void push(
        std::uint64_t timestamp_us,
        LogLevel level,
        ErrorCode error,
        std::string_view message) noexcept;

    [[nodiscard]] std::size_t snapshot(std::span<LogEntry> output) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    void clear() noexcept;

private:
    void lock() const noexcept;
    void unlock() const noexcept;

    mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::array<LogEntry, ring_log_capacity> entries_{};
    std::uint64_t next_sequence_{1};
    std::size_t size_{};
};

} // namespace lsfg
