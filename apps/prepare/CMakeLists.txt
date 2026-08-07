add_executable(lsfg_prepare source/main.cpp)
target_link_libraries(
    lsfg_prepare
    PRIVATE
        lsfg::cache_store
        lsfg::hash
        lsfg::image_graph
        lsfg::logging
        lsfg::prepare
        lsfg::version
        lsfg_project_options
)

nx_create_nro(
    lsfg_prepare_nro
    TARGET lsfg_prepare
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/lsfg-prepare.nro"
)

