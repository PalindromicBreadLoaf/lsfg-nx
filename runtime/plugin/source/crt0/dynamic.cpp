// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

// Startup fixup for a SaltyNX external plugin.

#include <cstddef>
#include <cstdint>

#include <elf.h>

namespace {

// Offsets into the MOD0 header built by _crt0.s.
constexpr std::size_t mod0_dynamic_offset = 4;
constexpr std::size_t mod0_bss_start_offset = 8;
constexpr std::size_t mod0_bss_end_offset = 12;

constexpr std::uint32_t mod0_magic = 0x30444F4D; // "MOD0"

constexpr std::size_t relr_bitmap_span = 63;

[[nodiscard]] std::int32_t read_offset(const unsigned char* const mod0, const std::size_t at) {
    std::int32_t value = 0;
    __builtin_memcpy(&value, mod0 + at, sizeof value);
    return value;
}

void zero_bss(unsigned char* first, const unsigned char* const last) {
    for (; first != last; ++first) {
        *static_cast<volatile unsigned char*>(first) = 0;
    }
}

void apply_rela(const std::uintptr_t base, const Elf64_Rela* const rela, const std::uint64_t size) {
    for (std::uint64_t i = 0; i < size / sizeof(Elf64_Rela); ++i) {
        if (ELF64_R_TYPE(rela[i].r_info) != R_AARCH64_RELATIVE) {
            continue;
        }
        *reinterpret_cast<std::uint64_t*>(base + rela[i].r_offset)
            = base + static_cast<std::uint64_t>(rela[i].r_addend);
    }
}

void apply_relr(const std::uintptr_t base, const Elf64_Relr* const relr, const std::uint64_t size) {
    std::uint64_t* where = nullptr;

    for (std::uint64_t i = 0; i < size / sizeof(Elf64_Relr); ++i) {
        Elf64_Relr entry = relr[i];

        if ((entry & 1) == 0) {
            where = reinterpret_cast<std::uint64_t*>(base + entry);
            *where++ += base;
            continue;
        }

        std::uint64_t* slot = where;
        for (entry >>= 1; entry != 0; entry >>= 1) {
            if ((entry & 1) != 0) {
                *slot += base;
            }
            ++slot;
        }
        where += relr_bitmap_span;
    }
}

} // namespace

extern "C" void __nx_dynamic(const std::uintptr_t base, const void* const mod0_header) {
    const auto* const mod0 = static_cast<const unsigned char*>(mod0_header);

    std::uint32_t magic = 0;
    __builtin_memcpy(&magic, mod0, sizeof magic);
    if (magic != mod0_magic) {
        return;
    }

    zero_bss(
        const_cast<unsigned char*>(mod0) + read_offset(mod0, mod0_bss_start_offset),
        mod0 + read_offset(mod0, mod0_bss_end_offset));

    const auto* dyn = reinterpret_cast<const Elf64_Dyn*>(
        mod0 + read_offset(mod0, mod0_dynamic_offset));

    const Elf64_Rela* rela = nullptr;
    std::uint64_t rela_size = 0;
    std::uint64_t rela_entry_size = sizeof(Elf64_Rela);

    const Elf64_Relr* relr = nullptr;
    std::uint64_t relr_size = 0;
    std::uint64_t relr_entry_size = sizeof(Elf64_Relr);

    for (; dyn->d_tag != DT_NULL; ++dyn) {
        const std::int64_t tag = dyn->d_tag;
        const std::uint64_t value = dyn->d_un.d_val;

        if (tag == DT_RELA) {
            rela = reinterpret_cast<const Elf64_Rela*>(base + value);
        } else if (tag == DT_RELASZ) {
            rela_size = value;
        } else if (tag == DT_RELAENT) {
            rela_entry_size = value;
        } else if (tag == DT_RELR) {
            relr = reinterpret_cast<const Elf64_Relr*>(base + value);
        } else if (tag == DT_RELRSZ) {
            relr_size = value;
        } else if (tag == DT_RELRENT) {
            relr_entry_size = value;
        }
    }

    if (rela != nullptr && rela_entry_size == sizeof(Elf64_Rela)) {
        apply_rela(base, rela, rela_size);
    }

    if (relr != nullptr && relr_entry_size == sizeof(Elf64_Relr)) {
        apply_relr(base, relr, relr_size);
    }
}
