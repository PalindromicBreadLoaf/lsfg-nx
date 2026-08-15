// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Running a piece of installation work at the first call the game itself
// makes, rather than while it is still being loaded.
namespace lsfg::plugin::deferred {

using Callback = void (*)();

// Returns false when the game does not import the trigger.
[[nodiscard]] bool arm(Callback callback) noexcept;

[[nodiscard]] bool fired() noexcept;

} // namespace lsfg::plugin::deferred
