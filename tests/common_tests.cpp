// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/common/cache_format.hpp>
#include <lsfg/common/error.hpp>
#include <lsfg/common/protocol.hpp>
#include <lsfg/common/ring_log.hpp>

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void test_error_names() {
    require(lsfg::succeeded(lsfg::ErrorCode::ok), "ok must be successful");
    require(!lsfg::succeeded(lsfg::ErrorCode::io_error), "errors must not be successful");
    require(lsfg::error_name(lsfg::ErrorCode::cache_missing) == "cache_missing", "known error name");
    require(lsfg::error_name(static_cast<lsfg::ErrorCode>(0xffff'ffffU)) == "unknown_error", "unknown error name");
}

void test_log_order_and_truncation() {
    lsfg::RingLog log;
    log.push(10, lsfg::LogLevel::info, lsfg::ErrorCode::ok, "first");
    log.push(20, lsfg::LogLevel::warning, lsfg::ErrorCode::timed_out, "second");

    std::array<lsfg::LogEntry, 2> entries{};
    require(log.snapshot(entries) == 2, "snapshot count");
    require(entries[0].sequence == 1 && entries[0].message_view() == "first", "oldest entry first");
    require(entries[1].sequence == 2 && entries[1].message_view() == "second", "newest entry last");

    const std::string long_message(lsfg::log_message_capacity + 17U, 'x');
    log.push(30, lsfg::LogLevel::error, lsfg::ErrorCode::io_error, long_message);
    std::array<lsfg::LogEntry, 1> latest{};
    require(log.snapshot(latest) == 1, "small snapshot count");
    require(latest[0].sequence == 3, "small snapshot retains newest entry");
    require(latest[0].truncated, "oversized message is marked truncated");
    require(latest[0].message_size == lsfg::log_message_capacity, "truncated message size");
}

void test_log_wrap_and_clear() {
    lsfg::RingLog log;
    for (std::size_t index = 0; index < lsfg::ring_log_capacity + 5U; ++index) {
        log.push(index, lsfg::LogLevel::debug, lsfg::ErrorCode::ok, "entry");
    }

    require(log.size() == lsfg::ring_log_capacity, "ring remains bounded");
    std::array<lsfg::LogEntry, lsfg::ring_log_capacity> entries{};
    require(log.snapshot(entries) == entries.size(), "full wrapped snapshot");
    require(entries.front().sequence == 6, "wrapped snapshot starts with oldest retained entry");
    require(entries.back().sequence == lsfg::ring_log_capacity + 5U, "wrapped snapshot ends with newest entry");

    log.clear();
    require(log.size() == 0, "clear resets size");
    require(log.snapshot(entries) == 0, "clear removes entries");
}

void test_protocol_defaults() {
    lsfg::protocol::ControlBlock control{};
    lsfg::protocol::initialize(control);
    require(lsfg::succeeded(lsfg::protocol::validate(control)), "initialized control is valid");

    control.abi_version = lsfg::protocol::abi_version + 1U;
    require(
        lsfg::protocol::validate(control) == lsfg::ErrorCode::protocol_version_mismatch,
        "foreign protocol version is refused");

    lsfg::protocol::initialize(control);
    control.sequence = 3;
    require(
        lsfg::protocol::validate(control) == lsfg::ErrorCode::protocol_message_invalid,
        "odd sequence is a torn write");

    lsfg::protocol::initialize(control);
    control.preset = static_cast<lsfg::protocol::QualityPreset>(9);
    require(
        lsfg::protocol::validate(control) == lsfg::ErrorCode::protocol_message_invalid,
        "unknown preset is refused");
}

void test_protocol_status_message() {
    lsfg::protocol::StatusBlock status{};
    lsfg::protocol::initialize(status);
    require(lsfg::succeeded(lsfg::protocol::validate(status)), "initialized status is valid");
    require(
        lsfg::protocol::state_name(status.state) == "unavailable",
        "a status block with no runtime reports unavailable");

    const std::string oversized(lsfg::protocol::message_capacity + 40U, 'e');
    lsfg::protocol::set_message(status, oversized);
    require(status.message_size == lsfg::protocol::message_capacity, "message is clamped");
    require(lsfg::succeeded(lsfg::protocol::validate(status)), "clamped message stays valid");

    lsfg::protocol::set_message(status, "short");
    require(status.message_view() == "short", "shorter message replaces the longer one");
}

void test_protocol_report_ring() {
    lsfg::protocol::ReportBlock reports{};
    lsfg::protocol::initialize(reports);
    require(lsfg::succeeded(lsfg::protocol::validate(reports)), "initialized report ring is valid");
    require(!lsfg::protocol::push_report(reports, {}), "report ring rejects empty lines");
    lsfg::protocol::consume_report(reports);

    for (std::size_t index = 0; index < lsfg::protocol::report_slot_count; ++index) {
        require(lsfg::protocol::push_report(reports, std::to_string(index)), "report ring accepts capacity");
    }
    require(!lsfg::protocol::push_report(reports, "overflow"), "report ring rejects overflow");
    require(lsfg::protocol::dropped_reports(reports) == 1, "report ring counts overflow");

    for (std::size_t index = 0; index < lsfg::protocol::report_slot_count; ++index) {
        require(
            lsfg::protocol::peek_report(reports) == std::to_string(index),
            "report ring preserves order");
        lsfg::protocol::consume_report(reports);
    }
    require(lsfg::protocol::peek_report(reports).empty(), "consumed report ring is empty");

    const std::string oversized(lsfg::protocol::report_line_capacity + 20U, 'r');
    require(lsfg::protocol::push_report(reports, oversized), "report ring accepts long line");
    require(
        lsfg::protocol::peek_report(reports).size() == lsfg::protocol::report_line_capacity,
        "report ring truncates long line");
}

// A header the runtime would accept, so that each check below can spoil one
// field at a time.
lsfg::cache::ManifestHeader plausible_header() {
    lsfg::cache::ManifestHeader header{};
    lsfg::cache::initialize(header);
    header.dll_size = 4096;
    header.shader_first_resource_id = 303;
    header.shader_block_size = 49;
    header.shader_precision = static_cast<std::uint32_t>(lsfg::cache::Precision::high);
    header.pass_count = 1;
    header.slot_count = 4;
    header.image_count = 8;
    header.dispatch_count = 1;
    header.variant_count = 1;
    header.binding_count = 4;
    header.uniform_buffer_count = 2;
    header.generated_frames = 1;
    header.flow_numerator = 1;
    header.flow_denominator = 1;
    return header;
}

void test_cache_manifest_validation() {
    lsfg::cache::ManifestHeader header{};
    lsfg::cache::initialize(header);
    require(
        lsfg::cache::validate(header) == lsfg::ErrorCode::cache_integrity_failure,
        "an empty manifest describes no shaders and is refused");

    header = plausible_header();
    require(lsfg::succeeded(lsfg::cache::validate(header)), "populated header is valid");

    header.abi_version = lsfg::cache::abi_version + 1U;
    require(
        lsfg::cache::validate(header) == lsfg::ErrorCode::cache_version_mismatch,
        "foreign cache version is refused");

    header = plausible_header();
    header.graph_version = lsfg::graph::graph_version + 1U;
    require(
        lsfg::cache::validate(header) == lsfg::ErrorCode::cache_version_mismatch,
        "a cache describing a differently shaped chain is refused");

    header = plausible_header();
    header.pass_count = lsfg::cache::max_passes + 1U;
    require(
        lsfg::cache::validate(header) == lsfg::ErrorCode::cache_integrity_failure,
        "implausible pass count is refused");

    header = plausible_header();
    header.image_count = 0;
    require(
        lsfg::cache::validate(header) == lsfg::ErrorCode::cache_integrity_failure,
        "a manifest with no images cannot describe the chain");

    header = plausible_header();
    header.flow_numerator = 2;
    require(
        lsfg::cache::validate(header) == lsfg::ErrorCode::cache_integrity_failure,
        "a flow extent above the output extent is refused");
}

void test_cache_configuration_round_trip() {
    lsfg::cache::ManifestHeader header = plausible_header();
    header.options = lsfg::cache::option_performance;
    header.generated_frames = 2;
    header.flow_numerator = 3;
    header.flow_denominator = 4;

    const lsfg::graph::Config config = lsfg::cache::configuration(header);
    require(config.performance && !config.hdr, "the preset survives the manifest");
    require(config.generated_frames == 2, "the generated frame count survives the manifest");
    require(config.flow_numerator == 3 && config.flow_denominator == 4, "the flow scale survives");
}

void test_cache_pass_interface() {
    lsfg::cache::PassEntry entry{};
    entry.dksh_size = 128;
    entry.workgroup_x = 8;
    entry.workgroup_y = 8;
    entry.workgroup_z = 0;
    entry.image_count = 2;
    entry.texture_slot_count = 3;
    entry.storage_image_count = 1;
    entry.sampler_count = 1;
    entry.uniform_buffer_count = 1;
    entry.slot_count = 5;
    require(
        lsfg::cache::validate(entry) == lsfg::ErrorCode::shader_interface_mismatch,
        "a zero workgroup dimension cannot dispatch");

    entry.workgroup_z = 1;
    require(lsfg::succeeded(lsfg::cache::validate(entry)), "a described pass is valid");

    entry.slot_count = 4;
    require(
        lsfg::cache::validate(entry) == lsfg::ErrorCode::shader_interface_mismatch,
        "a slot table that does not cover every resource is refused");

    entry.slot_count = 5;
    entry.texture_slot_count = 1;
    require(
        lsfg::cache::validate(entry) == lsfg::ErrorCode::shader_interface_mismatch,
        "fewer texture slots than images means an image is unreachable");

    entry.texture_slot_count = 3;
    entry.dksh_size = 0;
    require(
        lsfg::cache::validate(entry) == lsfg::ErrorCode::cache_integrity_failure,
        "a pass with no compiled module is refused");
}

void test_crc() {
    require(lsfg::cache::crc32(std::span<const std::uint8_t>{}) == 0, "empty payload CRC");

    const std::array<std::uint8_t, 4> data{1, 2, 3, 4};
    const std::array<std::uint8_t, 4> altered{1, 2, 3, 5};
    require(lsfg::cache::crc32(data) != lsfg::cache::crc32(altered), "a changed byte changes the CRC");
}

} // namespace

int main() {
    test_error_names();
    test_log_order_and_truncation();
    test_log_wrap_and_clear();
    test_protocol_defaults();
    test_protocol_status_message();
    test_protocol_report_ring();
    test_cache_manifest_validation();
    test_cache_configuration_round_trip();
    test_cache_pass_interface();
    test_crc();
    std::cout << "All common checks passed\n";
    return EXIT_SUCCESS;
}
