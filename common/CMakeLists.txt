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

add_library(lsfg_protocol STATIC protocol/src/protocol.cpp)
add_library(lsfg::protocol ALIAS lsfg_protocol)
target_include_directories(lsfg_protocol PUBLIC protocol/include)
target_link_libraries(lsfg_protocol PUBLIC lsfg::error lsfg_project_options)

add_library(lsfg_cache_format STATIC cache-format/src/cache_format.cpp)
add_library(lsfg::cache_format ALIAS lsfg_cache_format)
target_include_directories(lsfg_cache_format PUBLIC cache-format/include)
target_link_libraries(lsfg_cache_format PUBLIC lsfg::error lsfg_project_options)

configure_file(
    version/version.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/version/include/lsfg/common/version.hpp"
    @ONLY
)

add_library(lsfg_version INTERFACE)
add_library(lsfg::version ALIAS lsfg_version)
target_include_directories(lsfg_version INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/version/include")
target_link_libraries(lsfg_version INTERFACE lsfg_project_options)
