// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/common/cache_store.hpp>
#include <lsfg/common/image_graph.hpp>
#include <lsfg/common/shader_set.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct Interface {
    std::uint32_t block_index;
    std::uint32_t images;
    std::uint32_t storage_images;
    std::uint32_t samplers;
    std::uint32_t uniform_buffers;
    std::uint16_t workgroup;
};

// The chain's modules as the extraction stage reports them, which is what the
// manifest has to be able to describe.
const std::vector<Interface>& interfaces() {
    static const std::vector<Interface> table{
        {1, 1, 7, 1, 1, 32},  {2, 5, 1, 2, 1, 16},  {3, 9, 3, 2, 1, 8},   {4, 10, 2, 2, 1, 8},
        {5, 3, 4, 1, 0, 8},   {6, 4, 4, 1, 0, 8},   {7, 4, 4, 1, 0, 8},   {8, 6, 1, 2, 1, 8},
        {9, 3, 4, 1, 0, 8},   {10, 4, 4, 1, 0, 8},  {11, 4, 4, 1, 0, 8},  {12, 6, 1, 2, 1, 8},
        {13, 1, 2, 1, 0, 8},  {14, 2, 2, 1, 0, 8},  {15, 2, 4, 1, 0, 8},  {16, 4, 4, 1, 0, 8},
        {17, 2, 2, 1, 0, 8},  {18, 2, 2, 1, 0, 8},  {19, 2, 2, 1, 0, 8},  {20, 3, 1, 2, 1, 8},
        {21, 12, 2, 1, 0, 8}, {22, 2, 2, 1, 0, 8},  {23, 2, 2, 1, 0, 8},  {24, 2, 2, 1, 0, 8},
        {25, 2, 6, 1, 1, 32},
    };
    return table;
}

// A cache with the real chain's shape but stand-in module bytes, since the
// store never looks inside a module.
lsfg::cache::Contents build_contents(std::vector<std::vector<std::uint8_t>>& storage) {
    lsfg::cache::Contents contents;
    lsfg::cache::initialize(contents.header);
    contents.header.dll_size = 7'521'280;
    contents.header.shader_first_resource_id = 303;
    contents.header.shader_block_size = 49;
    contents.header.shader_precision = static_cast<std::uint32_t>(lsfg::cache::Precision::high);

    require(
        lsfg::succeeded(lsfg::graph::build(lsfg::graph::Config{}, contents.graph)),
        "the graph builds");

    storage.clear();
    storage.reserve(interfaces().size());

    for (const Interface& module : interfaces()) {
        storage.emplace_back(64U + module.block_index, static_cast<std::uint8_t>(module.block_index));

        lsfg::cache::PassInput pass;
        pass.entry.resource_id = 352U + module.block_index;
        pass.entry.block_index = module.block_index;
        pass.entry.workgroup_x = module.workgroup;
        pass.entry.workgroup_y = module.workgroup;
        pass.entry.workgroup_z = 1;
        pass.entry.image_count = module.images;
        pass.entry.storage_image_count = module.storage_images;
        pass.entry.sampler_count = module.samplers;
        pass.entry.uniform_buffer_count = module.uniform_buffers;

        // Every module in this chain samples nothing from one of its images, so
        // one image is reached through a second slot with an introduced sampler.
        pass.entry.texture_slot_count = module.images + 1U;

        for (std::uint32_t index = 0; index < module.uniform_buffers; ++index) {
            pass.slots.push_back(lsfg::cache::SlotEntry{
                .kind = static_cast<std::uint8_t>(lsfg::cache::SlotKind::uniform_buffer),
                .slot = static_cast<std::uint8_t>(index),
                .ordinal = static_cast<std::uint8_t>(index)});
        }
        for (std::uint32_t index = 0; index < module.images; ++index) {
            pass.slots.push_back(lsfg::cache::SlotEntry{
                .kind = static_cast<std::uint8_t>(lsfg::cache::SlotKind::texture),
                .slot = static_cast<std::uint8_t>(index),
                .ordinal = static_cast<std::uint8_t>(index)});
        }
        pass.slots.push_back(lsfg::cache::SlotEntry{
            .kind = static_cast<std::uint8_t>(lsfg::cache::SlotKind::texture),
            .slot = static_cast<std::uint8_t>(module.images),
            .ordinal = 0,
            .sampler_ordinal = lsfg::cache::introduced_sampler});
        for (std::uint32_t index = 0; index < module.storage_images; ++index) {
            pass.slots.push_back(lsfg::cache::SlotEntry{
                .kind = static_cast<std::uint8_t>(lsfg::cache::SlotKind::storage_image),
                .slot = static_cast<std::uint8_t>(index),
                .ordinal = static_cast<std::uint8_t>(index)});
        }

        pass.entry.slot_count = static_cast<std::uint32_t>(pass.slots.size());
        contents.passes.push_back(std::move(pass));
    }

    for (std::size_t index = 0; index < contents.passes.size(); ++index) {
        contents.passes[index].dksh = storage[index];
    }
    return contents;
}

std::string scratch_directory() {
    const std::filesystem::path path
        = std::filesystem::temp_directory_path() / "lsfg-nx-cache-tests";
    std::filesystem::remove_all(path);
    return path.string();
}

void test_round_trip() {
    const std::string root = scratch_directory();
    const lsfg::Digest key{};
    const std::string directory = lsfg::cache::directory_for(root, key);

    std::vector<std::vector<std::uint8_t>> storage;
    lsfg::cache::Contents contents = build_contents(storage);
    require(lsfg::succeeded(lsfg::cache::write(directory, contents)), "the cache is written");

    lsfg::cache::Loaded loaded;
    require(lsfg::succeeded(lsfg::cache::read(directory, loaded)), "the cache is read back");

    require(loaded.passes.size() == contents.passes.size(), "every pass comes back");
    require(loaded.graph.dispatches.size() == 100, "the graph comes back whole");
    require(loaded.graph.config.generated_frames == 1, "the configuration comes back");
    require(lsfg::cache::same_modules(loaded, contents), "the modules come back byte for byte");

    // What a second preparation run on the same DLL has to conclude.
    std::vector<std::vector<std::uint8_t>> second_storage;
    const lsfg::cache::Contents second = build_contents(second_storage);
    require(lsfg::cache::same_modules(loaded, second), "recompiling the same DLL matches the cache");

    std::vector<std::vector<std::uint8_t>> different_storage;
    lsfg::cache::Contents different = build_contents(different_storage);
    different_storage[3].push_back(0xFF);
    different.passes[3].dksh = different_storage[3];
    require(!lsfg::cache::same_modules(loaded, different), "a module that changed is noticed");

    std::filesystem::remove_all(root);
}

void test_a_missing_cache_is_not_an_error_to_guess_at() {
    const std::string root = scratch_directory();
    lsfg::cache::Loaded loaded;
    require(
        lsfg::cache::read(lsfg::cache::directory_for(root, lsfg::Digest{}), loaded)
            == lsfg::ErrorCode::cache_missing,
        "a cache that was never written reports itself missing");
}

void test_tampering_is_caught() {
    const std::string root = scratch_directory();
    const std::string directory = lsfg::cache::directory_for(root, lsfg::Digest{});

    std::vector<std::vector<std::uint8_t>> storage;
    lsfg::cache::Contents contents = build_contents(storage);
    require(lsfg::succeeded(lsfg::cache::write(directory, contents)), "the cache is written");

    const std::filesystem::path module
        = std::filesystem::path(directory) / lsfg::cache::module_name(2);
    {
        std::ofstream out(module, std::ios::binary | std::ios::app);
        out << 'x';
    }

    lsfg::cache::Loaded loaded;
    require(
        lsfg::cache::read(directory, loaded) == lsfg::ErrorCode::cache_integrity_failure,
        "a module that does not match its recorded hash is refused");

    std::filesystem::remove(module);
    require(
        lsfg::cache::read(directory, loaded) == lsfg::ErrorCode::cache_missing,
        "a module the manifest names but the directory lacks is refused");

    std::filesystem::remove_all(root);
}

void test_an_unfinished_cache_has_no_manifest() {
    const std::string root = scratch_directory();
    const std::string directory = lsfg::cache::directory_for(root, lsfg::Digest{});

    std::vector<std::vector<std::uint8_t>> storage;
    lsfg::cache::Contents contents = build_contents(storage);
    require(lsfg::succeeded(lsfg::cache::write(directory, contents)), "the cache is written");

    // Interrupting a rewrite leaves modules in place but no manifest, and the
    // manifest is what makes a directory a cache.
    std::filesystem::remove(std::filesystem::path(directory) / lsfg::cache::manifest_name);

    lsfg::cache::Loaded loaded;
    require(
        lsfg::cache::read(directory, loaded) == lsfg::ErrorCode::cache_missing,
        "modules without a manifest are not a cache");

    require(lsfg::succeeded(lsfg::cache::write(directory, contents)), "the cache is written again");
    require(lsfg::succeeded(lsfg::cache::read(directory, loaded)), "a rewritten cache reads back");

    std::filesystem::remove_all(root);
}

void test_a_graph_the_modules_cannot_run_is_refused() {
    const std::string root = scratch_directory();
    const std::string directory = lsfg::cache::directory_for(root, lsfg::Digest{});

    std::vector<std::vector<std::uint8_t>> storage;
    lsfg::cache::Contents contents = build_contents(storage);

    // The pass that writes the pyramid covers 64 pixels per workgroup. A module
    // with a smaller workgroup would leave most of the image untouched.
    contents.passes.front().entry.workgroup_x = 12;
    contents.passes.front().entry.workgroup_y = 12;
    require(
        lsfg::cache::write(directory, contents) == lsfg::ErrorCode::shader_interface_mismatch,
        "a workgroup that does not tile the dispatch is refused");

    std::vector<std::vector<std::uint8_t>> other_storage;
    lsfg::cache::Contents other = build_contents(other_storage);
    other.passes.back().entry.storage_image_count = 5;
    require(
        lsfg::cache::write(directory, other) == lsfg::ErrorCode::shader_interface_mismatch,
        "a module with fewer outputs than the chain binds is refused");

    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    test_round_trip();
    test_a_missing_cache_is_not_an_error_to_guess_at();
    test_tampering_is_caught();
    test_an_unfinished_cache_has_no_manifest();
    test_a_graph_the_modules_cannot_run_is_refused();

    std::cout << "cache store tests passed\n";
    return EXIT_SUCCESS;
}
