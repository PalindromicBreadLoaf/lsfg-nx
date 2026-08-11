// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/common/image_compare.hpp>
#include <lsfg/common/image_file.hpp>
#include <lsfg/common/image_graph.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

namespace {

void require(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

constexpr lsfg::graph::Extent small{.width = 512, .height = 256};

std::vector<std::uint8_t> test_frame(const std::uint32_t frame) {
    std::vector<std::uint8_t> pixels(
        lsfg::image::pixel_bytes(small, lsfg::graph::Format::rgba8));
    require(
        lsfg::succeeded(lsfg::image::draw_test_frame(pixels, small, frame)),
        "the test frame is drawn");
    return pixels;
}

lsfg::image::DumpHeader header_for(
    const std::span<const std::uint8_t> pixels,
    const lsfg::graph::Format format = lsfg::graph::Format::rgba8) {
    return lsfg::image::describe(
        lsfg::image::Description{
            .extent = small,
            .format = format,
            .role = lsfg::graph::ImageRole::history,
            .image = 0,
            .frame = 7,
            .label = "real-0",
        },
        pixels);
}

void test_dump_round_trip() {
    const std::vector<std::uint8_t> pixels = test_frame(0);
    const lsfg::image::DumpHeader header = header_for(pixels);

    std::vector<std::uint8_t> encoded;
    require(lsfg::succeeded(lsfg::image::encode(header, pixels, encoded)), "the dump encodes");
    require(
        encoded.size() == sizeof(lsfg::image::DumpHeader) + pixels.size(),
        "the file is the header and the pixels and nothing else");

    lsfg::image::Dump back;
    require(lsfg::succeeded(lsfg::image::decode(encoded, back)), "the dump decodes");
    require(back.pixels == pixels, "the pixels survive the round trip");
    require(back.header.width == small.width, "the extent survives");
    require(back.header.height == small.height, "the height survives");
    require(back.header.frame == 7, "the frame index survives");
    require(lsfg::image::label_of(back.header) == "real-0", "the label survives");
}

void test_dump_fails_closed() {
    const std::vector<std::uint8_t> pixels = test_frame(0);
    const lsfg::image::DumpHeader header = header_for(pixels);

    std::vector<std::uint8_t> encoded;
    require(lsfg::succeeded(lsfg::image::encode(header, pixels, encoded)), "the dump encodes");

    lsfg::image::Dump back;

    {
        std::vector<std::uint8_t> truncated(encoded.begin(), encoded.begin() + 16);
        require(!lsfg::succeeded(lsfg::image::decode(truncated, back)), "a stub is refused");
    }
    {
        std::vector<std::uint8_t> foreign = encoded;
        foreign[0] ^= 0xFFU;
        require(!lsfg::succeeded(lsfg::image::decode(foreign, back)), "a foreign magic is refused");
    }
    {
        std::vector<std::uint8_t> newer = encoded;
        newer[4] = lsfg::image::dump_version + 1U;
        require(
            lsfg::image::decode(newer, back) == lsfg::ErrorCode::cache_version_mismatch,
            "a future version is refused as a version mismatch");
    }
    {
        std::vector<std::uint8_t> tampered = encoded;
        tampered[sizeof(lsfg::image::DumpHeader) + 100U] ^= 0x01U;
        require(
            lsfg::image::decode(tampered, back) == lsfg::ErrorCode::cache_integrity_failure,
            "a flipped pixel bit is caught by the checksum");
    }
    {
        std::vector<std::uint8_t> short_payload = encoded;
        short_payload.pop_back();
        require(
            !lsfg::succeeded(lsfg::image::decode(short_payload, back)),
            "a payload that does not match the extent is refused");
    }
}

void test_dds_export() {
    const std::vector<std::uint8_t> pixels = test_frame(1);
    const lsfg::image::DumpHeader header = header_for(pixels);

    std::vector<std::uint8_t> dds;
    require(lsfg::succeeded(lsfg::image::encode_dds(header, pixels, dds)), "the DDS encodes");
    require(
        dds.size() == lsfg::image::dds_header_bytes + pixels.size(),
        "the DDS is a 128 byte header and the pixels");
    require(std::memcmp(dds.data(), "DDS ", 4) == 0, "the DDS magic is there");
    require(dds[4] == 124, "the header declares its own size");
    // The reference reader skips the header and copies the rest verbatim.
    require(
        std::memcmp(dds.data() + lsfg::image::dds_header_bytes, pixels.data(), pixels.size()) == 0,
        "the pixels follow the header unchanged");

    lsfg::image::DumpHeader floating = header;
    floating.format = static_cast<std::uint8_t>(lsfg::graph::Format::rgba16f);
    require(
        lsfg::image::encode_dds(floating, pixels, dds) == lsfg::ErrorCode::unsupported,
        "a floating point image has no eight-bit DDS form");
}

void test_dds_round_trip() {
    const std::vector<std::uint8_t> pixels = test_frame(1);
    const lsfg::image::DumpHeader header = header_for(pixels);

    std::vector<std::uint8_t> dds;
    require(lsfg::succeeded(lsfg::image::encode_dds(header, pixels, dds)), "the DDS encodes");

    lsfg::image::Dump back;
    require(
        lsfg::succeeded(lsfg::image::decode_dds(
            dds, lsfg::image::Description{.label = "generated"}, back)),
        "the DDS decodes");
    require(back.pixels == pixels, "the pixels survive the round trip");
    require(back.header.width == small.width, "the extent is read out of the header");
    require(back.header.height == small.height, "the height is read out of the header");
    require(lsfg::image::label_of(back.header) == "generated", "the caller names the image");

    {
        std::vector<std::uint8_t> compressed = dds;
        compressed[84] = 'D';
        compressed[85] = 'X';
        compressed[86] = '1';
        compressed[87] = '0';
        require(
            lsfg::image::decode_dds(
                compressed, lsfg::image::Description{.label = "x"}, back)
                == lsfg::ErrorCode::unsupported,
            "a compressed or DX10 file is refused");
    }
    {
        std::vector<std::uint8_t> swizzled = dds;
        swizzled[92] = 0x00;
        swizzled[94] = 0xFF;
        require(
            lsfg::image::decode_dds(
                swizzled, lsfg::image::Description{.label = "x"}, back)
                == lsfg::ErrorCode::unsupported,
            "a different channel order is refused");
    }
    {
        std::vector<std::uint8_t> truncated(dds.begin(), dds.end() - 4);
        require(
            !lsfg::succeeded(lsfg::image::decode_dds(
                truncated, lsfg::image::Description{.label = "x"}, back)),
            "a payload shorter than the extent is refused");
    }
}

void test_test_frames_differ_only_in_the_bar() {
    const std::vector<std::uint8_t> first = test_frame(0);
    const std::vector<std::uint8_t> second = test_frame(1);

    require(first != second, "consecutive frames are not the same image");

    const lsfg::image::Bar left = lsfg::image::find_bar(first, small);
    const lsfg::image::Bar right = lsfg::image::find_bar(second, small);
    require(left.found && right.found, "both frames have a bar");
    require(right.centre > left.centre, "the bar moves right between the frames");

    // Drawing the same frame twice has to give the same bytes.
    require(test_frame(0) == first, "the same frame index draws the same image");
}

void test_identical_images_are_within_tolerance() {
    const std::vector<std::uint8_t> pixels = test_frame(0);
    const lsfg::image::DumpHeader header = header_for(pixels);
    const lsfg::image::Tolerance tolerance
        = lsfg::image::default_tolerance(lsfg::graph::Format::rgba8);

    lsfg::image::Difference difference;
    require(
        lsfg::succeeded(
            lsfg::image::compare(header, pixels, header, pixels, tolerance, difference)),
        "an image compares against itself");
    require(difference.max_abs == 0.0, "nothing differs");
    require(difference.outliers == 0, "there are no outliers");
    require(difference.non_finite == 0, "there is nothing non-finite");
    require(difference.channels == 512U * 256U * 4U, "every channel was compared");
    require(difference.within(tolerance), "an image matches itself");
}

void test_scattered_rounding_is_accepted() {
    const std::vector<std::uint8_t> reference = test_frame(0);
    std::vector<std::uint8_t> measured = reference;
    for (std::size_t index = 0; index < measured.size(); index += 8U) {
        std::uint8_t& value = measured[index];
        value = value == 255 ? 254 : static_cast<std::uint8_t>(value + 1U);
    }

    const lsfg::image::DumpHeader header = header_for(reference);
    const lsfg::image::Tolerance tolerance
        = lsfg::image::default_tolerance(lsfg::graph::Format::rgba8);

    lsfg::image::Difference difference;
    require(
        lsfg::succeeded(
            lsfg::image::compare(header, reference, header, measured, tolerance, difference)),
        "the pair compares");
    require(difference.outliers == 0, "one quantisation step is not an outlier");
    require(
        std::abs(difference.max_abs - (1.0 / 255.0)) < 1e-9,
        "the largest difference is one step");
    require(difference.within(tolerance), "scattered rounding is within tolerance");
}

// A step in the same direction everywhere is a systematic bias rather than
// rounding.
void test_uniform_bias_is_rejected() {
    const std::vector<std::uint8_t> reference = test_frame(0);
    std::vector<std::uint8_t> measured = reference;
    for (std::uint8_t& value : measured) {
        value = value == 255 ? 254 : static_cast<std::uint8_t>(value + 1U);
    }

    const lsfg::image::DumpHeader header = header_for(reference);
    const lsfg::image::Tolerance tolerance
        = lsfg::image::default_tolerance(lsfg::graph::Format::rgba8);

    lsfg::image::Difference difference;
    require(
        lsfg::succeeded(
            lsfg::image::compare(header, reference, header, measured, tolerance, difference)),
        "the pair compares");
    require(difference.outliers == 0, "no single channel is far out");
    require(
        std::abs(difference.mean_abs - (1.0 / 255.0)) < 1e-9,
        "every channel moved by one step");
    require(!difference.within(tolerance), "a whole image one step brighter is rejected");
}

void test_a_wrong_image_is_rejected() {
    const std::vector<std::uint8_t> reference = test_frame(0);
    const std::vector<std::uint8_t> measured = test_frame(1);

    const lsfg::image::DumpHeader header = header_for(reference);
    const lsfg::image::Tolerance tolerance
        = lsfg::image::default_tolerance(lsfg::graph::Format::rgba8);

    lsfg::image::Difference difference;
    require(
        lsfg::succeeded(
            lsfg::image::compare(header, reference, header, measured, tolerance, difference)),
        "the pair compares");
    require(!difference.within(tolerance), "the neighbouring frame is not an acceptable match");
    require(difference.outliers > 0, "the moved bar shows up as outliers");
    require(difference.worst_y < small.height, "the worst pixel is inside the image");
}

void test_mismatched_shapes_are_refused() {
    const std::vector<std::uint8_t> pixels = test_frame(0);
    const lsfg::image::DumpHeader header = header_for(pixels);

    lsfg::image::DumpHeader taller = header;
    taller.height = small.height * 2U;

    const lsfg::image::Tolerance tolerance
        = lsfg::image::default_tolerance(lsfg::graph::Format::rgba8);

    lsfg::image::Difference difference;
    require(
        lsfg::image::compare(header, pixels, taller, pixels, tolerance, difference)
            == lsfg::ErrorCode::invalid_argument,
        "two extents are not comparable");

    lsfg::image::DumpHeader floating = header;
    floating.format = static_cast<std::uint8_t>(lsfg::graph::Format::rgba16f);
    require(
        lsfg::image::compare(header, pixels, floating, pixels, tolerance, difference)
            == lsfg::ErrorCode::invalid_argument,
        "two formats are not comparable");
}

void test_half_floats() {
    require(lsfg::image::half_to_float(0x0000U) == 0.0F, "zero");
    require(lsfg::image::half_to_float(0x3C00U) == 1.0F, "one");
    require(lsfg::image::half_to_float(0xBC00U) == -1.0F, "minus one");
    require(lsfg::image::half_to_float(0x4000U) == 2.0F, "two");
    require(
        std::abs(lsfg::image::half_to_float(0x3555U) - (1.0F / 3.0F)) < 1e-3F,
        "a third, to the precision a half holds");
    require(std::isinf(lsfg::image::half_to_float(0x7C00U)), "infinity");
    require(std::isnan(lsfg::image::half_to_float(0x7E00U)), "not a number");
    // The subnormal path renormalises the exponent by hand.
    require(
        std::abs(lsfg::image::half_to_float(0x0001U) - 5.9604645e-8F) < 1e-13F,
        "the smallest subnormal");
    require(
        std::abs(lsfg::image::half_to_float(0x03FFU) - 6.0975552e-5F) < 1e-10F,
        "the largest subnormal");
    require(
        lsfg::image::half_to_float(0x0400U) == 6.1035156e-5F,
        "the smallest normal is one step above it");
}

void test_non_finite_channels_fail_whatever_the_tolerance() {
    const lsfg::graph::Extent tiny{.width = 4, .height = 4};
    const std::uint64_t bytes = lsfg::image::pixel_bytes(tiny, lsfg::graph::Format::rgba16f);

    std::vector<std::uint8_t> reference(static_cast<std::size_t>(bytes), 0);
    std::vector<std::uint8_t> measured = reference;
    measured[0] = 0x00U;
    measured[1] = 0x7EU;

    const lsfg::image::DumpHeader header = lsfg::image::describe(
        lsfg::image::Description{
            .extent = tiny,
            .format = lsfg::graph::Format::rgba16f,
            .label = "history",
        },
        reference);

    lsfg::image::Tolerance everything{
        .max_abs = 1e9,
        .mean_abs = 1e9,
        .rmse = 1e9,
        .outlier_fraction = 1.0,
    };

    lsfg::image::Difference difference;
    require(
        lsfg::succeeded(
            lsfg::image::compare(header, reference, header, measured, everything, difference)),
        "the pair compares");
    require(difference.non_finite == 1, "the one bad channel is counted");
    require(!difference.within(everything), "a NaN fails however wide the tolerance is");
}

} // namespace

int main() {
    test_dump_round_trip();
    test_dump_fails_closed();
    test_dds_export();
    test_dds_round_trip();
    test_test_frames_differ_only_in_the_bar();
    test_identical_images_are_within_tolerance();
    test_scattered_rounding_is_accepted();
    test_uniform_bias_is_rejected();
    test_a_wrong_image_is_rejected();
    test_mismatched_shapes_are_refused();
    test_half_floats();
    test_non_finite_channels_fail_whatever_the_tolerance();

    std::cout << "image tests passed\n";
    return EXIT_SUCCESS;
}
