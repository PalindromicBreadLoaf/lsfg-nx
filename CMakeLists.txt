cmake_minimum_required(VERSION 3.22)

project(
    lsfg-nx
    VERSION 0.1.0
    DESCRIPTION "Experimental LSFG frame generation for Nintendo Switch"
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(CTest)
include(LsfgVersion)

option(LSFG_BUILD_HOST_TESTS "Build portable host unit tests" ${BUILD_TESTING})
option(LSFG_BUILD_SWITCH_APPS "Build the Nintendo Switch applications" ${NINTENDO_SWITCH})

if(LSFG_BUILD_SWITCH_APPS AND NOT NINTENDO_SWITCH)
    message(FATAL_ERROR "LSFG_BUILD_SWITCH_APPS requires the devkitPro Switch toolchain")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(lsfg_project_options INTERFACE)
target_compile_options(
    lsfg_project_options
    INTERFACE
        $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wsign-conversion;-ffile-prefix-map=${PROJECT_SOURCE_DIR}=.;-ffile-prefix-map=${PROJECT_BINARY_DIR}=build>
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wsign-conversion;-ffile-prefix-map=${PROJECT_SOURCE_DIR}=.;-ffile-prefix-map=${PROJECT_BINARY_DIR}=build>
)

add_subdirectory(common)

if(LSFG_BUILD_SWITCH_APPS)
    enable_language(ASM)
    include(LsfgDependencies)

    add_subdirectory(apps/prepare)
    add_subdirectory(apps/test-pattern)
    add_subdirectory(runtime/plugin)
    add_subdirectory(overlay/lsfg-overlay)
    add_subdirectory(packaging)
endif()

if(LSFG_BUILD_HOST_TESTS AND NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(tests)
endif()
