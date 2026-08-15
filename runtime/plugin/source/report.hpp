// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <lsfg/instrument/presentation.hpp>
#include <lsfg/common/error.hpp>

#include <cstdint>

namespace lsfg::plugin::report {

void configure(
    bool enabled, bool verbose, std::uint32_t presents_between_reports) noexcept;

void on_install(ErrorCode result) noexcept;

void on_queries_resolved(std::uint32_t missing) noexcept;

// The swapchain as it was declared, written once per window.
void on_swapchain(const instrument::SwapchainMap& map) noexcept;

void on_present() noexcept;

// The call order the game brought the graphics API up in.
void discovery_trace() noexcept;

void pacing() noexcept;

} // namespace lsfg::plugin::report
