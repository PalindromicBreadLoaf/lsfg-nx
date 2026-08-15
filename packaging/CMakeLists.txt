set(sd_package_root "${CMAKE_BINARY_DIR}/sd-package")

# Currently support titles
set(plugin_title_ids
    "0100ECD018EBE000" # Paper Mario: The Thousand-Year Door
    "0100E95004038000" # Xenoblade Chronicles 2
)

get_target_property(prepare_nro_file lsfg_prepare_nro DKP_FILE)
get_target_property(test_pattern_nro_file lsfg_test_pattern_nro DKP_FILE)

if(NOT prepare_nro_file OR NOT test_pattern_nro_file)
    message(FATAL_ERROR "Switch NRO targets do not expose their generated files")
endif()

set(package_commands
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${sd_package_root}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${sd_package_root}/switch/lsfg-nx"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${sd_package_root}/SaltySD/plugins/lsfg-nx/profiles"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${sd_package_root}/switch/.overlays"
    COMMAND "${CMAKE_COMMAND}" -E copy
        "${prepare_nro_file}"
        "${sd_package_root}/switch/lsfg-nx/prepare.nro"
    COMMAND "${CMAKE_COMMAND}" -E copy
        "${test_pattern_nro_file}"
        "${sd_package_root}/switch/lsfg-nx/test-pattern.nro"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${PROJECT_SOURCE_DIR}/profiles"
        "${sd_package_root}/SaltySD/plugins/lsfg-nx/profiles"
    COMMAND "${CMAKE_COMMAND}" -E copy
        "${PROJECT_SOURCE_DIR}/packaging/README.txt"
        "${sd_package_root}/README.txt"
)

foreach(title_id IN LISTS plugin_title_ids)
    list(
        APPEND package_commands
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${sd_package_root}/SaltySD/plugins/${title_id}"
        COMMAND "${CMAKE_COMMAND}" -E copy
            "$<TARGET_FILE:lsfg_plugin>"
            "${sd_package_root}/SaltySD/plugins/${title_id}/lsfg-nx.elf"
    )
endforeach()

set(package_dependencies lsfg_prepare_nro lsfg_test_pattern_nro lsfg_plugin)

if(TARGET lsfg_overlay_ovl)
    get_target_property(overlay_ovl_file lsfg_overlay_ovl DKP_FILE)
    list(
        APPEND package_commands
        COMMAND "${CMAKE_COMMAND}" -E copy
            "${overlay_ovl_file}"
            "${sd_package_root}/switch/.overlays/lsfg-nx.ovl"
    )
    list(APPEND package_dependencies lsfg_overlay_ovl)
endif()

add_custom_target(
    sd-package
    ${package_commands}
    DEPENDS ${package_dependencies}
    COMMENT "Staging the LSFG-NX SD card package"
    VERBATIM
)
