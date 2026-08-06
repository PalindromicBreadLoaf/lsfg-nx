# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-3.0-or-later

include(FetchContent)

set(LSFG_SALTYNX_COMMIT "f3a89e251ef659434ac84781a26986e15fa41fd9" CACHE STRING "SaltyNX revision (tag 1.9.0)")
set(LSFG_LIBULTRAHAND_COMMIT "856ddbddd796fc4a59ad2e0bf939c5963e6f9dd2" CACHE STRING "libultrahand revision (tag v2.5.3)")
set(LSFG_SPIRV_CROSS_COMMIT "ebe2aa0cd80f5eb5cd8a605da604cacf72205f3b" CACHE STRING "SPIRV-Cross revision")
set(LSFG_UAM_COMMIT "c0d76aa9107d67edcd35a1b717ace87ee7939c68" CACHE STRING "uam revision")

set(LSFG_OFFLINE_DEPS_DIR "" CACHE PATH "Directory of pre-cloned dependencies for offline builds")

# SOURCE_SUBDIR names a directory with no CMakeLists.txt for the projects whose
# own build system must not run, which populates the source and stops there.
function(lsfg_declare_dependency name repository commit)
    cmake_parse_arguments(PARSE_ARGV 3 arg "NO_BUILD_SYSTEM" "" "")

    set(extra "")
    if(arg_NO_BUILD_SYSTEM)
        set(extra SOURCE_SUBDIR "lsfg-nx-builds-this-itself")
    endif()

    if(LSFG_OFFLINE_DEPS_DIR)
        if(NOT IS_DIRECTORY "${LSFG_OFFLINE_DEPS_DIR}/${name}")
            message(FATAL_ERROR "LSFG_OFFLINE_DEPS_DIR is set but ${LSFG_OFFLINE_DEPS_DIR}/${name} is missing")
        endif()
        FetchContent_Declare(${name} SOURCE_DIR "${LSFG_OFFLINE_DEPS_DIR}/${name}" ${extra})
    else()
        FetchContent_Declare(
            ${name}
            GIT_REPOSITORY "${repository}"
            GIT_TAG "${commit}"
            GIT_SHALLOW OFF
            GIT_PROGRESS ON
            ${extra}
        )
    endif()
endfunction()

lsfg_declare_dependency(saltynx "https://github.com/masagrator/SaltyNX.git" "${LSFG_SALTYNX_COMMIT}")
lsfg_declare_dependency(libultrahand "https://github.com/ppkantorski/libultrahand.git" "${LSFG_LIBULTRAHAND_COMMIT}")
lsfg_declare_dependency(spirv_cross "https://github.com/KhronosGroup/SPIRV-Cross.git" "${LSFG_SPIRV_CROSS_COMMIT}" NO_BUILD_SYSTEM)
lsfg_declare_dependency(uam "https://github.com/PalindromicBreadLoaf/uam.git" "${LSFG_UAM_COMMIT}" NO_BUILD_SYSTEM)

# Both projects build with Make and produce artifacts this
# repository does not want.
function(lsfg_require_saltynx)
    FetchContent_MakeAvailable(saltynx)
    set(LSFG_SALTYNX_INCLUDE_DIR "${saltynx_SOURCE_DIR}/saltysd_core/source" PARENT_SCOPE)
endfunction()

function(lsfg_require_libultrahand)
    FetchContent_MakeAvailable(libultrahand)
    set(LSFG_LIBULTRAHAND_SOURCE_DIR "${libultrahand_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

# Only the core and the GLSL backend are wanted.
function(lsfg_require_spirv_cross)
    FetchContent_MakeAvailable(spirv_cross)
    set(LSFG_SPIRV_CROSS_SOURCE_DIR "${spirv_cross_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

# uam builds with Meson upstream and produces a command line tool, neither of
# which is useful here.
function(lsfg_require_uam)
    FetchContent_MakeAvailable(uam)
    set(LSFG_UAM_SOURCE_DIR "${uam_SOURCE_DIR}" PARENT_SCOPE)
endfunction()
