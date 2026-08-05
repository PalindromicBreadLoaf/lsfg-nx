// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/common/cache_format.hpp>
#include <lsfg/common/error.hpp>
#include <lsfg/common/protocol.hpp>
#include <lsfg/common/ring_log.hpp>

#include <array>
#include <bit>
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

void test_cache_manifest_validation() {
    lsfg::cache::ManifestHeader header{};
    lsfg::cache::initialize(header);
    require(
        lsfg::cache::validate(header) == lsfg::ErrorCode::cache_integrity_failure,
        "an empty manifest describes no shaders and is refused");

    header.dll_size = 4096;
    header.pass_count = 1;
    require(lsfg::succeeded(lsfg::cache::validate(header)), "populated header is valid");

    header.abi_version = lsfg::cache::abi_version + 1U;
    require(
        lsfg::cache::validate(header) == lsfg::ErrorCode::cache_version_mismatch,
        "foreign cache version is refused");

    header.abi_version = lsfg::cache::abi_version;
    header.pass_count = lsfg::cache::max_passes + 1U;
    require(
        lsfg::cache::validate(header) == lsfg::ErrorCode::cache_integrity_failure,
        "implausible pass count is refused");
}

void test_cache_payload_crc() {
    lsfg::cache::ManifestHeader header{};
    lsfg::cache::initialize(header);
    header.dll_size = 4096;
    header.pass_count = 2;

    std::vector<lsfg::cache::PassEntry> passes(2);
    for (lsfg::cache::PassEntry& entry : passes) {
        entry.dksh_size = 512;
        entry.workgroup_x = 8;
        entry.workgroup_y = 8;
        entry.workgroup_z = 1;
        entry.width_numerator = 1;
        entry.width_denominator = 1;
        entry.height_numerator = 1;
        entry.height_denominator = 1;
    }

    const std::span<const std::uint8_t> payload{
        std::bit_cast<const std::uint8_t*>(passes.data()),
        passes.size() * sizeof(lsfg::cache::PassEntry)};
    header.payload_crc32 = lsfg::cache::crc32(payload);
    require(lsfg::succeeded(lsfg::cache::validate(header, passes)), "matching CRC accepts the manifest");

    passes[1].dksh_size = 513;
    require(
        lsfg::cache::validate(header, passes) == lsfg::ErrorCode::cache_integrity_failure,
        "a modified pass fails the CRC");

    passes.pop_back();
    require(
        lsfg::cache::validate(header, passes) == lsfg::ErrorCode::cache_integrity_failure,
        "pass count must match the header");

    require(lsfg::cache::crc32(std::span<const std::uint8_t>{}) == 0, "empty payload CRC");
}

void test_cache_pass_interface() {
    lsfg::cache::PassEntry entry{};
    entry.dksh_size = 128;
    entry.workgroup_x = 8;
    entry.workgroup_y = 8;
    entry.workgroup_z = 0;
    entry.width_numerator = 1;
    entry.width_denominator = 1;
    entry.height_numerator = 1;
    entry.height_denominator = 1;
    require(
        lsfg::cache::validate(entry) == lsfg::ErrorCode::shader_interface_mismatch,
        "a zero workgroup dimension cannot dispatch");

    entry.workgroup_z = 1;
    entry.height_denominator = 0;
    require(
        lsfg::cache::validate(entry) == lsfg::ErrorCode::shader_interface_mismatch,
        "a zero denominator has no representable extent");
}

} // namespace

int main() {
    test_error_names();
    test_log_order_and_truncation();
    test_log_wrap_and_clear();
    test_protocol_defaults();
    test_protocol_status_message();
    test_cache_manifest_validation();
    test_cache_payload_crc();
    test_cache_pass_interface();
    std::cout << "All common checks passed\n";
    return EXIT_SUCCESS;
}
