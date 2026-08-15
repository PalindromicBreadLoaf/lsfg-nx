# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-3.0-or-later

# Verifies the properties the SaltySD loader depends on.

if(NOT EXISTS "${PLUGIN_ELF}")
    message(FATAL_ERROR "plugin ELF not found: ${PLUGIN_ELF}")
endif()

execute_process(
    COMMAND "${READELF}" --dynamic --relocs --wide "${PLUGIN_ELF}"
    OUTPUT_VARIABLE elf_info
    ERROR_QUIET
    RESULT_VARIABLE readelf_result
)

if(NOT readelf_result EQUAL 0)
    message(FATAL_ERROR "could not inspect ${PLUGIN_ELF}")
endif()

if(NOT elf_info MATCHES "SaltySDCore_printf")
    message(
        FATAL_ERROR
        "plugin has no dynamic relocation for SaltySDCore_printf. "
        "The loader cannot patch imports it cannot see, and every SaltySD call "
        "would jump to zero."
    )
endif()

# The loader patches SaltySD imports and nothing else.
execute_process(
    COMMAND "${READELF}" --dyn-syms --wide "${PLUGIN_ELF}"
    OUTPUT_VARIABLE dyn_syms
    ERROR_QUIET
)

string(REGEX MATCHALL "UND ([A-Za-z_][A-Za-z0-9_]*)" undefined_syms "${dyn_syms}")

set(foreign_imports "")
foreach(entry IN LISTS undefined_syms)
    string(REGEX REPLACE "^UND " "" name "${entry}")
    if(NOT name MATCHES "^(SaltySD|__syscall_)")
        list(APPEND foreign_imports "${name}")
    endif()
endforeach()

if(foreign_imports)
    list(REMOVE_DUPLICATES foreign_imports)
    string(REPLACE ";" ", " foreign_list "${foreign_imports}")
    message(
        FATAL_ERROR
        "plugin imports symbols the SaltySD loader will not resolve: ${foreign_list}."
        "Please either link the libnx object that defines them, or define them here."
    )
endif()

execute_process(
    COMMAND "${READELF}" --syms --wide "${PLUGIN_ELF}"
    OUTPUT_VARIABLE all_syms
    ERROR_QUIET
)

if(all_syms MATCHES "newlibSetup")
    message(
        FATAL_ERROR
        "Drop the reference to newlibSetup."
    )
endif()

if(elf_info MATCHES "\\(RELR\\)")
    message(
        FATAL_ERROR
        "plugin ELF carries DT_RELR. Keep -z pack-relative-relocs out of the link."
    )
endif()

execute_process(
    COMMAND "${READELF}" --file-header --wide "${PLUGIN_ELF}"
    OUTPUT_VARIABLE header_info
    ERROR_QUIET
)

if(NOT header_info MATCHES "DYN \\(")
    message(FATAL_ERROR "plugin is not a PIE")
endif()

if(NOT header_info MATCHES "Entry point address: *0x0\n")
    message(FATAL_ERROR "plugin entry point is not at offset zero.")
endif()
