# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-3.0-or-later

# Only what turns a SPIR-V module into GLSL: the core, the parser, and the GLSL
# backend.

include(LsfgDependencies)

lsfg_require_spirv_cross()

set(_spirv_cross_sources
    spirv_cfg.cpp
    spirv_cross.cpp
    spirv_cross_parsed_ir.cpp
    spirv_glsl.cpp
    spirv_parser.cpp
)
list(TRANSFORM _spirv_cross_sources PREPEND "${LSFG_SPIRV_CROSS_SOURCE_DIR}/")

add_library(lsfg_spirv_cross STATIC ${_spirv_cross_sources})
add_library(lsfg::spirv_cross ALIAS lsfg_spirv_cross)

target_include_directories(lsfg_spirv_cross SYSTEM PUBLIC "${LSFG_SPIRV_CROSS_SOURCE_DIR}")

# Exceptions are left on so a malformed module raises spirv_cross::CompilerError.
set_target_properties(
    lsfg_spirv_cross
    PROPERTIES
        CXX_STANDARD 17
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
)
target_compile_options(lsfg_spirv_cross PRIVATE -w)
