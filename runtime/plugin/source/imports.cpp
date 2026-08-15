// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imports.hpp"

#include <switch.h>

#include <saltysd_core.h>
#include <saltysd_ipc.h>

#include <elf.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>

extern "C" {

__attribute__((weak)) void SaltySDCore_getDataForUpdate(
    std::uint32_t* module_count,
    std::int32_t* replaced_count,
    void** replaced_symbols,
    void*** modules);

} // extern "C"

namespace lsfg::plugin::imports {
namespace {

// The two headers every module a game is built from starts with.
struct NsoHeader {
    std::uint32_t start;
    std::uint32_t mod;
};

struct Mod0Header {
    std::uint32_t magic;
    std::uint32_t dynamic;
};

struct Dynamic {
    const Elf64_Sym* symtab{};
    const char* strtab{};
    const Elf64_Rela* rela{};
    std::size_t rela_count{};
    std::size_t symbol_count{};
};

[[nodiscard]] bool read_dynamic(const std::uint8_t* const base, Dynamic& out) noexcept {
    const auto* const nso = reinterpret_cast<const NsoHeader*>(base);
    const auto* const mod0 = reinterpret_cast<const Mod0Header*>(base + nso->mod);
    const auto* entry
        = reinterpret_cast<const Elf64_Dyn*>(base + nso->mod + mod0->dynamic);

    for (; entry->d_tag != DT_NULL; ++entry) {
        switch (entry->d_tag) {
        case DT_SYMTAB:
            out.symtab = reinterpret_cast<const Elf64_Sym*>(base + entry->d_un.d_ptr);
            break;
        case DT_STRTAB:
            out.strtab = reinterpret_cast<const char*>(base + entry->d_un.d_ptr);
            break;
        case DT_RELA:
            out.rela = reinterpret_cast<const Elf64_Rela*>(base + entry->d_un.d_ptr);
            break;
        case DT_RELASZ:
        case DT_PLTRELSZ:
            out.rela_count += entry->d_un.d_val / sizeof(Elf64_Rela);
            break;
        default:
            break;
        }
    }

    if (out.symtab == nullptr || out.strtab == nullptr || out.rela == nullptr) {
        return false;
    }

    const auto* const symbol_end = reinterpret_cast<const std::uint8_t*>(out.strtab);
    const auto* const symbol_begin = reinterpret_cast<const std::uint8_t*>(out.symtab);
    if (symbol_end <= symbol_begin) {
        return false;
    }
    out.symbol_count = static_cast<std::size_t>(symbol_end - symbol_begin) / sizeof(Elf64_Sym);
    return true;
}

void collect(void* const module, const char* const symbol, Sites& out) noexcept {
    auto* const base = static_cast<std::uint8_t*>(module);

    Dynamic dynamic{};
    if (!read_dynamic(base, dynamic)) {
        return;
    }

    for (std::size_t index = 0; index < dynamic.rela_count; ++index) {
        const Elf64_Rela& relocation = dynamic.rela[index];
        if (ELF64_R_TYPE(relocation.r_info) == R_AARCH64_RELATIVE) {
            continue;
        }

        const auto symbol_index = static_cast<std::size_t>(ELF64_R_SYM(relocation.r_info));
        if (symbol_index >= dynamic.symbol_count) {
            continue;
        }

        const char* const name = dynamic.strtab + dynamic.symtab[symbol_index].st_name;
        if (std::strcmp(name, symbol) != 0) {
            continue;
        }

        RelocationKind kind{};
        switch (ELF64_R_TYPE(relocation.r_info)) {
        case R_AARCH64_GLOB_DAT:
            ++out.glob_dat;
            kind = RelocationKind::glob_dat;
            break;
        case R_AARCH64_JUMP_SLOT:
            ++out.jump_slot;
            kind = RelocationKind::jump_slot;
            break;
        default:
            ++out.other_kind;
            continue;
        }

        if (out.count == max_sites) {
            ++out.overflow;
            continue;
        }
        out.entries[out.count] = Site{
            reinterpret_cast<void**>(base + relocation.r_offset),
            const_cast<Elf64_Rela*>(&relocation),
            kind};
        ++out.count;
    }
}

void tally(void* const module, Survey& out) noexcept {
    auto* const base = static_cast<std::uint8_t*>(module);

    Dynamic dynamic{};
    if (!read_dynamic(base, dynamic)) {
        return;
    }

    ++out.modules;

    for (std::size_t index = 0; index < dynamic.rela_count; ++index) {
        const Elf64_Rela& relocation = dynamic.rela[index];
        if (ELF64_R_TYPE(relocation.r_info) == R_AARCH64_RELATIVE) {
            continue;
        }

        const auto symbol_index = static_cast<std::size_t>(ELF64_R_SYM(relocation.r_info));
        if (symbol_index >= dynamic.symbol_count) {
            continue;
        }

        ++out.symbol_relocations;

        switch (ELF64_R_TYPE(relocation.r_info)) {
        case R_AARCH64_GLOB_DAT:
            ++out.glob_dat;
            if (out.named < max_named) {
                out.names[out.named] = dynamic.strtab + dynamic.symtab[symbol_index].st_name;
                ++out.named;
            }
            break;
        case R_AARCH64_JUMP_SLOT:
            ++out.jump_slot;
            break;
        default:
            ++out.other_kind;
            break;
        }
    }
}

// Buffered output for the import dump.
class Writer {
public:
    explicit Writer(FILE* const file) noexcept : m_file{file} {}

    void put(const char* const text) noexcept {
        for (const char* cursor = text; *cursor != '\0'; ++cursor) {
            if (m_used == m_buffer.size()) {
                flush();
            }
            m_buffer[m_used] = *cursor;
            ++m_used;
        }
    }

    void put_number(std::size_t value) noexcept {
        std::array<char, 24> digits{};
        std::size_t length = 0;
        do {
            digits[length] = static_cast<char>('0' + (value % 10));
            ++length;
            value /= 10;
        } while (value != 0);

        while (length != 0) {
            --length;
            const char text[2] = {digits[length], '\0'};
            put(&text[0]);
        }
    }

    void flush() noexcept {
        if (m_used == 0) {
            return;
        }
        if (SaltySDCore_fwrite(m_buffer.data(), 1, m_used, m_file) != m_used) {
            m_failed = true;
        }
        m_used = 0;
    }

    [[nodiscard]] bool failed() const noexcept {
        return m_failed;
    }

private:
    FILE* m_file;
    std::array<char, 8192> m_buffer{};
    std::size_t m_used{};
    bool m_failed{};
};

void write_module(void* const module, const std::size_t index, Writer& out) noexcept {
    auto* const base = static_cast<std::uint8_t*>(module);

    Dynamic dynamic{};
    if (!read_dynamic(base, dynamic)) {
        return;
    }

    for (std::size_t entry = 0; entry < dynamic.rela_count; ++entry) {
        const Elf64_Rela& relocation = dynamic.rela[entry];

        const char* kind = nullptr;
        switch (ELF64_R_TYPE(relocation.r_info)) {
        case R_AARCH64_GLOB_DAT:
            kind = " G ";
            break;
        case R_AARCH64_JUMP_SLOT:
            kind = " J ";
            break;
        default:
            continue;
        }

        const auto symbol_index = static_cast<std::size_t>(ELF64_R_SYM(relocation.r_info));
        if (symbol_index >= dynamic.symbol_count) {
            continue;
        }

        out.put_number(index);
        out.put(kind);
        out.put(dynamic.strtab + dynamic.symtab[symbol_index].st_name);
        out.put("\n");
    }
}

[[nodiscard]] bool module_list(std::uint32_t& count, void**& modules) noexcept {
    if (SaltySDCore_getDataForUpdate == nullptr) {
        return false;
    }

    std::int32_t replaced_count = 0;
    void* replaced = nullptr;
    count = 0;
    modules = nullptr;
    SaltySDCore_getDataForUpdate(&count, &replaced_count, &replaced, &modules);

    return modules != nullptr;
}

} // namespace

Sites find(const char* const symbol) noexcept {
    Sites sites{};

    std::uint32_t module_count = 0;
    void** modules = nullptr;
    if (!module_list(module_count, modules)) {
        return sites;
    }

    for (std::uint32_t index = 0; index < module_count; ++index) {
        collect(modules[index], symbol, sites);
    }
    return sites;
}

bool replace(
    const Sites& sites,
    void* const original,
    void* const replacement) noexcept {
    if (sites.empty() || sites.overflow != 0 || original == nullptr || replacement == nullptr) {
        return false;
    }

    std::array<Elf64_Rela, max_sites> originals{};
    for (std::size_t index = 0; index < sites.count; ++index) {
        originals[index] = *static_cast<const Elf64_Rela*>(sites.entries[index].relocation);
    }

    const std::uint64_t delta
        = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(replacement))
        - static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(original));

    std::size_t written = 0;
    for (; written < sites.count; ++written) {
        Elf64_Rela updated = originals[written];
        const std::uint64_t addend = std::bit_cast<std::uint64_t>(updated.r_addend) + delta;
        updated.r_addend = std::bit_cast<Elf64_Sxword>(addend);

        const auto destination
            = reinterpret_cast<std::uintptr_t>(sites.entries[written].relocation);
        const auto source = reinterpret_cast<std::uintptr_t>(&updated);
        if (R_FAILED(SaltySD_Memcpy(destination, source, sizeof updated))) {
            break;
        }
    }

    if (written == sites.count) {
        return true;
    }

    for (std::size_t index = 0; index < written; ++index) {
        const auto destination
            = reinterpret_cast<std::uintptr_t>(sites.entries[index].relocation);
        const auto source = reinterpret_cast<std::uintptr_t>(&originals[index]);
        SaltySD_Memcpy(destination, source, sizeof originals[index]);
    }
    return false;
}

Survey survey() noexcept {
    Survey result{};

    std::uint32_t module_count = 0;
    void** modules = nullptr;
    if (!module_list(module_count, modules)) {
        return result;
    }

    for (std::uint32_t index = 0; index < module_count; ++index) {
        tally(modules[index], result);
    }
    return result;
}

bool dump(const char* const path) noexcept {
    std::uint32_t module_count = 0;
    void** modules = nullptr;
    if (!module_list(module_count, modules)) {
        return false;
    }

    FILE* const file = SaltySDCore_fopen(path, "wb");
    if (file == nullptr) {
        return false;
    }

    Writer writer{file};
    for (std::uint32_t index = 0; index < module_count; ++index) {
        write_module(modules[index], index, writer);
    }
    writer.flush();
    SaltySDCore_fclose(file);

    return !writer.failed();
}

void* current_target(const Sites& sites) noexcept {
    for (std::size_t index = 0; index < sites.count; ++index) {
        if (void* const target = *sites.entries[index].slot) {
            return target;
        }
    }
    return nullptr;
}

void redirect(const Sites& sites, void* const target) noexcept {
    for (std::size_t index = 0; index < sites.count; ++index) {
        *sites.entries[index].slot = target;
    }
}

} // namespace lsfg::plugin::imports
