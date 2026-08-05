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

execute_process(
    COMMAND "${READELF}" --file-header --wide "${PLUGIN_ELF}"
    OUTPUT_VARIABLE header_info
    ERROR_QUIET
)

if(NOT header_info MATCHES "DYN \\(")
    message(FATAL_ERROR "plugin is not a position-independent ELF")
endif()

if(NOT header_info MATCHES "Entry point address: *0x0\n")
    message(FATAL_ERROR "plugin entry point is not at offset zero.")
endif()
