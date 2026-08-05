// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/common/ring_log.hpp>

#include <algorithm>

namespace lsfg {

void RingLog::lock() const noexcept {
    while (lock_.test_and_set(std::memory_order_acquire)) {
    }
}

void RingLog::unlock() const noexcept {
    lock_.clear(std::memory_order_release);
}

void RingLog::push(
    const std::uint64_t timestamp_us,
    const LogLevel level,
    const ErrorCode error,
    const std::string_view message) noexcept {
    lock();

    const std::uint64_t sequence = next_sequence_++;
    LogEntry& entry = entries_[(sequence - 1U) % ring_log_capacity];
    entry = {};
    entry.sequence = sequence;
    entry.timestamp_us = timestamp_us;
    entry.error = error;
    entry.level = level;

    const std::size_t bytes_to_copy = std::min(message.size(), log_message_capacity);
    std::copy_n(message.data(), bytes_to_copy, entry.message.data());
    entry.message_size = static_cast<std::uint16_t>(bytes_to_copy);
    entry.truncated = message.size() > bytes_to_copy;
    size_ = std::min(size_ + 1U, ring_log_capacity);

    unlock();
}

std::size_t RingLog::snapshot(const std::span<LogEntry> output) const noexcept {
    lock();

    const std::size_t count = std::min(size_, output.size());
    const std::uint64_t first_sequence = next_sequence_ - count;
    for (std::size_t index = 0; index < count; ++index) {
        const std::uint64_t sequence = first_sequence + index;
        output[index] = entries_[(sequence - 1U) % ring_log_capacity];
    }

    unlock();
    return count;
}

std::size_t RingLog::size() const noexcept {
    lock();
    const std::size_t result = size_;
    unlock();
    return result;
}

void RingLog::clear() noexcept {
    lock();
    entries_ = {};
    next_sequence_ = 1;
    size_ = 0;
    unlock();
}

} // namespace lsfg

