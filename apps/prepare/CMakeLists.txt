add_executable(lsfg_prepare source/main.cpp)
target_link_libraries(
    lsfg_prepare
    PRIVATE
        lsfg::cache_format
        lsfg::hash
        lsfg::logging
        lsfg::pe
        lsfg::shader_set
        lsfg::spirv
        lsfg_project_options
)

nx_create_nro(
    lsfg_prepare_nro
    TARGET lsfg_prepare
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/lsfg-prepare.nro"
)

