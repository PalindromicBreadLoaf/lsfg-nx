find_path(
    LSFG_MBEDTLS_INCLUDE_DIR mbedtls/ssl.h
    PATHS "${DEVKITPRO}/portlibs/switch/include"
    NO_DEFAULT_PATH
)

if(NOT LSFG_MBEDTLS_INCLUDE_DIR)
    message(
        STATUS
        "Skipping the overlay: switch-mbedtls is not installed. "
        "Install it with '(dkp-)pacman -S switch-curl switch-zlib switch-minizip switch-mbedtls'"
    )
    return()
endif()

lsfg_require_libultrahand()

file(GLOB libultra_sources CONFIGURE_DEPENDS "${LSFG_LIBULTRAHAND_SOURCE_DIR}/libultra/source/*.cpp")

add_library(
    lsfg_overlay_framework STATIC
    ${libultra_sources}
    "${LSFG_LIBULTRAHAND_SOURCE_DIR}/libtesla/source/tesla.cpp"
)

target_include_directories(
    lsfg_overlay_framework
    SYSTEM PUBLIC
        "${LSFG_LIBULTRAHAND_SOURCE_DIR}/libultra/include"
        "${LSFG_LIBULTRAHAND_SOURCE_DIR}/libtesla/include"
        "${LSFG_LIBULTRAHAND_SOURCE_DIR}/common"
        "${DEVKITPRO}/portlibs/switch/include"
)

target_compile_definitions(
    lsfg_overlay_framework
    PUBLIC
        UI_OVERRIDE_PATH="/config/lsfg-nx/"
        APP_VERSION="${PROJECT_VERSION}"
        USING_WIDGET_DIRECTIVE=1
)

target_compile_options(lsfg_overlay_framework PUBLIC -std=c++26 PRIVATE -w)
set_target_properties(lsfg_overlay_framework PROPERTIES CXX_STANDARD_REQUIRED OFF)

target_link_libraries(
    lsfg_overlay_framework
    PUBLIC
        curl minizip z mbedtls mbedx509 mbedcrypto
)

add_executable(lsfg_overlay source/main.cpp)
target_link_libraries(lsfg_overlay PRIVATE lsfg_overlay_framework lsfg::protocol lsfg::version)

nx_create_nro(
    lsfg_overlay_ovl
    TARGET lsfg_overlay
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/lsfg-nx.ovl"
    NOICON
)
