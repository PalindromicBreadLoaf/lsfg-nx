// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// What a graphics interception layer observes about a game's presentation,
// separated from the interception itself so it can be reasoned about and
// tested off-device.
namespace lsfg::instrument {

inline constexpr std::uint64_t tick_frequency = 19'200'000;

[[nodiscard]] constexpr std::uint64_t ticks_to_us(const std::uint64_t ticks) noexcept {
    return (ticks * 1'000'000U) / tick_frequency;
}

[[nodiscard]] constexpr std::uint64_t us_to_ticks(const std::uint64_t microseconds) noexcept {
    return (microseconds * tick_frequency) / 1'000'000U;
}

struct Stat {
    std::uint64_t count{};
    std::uint64_t sum{};
    std::uint64_t min{};
    std::uint64_t max{};

    void add(std::uint64_t value) noexcept;

    [[nodiscard]] std::uint64_t mean() const noexcept {
        return count == 0 ? 0 : sum / count;
    }
};

struct TextureRecord {
    std::uint64_t handle{};
    std::uint64_t memory_pool{};
    std::uint64_t pool_address{};
    std::uint64_t pool_cpu_address{};
    std::uint64_t texture_address{};
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

    [[nodiscard]] bool valid() const noexcept {
        return handle != 0;
    }

    [[nodiscard]] bool same_layout_as(const TextureRecord& other) const noexcept;
};

// The last few textures the game built.
class TextureLog {
public:
    static constexpr std::size_t capacity = 64;

    void record(const TextureRecord& texture) noexcept;

    [[nodiscard]] const TextureRecord* find(std::uint64_t handle) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }

    [[nodiscard]] std::uint64_t observed() const noexcept {
        return observed_;
    }

private:
    std::array<TextureRecord, capacity> entries_{};
    std::size_t next_{};
    std::size_t size_{};
    std::uint64_t observed_{};
};

struct SwapchainMap {
    static constexpr std::size_t capacity = 8;

    std::uint64_t window{};
    std::uint32_t texture_count{};
    std::uint32_t resolved_count{};
    std::array<std::uint64_t, capacity> handles{};
    std::array<TextureRecord, capacity> textures{};

    std::uint8_t present_interval{};
    std::uint8_t active_textures{};

    bool uniform{};

    void clear() noexcept;

    [[nodiscard]] bool adopt(
        std::uint64_t window_handle,
        std::span<const std::uint64_t> texture_handles,
        const TextureLog& log) noexcept;

    [[nodiscard]] bool complete() const noexcept {
        return texture_count != 0 && resolved_count == texture_count;
    }

    [[nodiscard]] std::size_t index_of(std::uint64_t handle) const noexcept;
};

enum class EventKind : std::uint8_t {
    unknown = 0,
    bootstrap_loader = 1,
    device_get_proc_address = 2,
    memory_pool_initialize = 3,
    texture_initialize = 4,
    window_builder_set_textures = 5,
    window_initialize = 6,
    window_set_present_interval = 7,
    window_set_num_active_textures = 8,
    window_acquire_texture = 9,
    queue_submit_commands = 10,
    queue_fence_sync = 11,
    queue_finish = 12,
    sync_wait = 13,
    queue_present_texture = 14,
};

[[nodiscard]] std::string_view event_name(EventKind kind) noexcept;

struct Event {
    std::uint64_t tick{};
    std::uint64_t object{};
    std::uint64_t detail{};
    EventKind kind{EventKind::unknown};
};

// The first calls in call order.
class EventTrace {
public:
    static constexpr std::size_t capacity = 256;

    void push(EventKind kind, std::uint64_t tick, std::uint64_t object,
        std::uint64_t detail) noexcept;

    [[nodiscard]] std::span<const Event> view() const noexcept {
        return {entries_.data(), size_};
    }

    [[nodiscard]] std::uint64_t dropped() const noexcept {
        return dropped_;
    }

    [[nodiscard]] bool full() const noexcept {
        return size_ == capacity;
    }

private:
    std::array<Event, capacity> entries_{};
    std::size_t size_{};
    std::uint64_t dropped_{};
};

// Presentation pacing.
class PresentTimeline {
public:
    static constexpr std::size_t histogram_buckets = 16;
    static constexpr std::uint64_t histogram_bucket_us = 4000;

    void on_acquire_begin(std::uint64_t tick) noexcept;
    void on_acquire_end(std::uint64_t tick, std::int32_t texture_index) noexcept;
    void on_submit(std::uint64_t tick) noexcept;
    void on_fence(std::uint64_t tick) noexcept;
    void on_sync_wait(std::uint64_t begin_tick, std::uint64_t end_tick) noexcept;
    void on_present(std::uint64_t tick, std::int32_t texture_index) noexcept;

    void set_expected_interval_us(std::uint32_t microseconds, std::uint32_t tolerance) noexcept;

    [[nodiscard]] std::uint64_t presents() const noexcept { return presents_; }
    [[nodiscard]] std::uint64_t acquires() const noexcept { return acquires_; }
    [[nodiscard]] std::uint64_t submits() const noexcept { return submits_; }
    [[nodiscard]] std::uint64_t fences() const noexcept { return fences_; }
    [[nodiscard]] std::uint64_t sync_waits() const noexcept { return sync_waits_; }
    [[nodiscard]] std::uint64_t outliers() const noexcept { return outliers_; }
    [[nodiscard]] std::uint64_t sequence_faults() const noexcept { return sequence_faults_; }
    [[nodiscard]] std::int32_t last_present_index() const noexcept { return last_present_index_; }

    [[nodiscard]] const Stat& present_interval() const noexcept { return present_interval_; }
    [[nodiscard]] const Stat& acquire_block() const noexcept { return acquire_block_; }
    [[nodiscard]] const Stat& acquire_to_present() const noexcept { return acquire_to_present_; }
    [[nodiscard]] const Stat& submit_to_present() const noexcept { return submit_to_present_; }
    [[nodiscard]] const Stat& sync_wait_block() const noexcept { return sync_wait_block_; }

    [[nodiscard]] std::span<const std::uint32_t> histogram() const noexcept {
        return histogram_;
    }

    void reset_intervals() noexcept;

private:
    Stat present_interval_{};
    Stat acquire_block_{};
    Stat acquire_to_present_{};
    Stat submit_to_present_{};
    Stat sync_wait_block_{};

    std::array<std::uint32_t, histogram_buckets> histogram_{};

    std::uint64_t presents_{};
    std::uint64_t acquires_{};
    std::uint64_t submits_{};
    std::uint64_t fences_{};
    std::uint64_t sync_waits_{};
    std::uint64_t outliers_{};
    std::uint64_t sequence_faults_{};

    std::uint64_t last_present_tick_{};
    std::uint64_t last_acquire_begin_tick_{};
    std::uint64_t last_acquire_end_tick_{};
    std::uint64_t last_submit_tick_{};

    std::uint32_t expected_interval_us_{};
    std::uint32_t tolerance_us_{};
    std::uint32_t held_indices_{};
    std::int32_t last_present_index_{-1};
    bool acquire_open_{};
};

} // namespace lsfg::instrument
