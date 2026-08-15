// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Direct access to the import relocations of the modules the game was built
// with.
namespace lsfg::plugin::imports {

inline constexpr std::size_t max_sites = 16;

enum class RelocationKind : std::uint8_t {
    glob_dat,
    jump_slot,
};

struct Site {
    void** slot{};
    void* relocation{};
    RelocationKind kind{};
};

struct Sites {
    std::array<Site, max_sites> entries{};
    std::size_t count{};
    // Sites past the array.
    std::size_t overflow{};

    std::size_t glob_dat{};
    std::size_t jump_slot{};
    std::size_t other_kind{};

    [[nodiscard]] bool empty() const noexcept {
        return count == 0;
    }
};

[[nodiscard]] Sites find(const char* symbol) noexcept;

[[nodiscard]] bool replace(
    const Sites& sites,
    void* original,
    void* replacement) noexcept;

// Named GLOB_DAT imports reported by the survey below.
inline constexpr std::size_t max_named = 24;

// The relocation kinds across every import of every module the game was built from.
// For picking a trigger SaltyNX can actually install one on.
struct Survey {
    std::size_t modules{};
    std::size_t symbol_relocations{};
    std::size_t glob_dat{};
    std::size_t jump_slot{};
    std::size_t other_kind{};

    // Points into the string table of a module the game keeps mapped.
    std::array<const char*, max_named> names{};
    std::size_t named{};
};

[[nodiscard]] Survey survey() noexcept;

// Writes every symbol import of every module the game was built from to a
// file, one per line, as "<module index> <G|J> <name>".
[[nodiscard]] bool dump(const char* path) noexcept;

// What the game reaches through these sites today.
[[nodiscard]] void* current_target(const Sites& sites) noexcept;

// Writes every site.
void redirect(const Sites& sites, void* target) noexcept;

} // namespace lsfg::plugin::imports
