lsfg_require_saltynx()

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
    source/main.cpp
)

set_target_properties(
    lsfg_plugin
    PROPERTIES
        OUTPUT_NAME lsfg-nx
        SUFFIX .elf
)

target_compile_definitions(lsfg_plugin PRIVATE SWITCH)
target_include_directories(lsfg_plugin PRIVATE "${LSFG_SALTYNX_INCLUDE_DIR}")
target_link_libraries(lsfg_plugin PRIVATE lsfg::version lsfg::protocol lsfg::logging)

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
    COMMAND
        "${CMAKE_COMMAND}"
        "-DPLUGIN_ELF=$<TARGET_FILE:lsfg_plugin>"
        "-DREADELF=${CMAKE_READELF}"
        -P "${PROJECT_SOURCE_DIR}/cmake/CheckPluginElf.cmake"
    VERBATIM
)
