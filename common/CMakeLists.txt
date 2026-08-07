add_library(lsfg_error STATIC error/src/error.cpp)
add_library(lsfg::error ALIAS lsfg_error)
target_include_directories(lsfg_error PUBLIC error/include)
target_link_libraries(lsfg_error PUBLIC lsfg_project_options)

add_library(lsfg_logging STATIC logging/src/ring_log.cpp)
add_library(lsfg::logging ALIAS lsfg_logging)
target_include_directories(lsfg_logging PUBLIC logging/include)
target_link_libraries(lsfg_logging PUBLIC lsfg::error lsfg_project_options)

add_library(lsfg_hash STATIC hash/src/sha256.cpp)
add_library(lsfg::hash ALIAS lsfg_hash)
target_include_directories(lsfg_hash PUBLIC hash/include)
target_link_libraries(lsfg_hash PUBLIC lsfg_project_options)

add_library(lsfg_pe STATIC pe/src/pe_resources.cpp)
add_library(lsfg::pe ALIAS lsfg_pe)
target_include_directories(lsfg_pe PUBLIC pe/include)
target_link_libraries(lsfg_pe PUBLIC lsfg::error lsfg_project_options)

add_library(lsfg_spirv STATIC spirv/src/spirv_module.cpp)
add_library(lsfg::spirv ALIAS lsfg_spirv)
target_include_directories(lsfg_spirv PUBLIC spirv/include)
target_link_libraries(lsfg_spirv PUBLIC lsfg::error lsfg_project_options)

add_library(lsfg_shader_set STATIC shader-set/src/shader_set.cpp)
add_library(lsfg::shader_set ALIAS lsfg_shader_set)
target_include_directories(lsfg_shader_set PUBLIC shader-set/include)
target_link_libraries(lsfg_shader_set PUBLIC lsfg::pe lsfg::spirv lsfg_project_options)

add_library(lsfg_image_graph STATIC image-graph/src/image_graph.cpp)
add_library(lsfg::image_graph ALIAS lsfg_image_graph)
target_include_directories(lsfg_image_graph PUBLIC image-graph/include)
target_link_libraries(lsfg_image_graph PUBLIC lsfg::error lsfg::shader_set lsfg_project_options)

add_library(lsfg_translate STATIC translate/src/translate.cpp)
add_library(lsfg::translate ALIAS lsfg_translate)
target_include_directories(lsfg_translate PUBLIC translate/include)
target_link_libraries(
    lsfg_translate
    PUBLIC lsfg::error lsfg::spirv lsfg_project_options
    PRIVATE lsfg::spirv_cross
)

if(LSFG_BUILD_UAM)
    set(_dksh_compile dksh/src/compile_uam.cpp)
else()
    set(_dksh_compile dksh/src/compile_absent.cpp)
endif()

add_library(lsfg_dksh STATIC dksh/src/dksh.cpp ${_dksh_compile})
add_library(lsfg::dksh ALIAS lsfg_dksh)
target_include_directories(lsfg_dksh PUBLIC dksh/include)
target_link_libraries(lsfg_dksh PUBLIC lsfg::error lsfg_project_options)
if(LSFG_BUILD_UAM)
    target_link_libraries(lsfg_dksh PRIVATE lsfg::uam)
endif()

add_library(lsfg_protocol STATIC protocol/src/protocol.cpp)
add_library(lsfg::protocol ALIAS lsfg_protocol)
target_include_directories(lsfg_protocol PUBLIC protocol/include)
target_link_libraries(lsfg_protocol PUBLIC lsfg::error lsfg_project_options)

add_library(lsfg_cache_format STATIC cache-format/src/cache_format.cpp)
add_library(lsfg::cache_format ALIAS lsfg_cache_format)
target_include_directories(lsfg_cache_format PUBLIC cache-format/include)
target_link_libraries(
    lsfg_cache_format
    PUBLIC lsfg::error lsfg::hash lsfg::image_graph lsfg_project_options
)

add_library(lsfg_cache_store STATIC cache-store/src/cache_store.cpp)
add_library(lsfg::cache_store ALIAS lsfg_cache_store)
target_include_directories(lsfg_cache_store PUBLIC cache-store/include)
target_link_libraries(lsfg_cache_store PUBLIC lsfg::cache_format lsfg_project_options)

configure_file(
    version/version.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/version/include/lsfg/common/version.hpp"
    @ONLY
)

add_library(lsfg_version INTERFACE)
add_library(lsfg::version ALIAS lsfg_version)
target_include_directories(lsfg_version INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/version/include")
target_link_libraries(lsfg_version INTERFACE lsfg_project_options)
