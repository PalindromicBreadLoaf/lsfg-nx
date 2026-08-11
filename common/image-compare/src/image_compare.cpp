// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/common/image_compare.hpp>

#include <cmath>
#include <cstddef>
#include <cstring>

namespace lsfg::image {

namespace {

std::uint32_t channels_per_pixel(const graph::Format format) noexcept {
    return format == graph::Format::r8 ? 1U : 4U;
}

std::uint16_t read_u16(const std::span<const std::uint8_t> bytes, const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U));
}

double channel_at(
    const std::span<const std::uint8_t> pixels,
    const graph::Format format,
    const std::size_t index) noexcept {
    if (format == graph::Format::rgba16f) {
        return half_to_float(read_u16(pixels, index * 2U));
    }
    return static_cast<double>(pixels[index]) / 255.0;
}

} // namespace

float half_to_float(const std::uint16_t half) noexcept {
    const std::uint32_t sign = static_cast<std::uint32_t>(half & 0x8000U) << 16U;
    const std::uint32_t exponent = (half >> 10U) & 0x1FU;
    const std::uint32_t mantissa = half & 0x3FFU;

    std::uint32_t bits{};
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            // Subnormal on the half side, normal once the exponent is
            // renormalised into single precision.
            std::uint32_t shifted = mantissa;
            std::uint32_t leading = 0;
            while ((shifted & 0x400U) == 0) {
                shifted <<= 1U;
                ++leading;
            }
            shifted &= 0x3FFU;
            bits = sign | ((127U - 14U - leading) << 23U) | (shifted << 13U);
        }
    } else if (exponent == 0x1FU) {
        bits = sign | 0x7F80'0000U | (mantissa << 13U);
    } else {
        bits = sign | ((exponent + 127U - 15U) << 23U) | (mantissa << 13U);
    }

    float value{};
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

Tolerance default_tolerance(const graph::Format format) noexcept {
    switch (format) {
        case graph::Format::rgba16f:
            return Tolerance{
                .max_abs = 0.02,
                .mean_abs = 0.002,
                .rmse = 0.005,
                .outlier_fraction = 0.001,
            };
        case graph::Format::rgba8:
        case graph::Format::r8:
        default:
            return Tolerance{
                .max_abs = 2.0 / 255.0,
                .mean_abs = 0.5 / 255.0,
                .rmse = 1.0 / 255.0,
                .outlier_fraction = 0.001,
            };
    }
}

double Difference::outlier_fraction() const noexcept {
    return channels == 0 ? 0.0 : static_cast<double>(outliers) / static_cast<double>(channels);
}

bool Difference::within(const Tolerance& tolerance) const noexcept {
    return non_finite == 0
        && mean_abs <= tolerance.mean_abs
        && rmse <= tolerance.rmse
        && outlier_fraction() <= tolerance.outlier_fraction;
}

ErrorCode compare(
    const DumpHeader& reference_header,
    const std::span<const std::uint8_t> reference,
    const DumpHeader& measured_header,
    const std::span<const std::uint8_t> measured,
    const Tolerance& tolerance,
    Difference& out) {
    if (reference_header.width != measured_header.width
        || reference_header.height != measured_header.height
        || reference_header.format != measured_header.format) {
        return ErrorCode::invalid_argument;
    }

    const auto format = static_cast<graph::Format>(reference_header.format);
    const graph::Extent extent{
        .width = reference_header.width,
        .height = reference_header.height,
    };

    if (reference.size() != pixel_bytes(extent, format)
        || measured.size() != reference.size()) {
        return ErrorCode::invalid_argument;
    }

    const std::uint32_t per_pixel = channels_per_pixel(format);
    const std::uint64_t total = static_cast<std::uint64_t>(extent.width) * extent.height
        * per_pixel;

    out = Difference{};
    out.channels = total;

    double sum_abs = 0;
    double sum_squares = 0;

    for (std::uint64_t index = 0; index < total; ++index) {
        const double left = channel_at(reference, format, static_cast<std::size_t>(index));
        const double right = channel_at(measured, format, static_cast<std::size_t>(index));

        if (!std::isfinite(left) || !std::isfinite(right)) {
            ++out.non_finite;
            continue;
        }

        const double difference = std::abs(left - right);
        sum_abs += difference;
        sum_squares += difference * difference;

        if (difference > tolerance.max_abs) {
            ++out.outliers;
        }
        if (difference > out.max_abs) {
            out.max_abs = difference;
            const std::uint64_t pixel = index / per_pixel;
            out.worst_x = static_cast<std::uint32_t>(pixel % extent.width);
            out.worst_y = static_cast<std::uint32_t>(pixel / extent.width);
            out.worst_channel = static_cast<std::uint32_t>(index % per_pixel);
        }
    }

    // Non-finite channels are counted as a defect.
    const std::uint64_t compared = total - out.non_finite;
    if (compared != 0) {
        out.mean_abs = sum_abs / static_cast<double>(compared);
        out.rmse = std::sqrt(sum_squares / static_cast<double>(compared));
    }
    return ErrorCode::ok;
}

ErrorCode compare(
    const Dump& reference,
    const Dump& measured,
    const Tolerance& tolerance,
    Difference& out) {
    return compare(reference.header, reference.pixels, measured.header, measured.pixels,
        tolerance, out);
}

} // namespace lsfg::image
