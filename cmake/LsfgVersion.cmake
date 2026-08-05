# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-3.0-or-later

# Resolves the revision stamped into every binary. A tree that cannot be
# identified reports "unknown".

find_package(Git QUIET)

set(LSFG_GIT_REVISION "unknown")
set(LSFG_GIT_DIRTY "true")

if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE git_revision
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE git_revision_result
    )

    if(git_revision_result EQUAL 0 AND git_revision)
        set(LSFG_GIT_REVISION "${git_revision}")

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            OUTPUT_VARIABLE git_status
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )

        if(git_status STREQUAL "")
            set(LSFG_GIT_DIRTY "false")
        endif()
    endif()
endif()

message(STATUS "LSFG-NX revision: ${LSFG_GIT_REVISION} (dirty: ${LSFG_GIT_DIRTY})")
