# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-3.0-or-later

lsfg_require_deko3d()

set(_deko3d_patch "${PROJECT_SOURCE_DIR}/cmake/patches/deko3d-borrowed-nvmap.patch")
file(SHA256 "${_deko3d_patch}" _deko3d_patch_hash)
set(_deko3d_fork "${PROJECT_BINARY_DIR}/_deps/deko3d-lsfg-${_deko3d_patch_hash}-v4")
set(_deko3d_stamp "${_deko3d_fork}/.patched")

if(NOT EXISTS "${_deko3d_stamp}")
    file(MAKE_DIRECTORY "${_deko3d_fork}")
    file(COPY "${LSFG_DEKO3D_SOURCE_DIR}/include" DESTINATION "${_deko3d_fork}")
    file(COPY "${LSFG_DEKO3D_SOURCE_DIR}/source" DESTINATION "${_deko3d_fork}")

    file(READ "${_deko3d_patch}" _deko3d_patch_contents)
    string(
        REGEX REPLACE
        "^# Copyright[^\n]*\n# SPDX-License-Identifier:[^\n]*\n"
        ""
        _deko3d_patch_contents
        "${_deko3d_patch_contents}"
    )
    set(_deko3d_build_patch "${_deko3d_fork}/interop.patch")
    file(WRITE "${_deko3d_build_patch}" "${_deko3d_patch_contents}")

    find_package(Git REQUIRED QUIET)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" init --quiet
        WORKING_DIRECTORY "${_deko3d_fork}"
        RESULT_VARIABLE _deko3d_init_result
        ERROR_VARIABLE _deko3d_init_error
    )
    if(NOT _deko3d_init_result EQUAL 0)
        message(FATAL_ERROR "could not initialize the deko3d patch workspace: ${_deko3d_init_error}")
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply "${_deko3d_build_patch}"
        WORKING_DIRECTORY "${_deko3d_fork}"
        RESULT_VARIABLE _deko3d_patch_result
        ERROR_VARIABLE _deko3d_patch_error
    )
    if(NOT _deko3d_patch_result EQUAL 0)
        message(FATAL_ERROR "could not apply the deko3d interop patch: ${_deko3d_patch_error}")
    endif()
    file(TOUCH "${_deko3d_stamp}")
endif()

add_library(lsfg_deko3d_interop STATIC "${_deko3d_fork}/source/dk_memblock.cpp")
add_library(lsfg::deko3d_interop ALIAS lsfg_deko3d_interop)

target_include_directories(
    lsfg_deko3d_interop
    SYSTEM
    BEFORE
    PUBLIC "${_deko3d_fork}/include"
    PRIVATE "${_deko3d_fork}/source"
)
target_compile_definitions(
    lsfg_deko3d_interop
    PRIVATE
        __DK_INTERNAL__
        $<$<CONFIG:Debug>:DEBUG=1>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG=1>
)
target_compile_options(lsfg_deko3d_interop PRIVATE -fno-rtti -fno-exceptions)
