// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <lsfg/common/error.hpp>
#include <lsfg/common/image_file.hpp>
#include <lsfg/common/image_graph.hpp>

#include <cstdint>
#include <span>

// How far apart two images are allowed to be.
namespace lsfg::image {

struct Tolerance {
    double max_abs{};
    double mean_abs{};
    double rmse{};
    double outlier_fraction{};
};

// TODO: Tighten this once a baseline has been established.
[[nodiscard]] Tolerance default_tolerance(graph::Format format) noexcept;

struct Difference {
    std::uint64_t channels{};

    double max_abs{};
    double mean_abs{};
    double rmse{};

    std::uint64_t outliers{};
    // Channels that are infinite or nan on either side.
    std::uint64_t non_finite{};

    std::uint32_t worst_x{};
    std::uint32_t worst_y{};
    std::uint32_t worst_channel{};

    [[nodiscard]] double outlier_fraction() const noexcept;
    [[nodiscard]] bool within(const Tolerance& tolerance) const noexcept;
};

// Refuses a pair that is not the same extent and format.
[[nodiscard]] ErrorCode compare(
    const DumpHeader& reference_header,
    std::span<const std::uint8_t> reference,
    const DumpHeader& measured_header,
    std::span<const std::uint8_t> measured,
    const Tolerance& tolerance,
    Difference& out);

[[nodiscard]] ErrorCode compare(
    const Dump& reference,
    const Dump& measured,
    const Tolerance& tolerance,
    Difference& out);

[[nodiscard]] float half_to_float(std::uint16_t half) noexcept;

} // namespace lsfg::image
