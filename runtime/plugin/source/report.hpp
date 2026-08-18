// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <lsfg/instrument/presentation.hpp>
#include <lsfg/common/error.hpp>

#include <deko3d.h>

#include <cstddef>
#include <cstdint>

namespace lsfg::plugin::report {

[[nodiscard]] bool prepare_shared_transport() noexcept;

void configure(
    bool enabled, bool verbose, std::uint32_t presents_between_reports) noexcept;

void on_install(ErrorCode result) noexcept;

void on_queries_resolved(std::uint32_t missing) noexcept;

void on_coexistence_started() noexcept;

void on_coexistence_progress(const char* stage) noexcept;

void on_coexistence_finished(
    const char* stage,
    bool passed,
    std::uint32_t value,
    std::size_t arena_bytes,
    bool layout_passed,
    DkResult layout_result,
    std::uint64_t layout_size,
    std::uint32_t layout_alignment,
    std::uint32_t layout_kind) noexcept;

// The swapchain as it was declared, written once per window.
void on_swapchain(const instrument::SwapchainMap& map) noexcept;

void on_present() noexcept;

// The call order the game brought the graphics API up in.
void discovery_trace() noexcept;

void pacing() noexcept;

} // namespace lsfg::plugin::report
