// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/instrument/presentation.hpp>

#include <algorithm>

namespace lsfg::instrument {
namespace {

constexpr std::uint32_t index_bits = 32;

[[nodiscard]] constexpr bool trackable(const std::int32_t index) noexcept {
    return index >= 0 && static_cast<std::uint32_t>(index) < index_bits;
}

} // namespace

void Stat::add(const std::uint64_t value) noexcept {
    if (count == 0) {
        min = value;
        max = value;
    } else {
        min = std::min(min, value);
        max = std::max(max, value);
    }
    sum += value;
    ++count;
}

bool TextureRecord::same_layout_as(const TextureRecord& other) const noexcept {
    return width == other.width && height == other.height && depth == other.depth
        && levels == other.levels && samples == other.samples && format == other.format
        && flags == other.flags && target == other.target && stride == other.stride
        && storage_size == other.storage_size && storage_alignment == other.storage_alignment
        && storage_class == other.storage_class && pool_flags == other.pool_flags;
}

void TextureLog::record(const TextureRecord& texture) noexcept {
    if (!texture.valid()) {
        return;
    }

    ++observed_;

    for (std::size_t index = 0; index < size_; ++index) {
        if (entries_[index].handle == texture.handle) {
            entries_[index] = texture;
            return;
        }
    }

    entries_[next_] = texture;
    next_ = (next_ + 1U) % capacity;
    size_ = std::min(size_ + 1U, capacity);
}

const TextureRecord* TextureLog::find(const std::uint64_t handle) const noexcept {
    if (handle == 0) {
        return nullptr;
    }
    for (std::size_t index = 0; index < size_; ++index) {
        if (entries_[index].handle == handle) {
            return &entries_[index];
        }
    }
    return nullptr;
}

void SwapchainMap::clear() noexcept {
    *this = SwapchainMap{};
}

bool SwapchainMap::adopt(const std::uint64_t window_handle,
    const std::span<const std::uint64_t> texture_handles, const TextureLog& log) noexcept {
    if (texture_handles.empty() || texture_handles.size() > capacity) {
        return false;
    }

    clear();
    window = window_handle;
    texture_count = static_cast<std::uint32_t>(texture_handles.size());

    for (std::size_t index = 0; index < texture_handles.size(); ++index) {
        handles[index] = texture_handles[index];
        if (const TextureRecord* const found = log.find(texture_handles[index])) {
            textures[index] = *found;
            ++resolved_count;
        }
    }

    uniform = complete();
    for (std::size_t index = 1; uniform && index < texture_count; ++index) {
        uniform = textures[index].same_layout_as(textures[0]);
    }
    return true;
}

std::size_t SwapchainMap::index_of(const std::uint64_t handle) const noexcept {
    for (std::size_t index = 0; index < texture_count; ++index) {
        if (handles[index] == handle) {
            return index;
        }
    }
    return capacity;
}

std::string_view event_name(const EventKind kind) noexcept {
    switch (kind) {
    case EventKind::bootstrap_loader:
        return "bootstrap_loader";
    case EventKind::device_get_proc_address:
        return "device_get_proc_address";
    case EventKind::memory_pool_initialize:
        return "memory_pool_initialize";
    case EventKind::texture_initialize:
        return "texture_initialize";
    case EventKind::window_builder_set_textures:
        return "window_builder_set_textures";
    case EventKind::window_initialize:
        return "window_initialize";
    case EventKind::window_set_present_interval:
        return "window_set_present_interval";
    case EventKind::window_set_num_active_textures:
        return "window_set_num_active_textures";
    case EventKind::window_acquire_texture:
        return "window_acquire_texture";
    case EventKind::queue_submit_commands:
        return "queue_submit_commands";
    case EventKind::queue_fence_sync:
        return "queue_fence_sync";
    case EventKind::queue_finish:
        return "queue_finish";
    case EventKind::sync_wait:
        return "sync_wait";
    case EventKind::queue_present_texture:
        return "queue_present_texture";
    case EventKind::unknown:
        break;
    }
    return "unknown";
}

void EventTrace::push(const EventKind kind, const std::uint64_t tick, const std::uint64_t object,
    const std::uint64_t detail) noexcept {
    if (size_ == capacity) {
        ++dropped_;
        return;
    }
    entries_[size_] = Event{tick, object, detail, kind};
    ++size_;
}

void PresentTimeline::set_expected_interval_us(
    const std::uint32_t microseconds, const std::uint32_t tolerance) noexcept {
    expected_interval_us_ = microseconds;
    tolerance_us_ = tolerance;
}

void PresentTimeline::on_acquire_begin(const std::uint64_t tick) noexcept {
    last_acquire_begin_tick_ = tick;
    acquire_open_ = true;
}

void PresentTimeline::on_acquire_end(
    const std::uint64_t tick, const std::int32_t texture_index) noexcept {
    ++acquires_;
    if (acquire_open_ && tick >= last_acquire_begin_tick_) {
        acquire_block_.add(ticks_to_us(tick - last_acquire_begin_tick_));
    }
    acquire_open_ = false;
    last_acquire_end_tick_ = tick;

    if (trackable(texture_index)) {
        held_indices_ |= 1U << static_cast<std::uint32_t>(texture_index);
    }
}

void PresentTimeline::on_submit(const std::uint64_t tick) noexcept {
    ++submits_;
    last_submit_tick_ = tick;
}

void PresentTimeline::on_fence(const std::uint64_t /*tick*/) noexcept {
    ++fences_;
}

void PresentTimeline::on_sync_wait(
    const std::uint64_t begin_tick, const std::uint64_t end_tick) noexcept {
    ++sync_waits_;
    if (end_tick >= begin_tick) {
        sync_wait_block_.add(ticks_to_us(end_tick - begin_tick));
    }
}

void PresentTimeline::on_present(
    const std::uint64_t tick, const std::int32_t texture_index) noexcept {
    ++presents_;

    if (trackable(texture_index)) {
        const std::uint32_t bit = 1U << static_cast<std::uint32_t>(texture_index);
        if ((held_indices_ & bit) == 0) {
            ++sequence_faults_;
        }
        held_indices_ &= ~bit;
    }

    if (last_present_tick_ != 0 && tick >= last_present_tick_) {
        const std::uint64_t interval = ticks_to_us(tick - last_present_tick_);
        present_interval_.add(interval);

        const std::size_t bucket = std::min<std::uint64_t>(
            interval / histogram_bucket_us, histogram_buckets - 1U);
        ++histogram_[bucket];

        if (expected_interval_us_ != 0) {
            const std::uint64_t expected = expected_interval_us_;
            const std::uint64_t deviation
                = interval > expected ? interval - expected : expected - interval;
            if (deviation > tolerance_us_) {
                ++outliers_;
            }
        }
    } else if (last_present_tick_ != 0) {
        ++sequence_faults_;
    }

    if (last_acquire_end_tick_ != 0 && tick >= last_acquire_end_tick_) {
        acquire_to_present_.add(ticks_to_us(tick - last_acquire_end_tick_));
    }
    if (last_submit_tick_ != 0 && tick >= last_submit_tick_) {
        submit_to_present_.add(ticks_to_us(tick - last_submit_tick_));
    }

    last_present_tick_ = tick;
    last_present_index_ = texture_index;
}

void PresentTimeline::reset_intervals() noexcept {
    present_interval_ = Stat{};
    acquire_block_ = Stat{};
    acquire_to_present_ = Stat{};
    submit_to_present_ = Stat{};
    sync_wait_block_ = Stat{};
    histogram_ = {};
    outliers_ = 0;
    last_present_tick_ = 0;
    last_acquire_begin_tick_ = 0;
    last_acquire_end_tick_ = 0;
    last_submit_tick_ = 0;
    acquire_open_ = false;
}

} // namespace lsfg::instrument
