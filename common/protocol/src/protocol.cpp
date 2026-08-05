// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/common/protocol.hpp>

#include <algorithm>

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
