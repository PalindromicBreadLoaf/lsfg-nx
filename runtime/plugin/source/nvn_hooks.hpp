// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <lsfg/common/error.hpp>

#include <cstdint>

namespace lsfg::plugin::nvn {

struct Options {
    bool reporting_enabled{};
    bool run_coexistence_probe{};
    // Reports the whole discovery trace.
    bool verbose_trace{};
    // Presents between pacing reports,.
    std::uint32_t report_every{};
    // What a present is expected to land on.
    std::uint32_t expected_interval_us{};
    std::uint32_t interval_tolerance_us{};
};

[[nodiscard]] ErrorCode install(const Options& options) noexcept;

[[nodiscard]] bool engaged() noexcept;

} // namespace lsfg::plugin::nvn
