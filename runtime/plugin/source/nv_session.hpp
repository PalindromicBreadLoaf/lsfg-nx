// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <switch.h>

#include <cstdint>

namespace lsfg::plugin::nv_session {

struct ArmResult {
    ::Result value{};
    bool armed{};
};

[[nodiscard]] ArmResult arm() noexcept;

[[nodiscard]] bool captured() noexcept;

[[nodiscard]] ::Result capture_result() noexcept;

} // namespace lsfg::plugin::nv_session

extern "C" Result lsfg_plugin_borrow_nvdrv_session(Handle *out);
