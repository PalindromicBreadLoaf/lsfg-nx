# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-3.0-or-later

include(FetchContent)

set(LSFG_SALTYNX_COMMIT "f3a89e251ef659434ac84781a26986e15fa41fd9" CACHE STRING "SaltyNX revision (tag 1.9.0)")
set(LSFG_LIBULTRAHAND_COMMIT "856ddbddd796fc4a59ad2e0bf939c5963e6f9dd2" CACHE STRING "libultrahand revision (tag v2.5.3)")

set(LSFG_OFFLINE_DEPS_DIR "" CACHE PATH "Directory of pre-cloned dependencies for offline builds")

function(lsfg_declare_dependency name repository commit)
    if(LSFG_OFFLINE_DEPS_DIR)
        if(NOT IS_DIRECTORY "${LSFG_OFFLINE_DEPS_DIR}/${name}")
            message(FATAL_ERROR "LSFG_OFFLINE_DEPS_DIR is set but ${LSFG_OFFLINE_DEPS_DIR}/${name} is missing")
        endif()
        FetchContent_Declare(${name} SOURCE_DIR "${LSFG_OFFLINE_DEPS_DIR}/${name}")
    else()
        FetchContent_Declare(
            ${name}
            GIT_REPOSITORY "${repository}"
            GIT_TAG "${commit}"
            GIT_SHALLOW OFF
            GIT_PROGRESS ON
        )
    endif()
endfunction()

lsfg_declare_dependency(saltynx "https://github.com/masagrator/SaltyNX.git" "${LSFG_SALTYNX_COMMIT}")
lsfg_declare_dependency(libultrahand "https://github.com/ppkantorski/libultrahand.git" "${LSFG_LIBULTRAHAND_COMMIT}")

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
