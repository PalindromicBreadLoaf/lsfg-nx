// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <lsfg/common/cache_format.hpp>
#include <lsfg/common/error.hpp>
#include <lsfg/common/image_graph.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Reading and writing a prepared cache. Preparation writes it once; the
// runtime reads it and never compiles.
namespace lsfg::cache {

inline constexpr std::string_view manifest_name = "manifest.bin";
inline constexpr std::string_view manifest_temporary_name = "manifest.new";

struct PassInput {
    // dksh_size, dksh_hash, slot_first, and slot_count are filled in by the
    // writer from what follows.
    PassEntry entry;
    std::vector<SlotEntry> slots;
    std::span<const std::uint8_t> dksh;
};

struct Contents {
    ManifestHeader header;
    std::vector<PassInput> passes;
    graph::Graph graph;
};

struct LoadedPass {
    PassEntry entry;
    std::vector<SlotEntry> slots;
    std::vector<std::uint8_t> dksh;
};

struct Loaded {
    ManifestHeader header;
    std::vector<LoadedPass> passes;
    graph::Graph graph;
};

[[nodiscard]] std::string directory_for(std::string_view root, const Digest& key);

[[nodiscard]] std::string module_name(std::uint32_t index);

// Writes every module, then renames the manifest into place.
[[nodiscard]] ErrorCode write(std::string_view directory, Contents& contents);

// Reads a cache back and checks it against itself.
[[nodiscard]] ErrorCode read(std::string_view directory, Loaded& out);

struct Comparison {
    std::uint32_t modules{};
    std::uint32_t differing_modules{};
    std::uint32_t differing_bytes{};
    // Same modules, in the same order, at the same sizes.
    bool same_shape{};

    [[nodiscard]] bool identical() const noexcept {
        return same_shape && differing_modules == 0;
    }
};

// What a second preparation run on the same DLL produced against what the
// cache already holds.
[[nodiscard]] Comparison compare(const Loaded& left, const Contents& right) noexcept;

[[nodiscard]] bool same_modules(const Loaded& left, const Contents& right) noexcept;

} // namespace lsfg::cache
