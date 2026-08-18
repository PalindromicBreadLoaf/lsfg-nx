lsfg_require_saltynx()

find_program(LSFG_UAM_TOOL NAMES uam REQUIRED)

set(_coexistence_shader "${CMAKE_CURRENT_SOURCE_DIR}/shaders/coexistence.comp")
set(_coexistence_dksh "${CMAKE_CURRENT_BINARY_DIR}/coexistence.dksh")
add_custom_command(
    OUTPUT "${_coexistence_dksh}"
    COMMAND "${LSFG_UAM_TOOL}" -s comp -o "${_coexistence_dksh}" "${_coexistence_shader}"
    DEPENDS "${_coexistence_shader}"
    COMMENT "Compiling deko3d coexistence probe shader"
    VERBATIM
)
add_custom_target(lsfg_coexistence_dksh DEPENDS "${_coexistence_dksh}")
dkp_set_target_file(lsfg_coexistence_dksh "${_coexistence_dksh}")
dkp_add_embedded_binary_library(lsfg_coexistence_shader lsfg_coexistence_dksh)

# The toolchain links every executable with libnx's specs.
string(
    REGEX REPLACE
    "-specs=[^ ]*switch\\.specs"
    "-specs=${CMAKE_CURRENT_SOURCE_DIR}/plugin.specs"
    CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS}"
)

if(NOT CMAKE_EXE_LINKER_FLAGS MATCHES "plugin\\.specs")
    message(FATAL_ERROR "could not replace libnx's specs in the link flags for the plugin")
endif()

add_executable(
    lsfg_plugin
    source/crt0/_crt0.s
    source/crt0/dynamic.cpp
    source/coexistence.cpp
    source/deferred.cpp
    source/deferred_thunk.s
    source/imports.cpp
    source/main.cpp
    source/nv_service.c
    source/nv_session.cpp
    source/nv_session_thunk.s
    source/nvn_hooks.cpp
    source/observations.cpp
    source/report.cpp
)

set_target_properties(
    lsfg_plugin
    PROPERTIES
        OUTPUT_NAME lsfg-nx
        SUFFIX .elf
)

target_compile_definitions(lsfg_plugin PRIVATE SWITCH)
target_include_directories(lsfg_plugin SYSTEM PRIVATE "${LSFG_SALTYNX_INCLUDE_DIR}")
target_link_libraries(
    lsfg_plugin
    PRIVATE
        lsfg::instrument
        lsfg::logging
        lsfg::profile
        lsfg::protocol
        lsfg::version
        lsfg::deko3d_interop
        lsfg_coexistence_shader
        $<IF:$<CONFIG:Debug>,deko3dd,deko3d>
)

# The loader resolves the SaltySD_* imports by walking this ELF's dynamic
# relocations.
target_link_options(
    lsfg_plugin
    PRIVATE
        -Wl,-z,max-page-size=0x1000
        "-Wl,--dynamic-list=${CMAKE_CURRENT_SOURCE_DIR}/dynamic_symbols.txt"
)

add_custom_command(
    TARGET lsfg_plugin
    POST_BUILD
    COMMAND "${CMAKE_STRIP}" --strip-debug "$<TARGET_FILE:lsfg_plugin>"
    COMMAND
        "${CMAKE_COMMAND}"
        "-DPLUGIN_ELF=$<TARGET_FILE:lsfg_plugin>"
        "-DREADELF=${CMAKE_READELF}"
        -P "${PROJECT_SOURCE_DIR}/cmake/CheckPluginElf.cmake"
    VERBATIM
)
