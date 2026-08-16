// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/common/protocol.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>

namespace lsfg::protocol {

void initialize(ControlBlock& control) noexcept {
    control = ControlBlock{};
    control.abi_version = abi_version;
    control.struct_size = static_cast<std::uint32_t>(sizeof(ControlBlock));
}

void initialize(StatusBlock& status) noexcept {
    status = StatusBlock{};
    status.abi_version = abi_version;
    status.struct_size = static_cast<std::uint32_t>(sizeof(StatusBlock));
}

void initialize(ReportBlock& reports) noexcept {
    reports = ReportBlock{};
    reports.magic = report_magic;
    reports.abi_version = abi_version;
    reports.struct_size = static_cast<std::uint32_t>(sizeof(ReportBlock));
    reports.slot_count = report_slot_count;
    reports.line_capacity = report_line_capacity;
}

ErrorCode validate(const ControlBlock& control) noexcept {
    if (control.abi_version != abi_version) {
        return ErrorCode::protocol_version_mismatch;
    }
    if (control.struct_size != sizeof(ControlBlock)) {
        return ErrorCode::protocol_version_mismatch;
    }
    if (!sequence_is_stable(control.sequence)) {
        return ErrorCode::protocol_message_invalid;
    }
    if (control.mode > GenerationMode::interpolate_2x) {
        return ErrorCode::protocol_message_invalid;
    }
    if (control.preset > QualityPreset::performance) {
        return ErrorCode::protocol_message_invalid;
    }
    if (control.debug_view > DebugView::history_age) {
        return ErrorCode::protocol_message_invalid;
    }
    return ErrorCode::ok;
}

ErrorCode validate(const StatusBlock& status) noexcept {
    if (status.abi_version != abi_version) {
        return ErrorCode::protocol_version_mismatch;
    }
    if (status.struct_size != sizeof(StatusBlock)) {
        return ErrorCode::protocol_version_mismatch;
    }
    if (!sequence_is_stable(status.sequence)) {
        return ErrorCode::protocol_message_invalid;
    }
    if (status.state > RuntimeState::error) {
        return ErrorCode::protocol_message_invalid;
    }
    if (status.api > GraphicsApi::vulkan) {
        return ErrorCode::protocol_message_invalid;
    }
    if (status.message_size > message_capacity) {
        return ErrorCode::protocol_message_invalid;
    }
    return ErrorCode::ok;
}

ErrorCode validate(const ReportBlock& reports) noexcept {
    if (reports.magic != report_magic || reports.abi_version != abi_version) {
        return ErrorCode::protocol_version_mismatch;
    }
    if (reports.struct_size != sizeof(ReportBlock)
        || reports.slot_count != report_slot_count
        || reports.line_capacity != report_line_capacity) {
        return ErrorCode::protocol_version_mismatch;
    }
    return ErrorCode::ok;
}

bool push_report(ReportBlock& reports, const std::string_view text) noexcept {
    if (text.empty()) {
        return false;
    }

    std::atomic_ref write{reports.write_sequence};
    std::atomic_ref read{reports.read_sequence};
    std::atomic_ref dropped{reports.dropped};

    const std::uint64_t next = write.load(std::memory_order_relaxed);
    if (next - read.load(std::memory_order_acquire) >= report_slot_count) {
        dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    ReportSlot& slot
        = reports.slots[static_cast<std::size_t>(next % report_slot_count)];
    slot.length = static_cast<std::uint32_t>(std::min(text.size(), slot.text.size()));
    std::memcpy(slot.text.data(), text.data(), slot.length);
    write.store(next + 1, std::memory_order_release);
    return true;
}

std::string_view peek_report(ReportBlock& reports) noexcept {
    std::atomic_ref write{reports.write_sequence};
    std::atomic_ref read{reports.read_sequence};

    const std::uint64_t next = read.load(std::memory_order_relaxed);
    if (next == write.load(std::memory_order_acquire)) {
        return {};
    }

    ReportSlot& slot
        = reports.slots[static_cast<std::size_t>(next % report_slot_count)];
    if (slot.length > slot.text.size()) {
        return {};
    }
    return {slot.text.data(), slot.length};
}

void consume_report(ReportBlock& reports) noexcept {
    std::atomic_ref write{reports.write_sequence};
    std::atomic_ref read{reports.read_sequence};
    const std::uint64_t next = read.load(std::memory_order_relaxed);
    if (next != write.load(std::memory_order_acquire)) {
        read.store(next + 1, std::memory_order_release);
    }
}

std::uint64_t dropped_reports(ReportBlock& reports) noexcept {
    std::atomic_ref dropped{reports.dropped};
    return dropped.load(std::memory_order_acquire);
}

void set_message(StatusBlock& status, const std::string_view message) noexcept {
    const std::size_t size = std::min(message.size(), message_capacity);
    std::copy_n(message.data(), size, status.message.data());
    std::fill(status.message.begin() + static_cast<std::ptrdiff_t>(size), status.message.end(), '\0');
    status.message_size = static_cast<std::uint16_t>(size);
}

std::string_view state_name(const RuntimeState state) noexcept {
    switch (state) {
    case RuntimeState::unavailable:
        return "unavailable";
    case RuntimeState::bypass:
        return "bypass";
    case RuntimeState::warming:
        return "warming";
    case RuntimeState::active:
        return "active";
    case RuntimeState::degraded:
        return "degraded";
    case RuntimeState::error:
        return "error";
    }
    return "unknown";
}

} // namespace lsfg::protocol
