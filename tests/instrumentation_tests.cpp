// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/common/profile.hpp>
#include <lsfg/instrument/presentation.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

constexpr std::string_view paper_mario_profile = R"(; a comment
[title]
name = Paper Mario: The Thousand-Year Door
title_id = 0100ECD018EBE000
supported = false

[builds]
1.0.1 = 0EFFE4AF1DEC3A79

[presentation]
present_interval = 2
swapchain_buffers = 3
handheld_width = 1280
handheld_height = 720
texture_format = 0x25
)";

void test_profile_parse() {
    lsfg::profile::Profile profile{};
    require(lsfg::succeeded(lsfg::profile::parse(paper_mario_profile, profile)), "profile parses");
    require(profile.title_id == 0x0100ECD018EBE000ULL, "title id");
    require(!profile.supported, "supported flag");
    require(profile.name_view() == "Paper Mario: The Thousand-Year Door", "title name");
    require(profile.build_count == 1, "one recorded build");
    require(profile.builds[0].version_view() == "1.0.1", "build version");
    require(profile.builds[0].build_id == 0x0EFFE4AF1DEC3A79ULL, "build id");
    require(profile.presentation.present_interval == 2, "present interval");
    require(profile.presentation.swapchain_buffers == 3, "swapchain buffers");
    require(profile.presentation.handheld_width == 1280, "handheld width");
    require(profile.presentation.texture_format == 0x25, "hexadecimal format");

    const lsfg::profile::BuildEntry* const found
        = profile.find_build(0x0EFFE4AF1DEC3A79ULL);
    require(found != nullptr && found->version_view() == "1.0.1", "build lookup");
    require(profile.find_build(1) == nullptr, "unknown build is not found");
}

void test_profile_refusals() {
    const auto refused = [](const std::string_view text, const char* message) {
        lsfg::profile::Profile profile{};
        require(lsfg::profile::parse(text, profile) == lsfg::ErrorCode::invalid_argument, message);
    };

    refused("[title]\nsupported = true\n", "a profile without a title id");
    refused("[title]\ntitle_id = 0100ECD018EBE00\n", "a title id of the wrong length");
    refused("[title]\ntitle_id = 0100ECD018EBE00G\n", "a title id that is not hexadecimal");
    refused("[title]\ntitle_id = 0100ECD018EBE000\nsupported = maybe\n", "an unreadable flag");
    refused(
        "[title]\ntitle_id = 0100ECD018EBE000\ntitle_id = 0100ECD018EBE000\n",
        "a repeated title id");
    refused("title_id = 0100ECD018EBE000\n", "a key outside any section");
    refused("[title\ntitle_id = 0100ECD018EBE000\n", "an unterminated section header");
    refused("[title]\ntitle_id = 0100ECD018EBE000\n[builds]\n1.0.0 = short\n", "a short build id");
    refused(
        "[title]\ntitle_id = 0100ECD018EBE000\n[builds]\n1.0.0 = 0EFFE4AF1DEC3A79\n"
        "1.0.1 = 0EFFE4AF1DEC3A79\n",
        "the same build recorded twice");
    refused(
        "[title]\ntitle_id = 0100ECD018EBE000\n[presentation]\npresent_interval = 300\n",
        "a present interval that does not fit");

    std::string many = "[title]\ntitle_id = 0100ECD018EBE000\n[builds]\n";
    for (std::size_t index = 0; index <= lsfg::profile::max_builds; ++index) {
        many += "1.0.";
        many += static_cast<char>('0' + static_cast<char>(index));
        many += " = 0EFFE4AF1DEC3A7";
        many += static_cast<char>('0' + static_cast<char>(index));
        many += '\n';
    }
    refused(many, "more builds than the profile can hold");

    refused(std::string(lsfg::profile::max_text_size + 1U, ' '), "a profile that is too large");
}

void test_profile_forward_compatibility() {
    constexpr std::string_view text = "[title]\ntitle_id = 0100ECD018EBE000\nfuture_key = 7\n"
                                      "[future_section]\nanything = at all\n";
    lsfg::profile::Profile profile{};
    require(lsfg::succeeded(lsfg::profile::parse(text, profile)), "unknown keys are ignored");
    require(profile.title_id == 0x0100ECD018EBE000ULL, "known keys still parse");
}

void test_targeting() {
    lsfg::profile::Profile profile{};
    require(lsfg::succeeded(lsfg::profile::parse(paper_mario_profile, profile)), "profile parses");

    constexpr std::uint64_t title = 0x0100ECD018EBE000ULL;
    constexpr std::uint64_t build = 0x0EFFE4AF1DEC3A79ULL;

    require(
        lsfg::succeeded(
            lsfg::profile::check(profile, title, build, lsfg::profile::Targeting::strict)),
        "the recorded build is allowed");
    require(
        lsfg::profile::check(profile, title + 1, build, lsfg::profile::Targeting::strict)
            == lsfg::ErrorCode::title_not_allowed,
        "another title is refused");
    require(
        lsfg::profile::check(profile, title, build + 1, lsfg::profile::Targeting::strict)
            == lsfg::ErrorCode::build_not_allowed,
        "an unrecorded build is refused under strict targeting");
    require(
        lsfg::succeeded(
            lsfg::profile::check(profile, title, build + 1, lsfg::profile::Targeting::permissive)),
        "an unrecorded build is allowed under permissive targeting");

    const lsfg::profile::Profile empty{};
    require(
        lsfg::profile::check(empty, 0, 0, lsfg::profile::Targeting::permissive)
            == lsfg::ErrorCode::title_not_allowed,
        "an unparsed profile refuses everything");
}

void test_profile_path() {
    std::array<char, 128> path{};
    require(
        lsfg::profile::path_for("sdmc:/SaltySD/plugins/lsfg-nx/profiles", 0x0100ECD018EBE000ULL,
            path),
        "path fits");
    require(
        std::string_view(path.data())
            == "sdmc:/SaltySD/plugins/lsfg-nx/profiles/0100ECD018EBE000/profile.ini",
        "path text");
    require(!lsfg::profile::path_for(std::string(120, 'x'), 0, path), "an overlong root is refused");
}

// The profiles the package ships are parsed on console and nowhere else, so a
// typo in one is worth a thirty minute play session to discover. Parse them
// here instead, where a wrong build ID costs a test run.
void test_shipped_profiles() {
    std::size_t checked = 0;

    for (const std::filesystem::directory_entry& entry :
        std::filesystem::directory_iterator{LSFG_PROFILES_DIR}) {
        std::ifstream file(entry.path() / "profile.ini", std::ios::binary);
        require(file.is_open(), "a profile directory holds a profile.ini");

        const std::string text{
            std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        lsfg::profile::Profile profile{};
        require(lsfg::succeeded(lsfg::profile::parse(text, profile)), "a shipped profile parses");

        // The plugin finds a profile by the title ID the running process
        // reports, so a record filed under any other directory is unreachable.
        std::array<char, 17> identifier{};
        lsfg::profile::format_id(profile.title_id, identifier);
        require(
            entry.path().filename().string() == identifier.data(),
            "a profile directory names the title the profile records");

        require(profile.name_size != 0, "a shipped profile names its title");
        for (std::size_t index = 0; index < profile.build_count; ++index) {
            require(profile.builds[index].build_id != 0, "a recorded build has a build ID");
        }
        ++checked;
    }

    require(checked != 0, "the shipped profiles were found");
}

lsfg::instrument::TextureRecord make_texture(const std::uint64_t handle,
    const std::uint64_t offset) {
    lsfg::instrument::TextureRecord texture{};
    texture.handle = handle;
    texture.memory_pool = 0xdead'0000ULL;
    texture.pool_address = 0x8000'0000ULL;
    texture.pool_cpu_address = 0x9000'0000ULL;
    texture.texture_address = texture.pool_address + offset;
    texture.storage_offset = offset;
    texture.storage_size = 0x30'0000;
    texture.pool_size = 0x90'0000;
    texture.storage_alignment = 0x1000;
    texture.width = 1280;
    texture.height = 720;
    texture.depth = 1;
    texture.levels = 1;
    texture.samples = 1;
    texture.format = 0x25;
    texture.flags = 0x10;
    texture.target = 1;
    texture.stride = 0;
    texture.pool_flags = 0x12;
    texture.storage_class = 2;
    return texture;
}

void test_texture_log() {
    lsfg::instrument::TextureLog log;
    require(log.find(0) == nullptr, "a null handle is never found");

    log.record(make_texture(0x1000, 0));
    log.record(make_texture(0x2000, 0x30'0000));
    require(log.size() == 2, "two textures recorded");
    require(log.find(0x1000) != nullptr, "first texture found");

    lsfg::instrument::TextureRecord updated = make_texture(0x1000, 0x60'0000);
    log.record(updated);
    require(log.size() == 2, "a rebuilt texture replaces its record rather than adding one");
    require(log.find(0x1000)->storage_offset == 0x60'0000, "the newest record wins");

    for (std::size_t index = 0; index < lsfg::instrument::TextureLog::capacity; ++index) {
        log.record(make_texture(0x1'0000 + index, index));
    }
    require(log.size() == lsfg::instrument::TextureLog::capacity, "the log stays bounded");
    require(log.find(0x1000) == nullptr, "an overwritten texture is gone");
    require(log.observed() == lsfg::instrument::TextureLog::capacity + 3U, "every texture counted");
}

void test_swapchain_map() {
    lsfg::instrument::TextureLog log;
    for (std::size_t index = 0; index < 3; ++index) {
        log.record(make_texture(0x1000 + index, index * 0x30'0000));
    }

    const std::array<std::uint64_t, 3> handles{0x1000, 0x1001, 0x1002};
    lsfg::instrument::SwapchainMap map{};
    require(map.adopt(0xabc0, handles, log), "the swapchain is adopted");
    require(map.complete(), "every texture resolved");
    require(map.uniform, "the textures share a layout");
    require(map.index_of(0x1001) == 1, "index lookup");
    require(map.index_of(0x9999) == lsfg::instrument::SwapchainMap::capacity, "unknown handle");

    const std::array<std::uint64_t, 2> partial{0x1000, 0x7777};
    require(map.adopt(0xabc0, partial, log), "a partly unknown swapchain is still adopted");
    require(!map.complete(), "an unresolved texture leaves the map incomplete");
    require(!map.uniform, "an incomplete map is never uniform");

    lsfg::instrument::TextureRecord odd = make_texture(0x2222, 0);
    odd.height = 1080;
    log.record(odd);
    const std::array<std::uint64_t, 2> mixed{0x1000, 0x2222};
    require(map.adopt(0xabc0, mixed, log), "a mixed swapchain is adopted");
    require(map.complete(), "both textures resolved");
    require(!map.uniform, "differing layouts are not uniform");

    lsfg::instrument::TextureRecord different_storage = make_texture(0x3333, 0x30'0000);
    different_storage.storage_class = 3;
    log.record(different_storage);
    const std::array<std::uint64_t, 2> storage_mismatch{0x1000, 0x3333};
    require(map.adopt(0xabc0, storage_mismatch, log), "storage classes are recorded");
    require(!map.uniform, "differing storage classes are not one import layout");

    const std::array<std::uint64_t, 9> too_many{};
    require(!map.adopt(0xabc0, too_many, log), "an oversized swapchain is refused");
    require(!map.adopt(0xabc0, {}, log), "an empty swapchain is refused");
}

void test_event_trace_is_bounded() {
    lsfg::instrument::EventTrace trace;
    for (std::size_t index = 0; index < lsfg::instrument::EventTrace::capacity + 10U; ++index) {
        trace.push(lsfg::instrument::EventKind::queue_present_texture, index, 1, index);
    }
    require(trace.full(), "the trace fills");
    require(trace.view().size() == lsfg::instrument::EventTrace::capacity, "bounded size");
    require(trace.dropped() == 10, "later events are dropped rather than displacing earlier ones");
    require(trace.view().front().detail == 0, "the first call is kept");
    require(
        trace.view().back().detail == lsfg::instrument::EventTrace::capacity - 1U,
        "the trace holds a prefix of the call order");
}

void test_present_timeline_pacing() {
    lsfg::instrument::PresentTimeline timeline;
    timeline.set_expected_interval_us(33'333, 4000);

    const std::uint64_t frame = lsfg::instrument::us_to_ticks(33'333);
    std::uint64_t tick = frame;
    for (std::int32_t index = 0; index < 60; ++index) {
        timeline.on_acquire_begin(tick);
        timeline.on_acquire_end(tick + lsfg::instrument::us_to_ticks(100), index % 3);
        timeline.on_submit(tick + lsfg::instrument::us_to_ticks(20'000));
        timeline.on_fence(tick + lsfg::instrument::us_to_ticks(20'100));
        timeline.on_present(tick + lsfg::instrument::us_to_ticks(30'000), index % 3);
        tick += frame;
    }

    require(timeline.presents() == 60, "every present counted");
    require(timeline.acquires() == 60, "every acquire counted");
    require(timeline.sequence_faults() == 0, "a well ordered stream has no faults");
    require(timeline.outliers() == 0, "a steady stream has no pacing outliers");
    require(timeline.present_interval().count == 59, "intervals need two presents");

    const std::uint64_t mean = timeline.present_interval().mean();
    require(mean > 33'000 && mean < 33'700, "the mean interval is the frame time");
    require(timeline.acquire_block().max <= 101, "acquire blocking time");
    require(timeline.submit_to_present().mean() == 10'000, "submit to present gap");

    require(timeline.histogram()[8] == 59, "every interval lands in the 32 to 36 ms bucket");
}

void test_present_timeline_faults() {
    lsfg::instrument::PresentTimeline timeline;
    timeline.set_expected_interval_us(33'333, 4000);

    const std::uint64_t frame = lsfg::instrument::us_to_ticks(33'333);
    timeline.on_acquire_begin(0);
    timeline.on_acquire_end(0, 0);
    timeline.on_present(frame, 0);

    // A present of a texture the game does not hold, which is what a
    // mis-scheduled generated frame would look like from the outside.
    timeline.on_present(frame * 2, 1);
    require(timeline.sequence_faults() == 1, "presenting an unacquired texture is a fault");

    // A doubled present rate, which is what pass-through must never produce.
    timeline.on_acquire_begin(frame * 2);
    timeline.on_acquire_end(frame * 2, 2);
    timeline.on_present(frame * 2 + (frame / 2), 2);
    require(timeline.outliers() == 1, "a halved interval is an outlier");

    timeline.reset_intervals();
    require(timeline.outliers() == 0, "a reset drops the derived timings");
    require(timeline.presents() == 3, "a reset keeps the totals");
    require(timeline.present_interval().count == 0, "a reset drops the interval distribution");
}

} // namespace

int main() {
    test_profile_parse();
    test_profile_refusals();
    test_profile_forward_compatibility();
    test_targeting();
    test_profile_path();
    test_shipped_profiles();
    test_texture_log();
    test_swapchain_map();
    test_event_trace_is_bounded();
    test_present_timeline_pacing();
    test_present_timeline_faults();

    std::cout << "instrumentation tests passed\n";
    return EXIT_SUCCESS;
}
