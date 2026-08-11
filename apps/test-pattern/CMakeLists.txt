add_executable(lsfg_test_pattern source/main.cpp)
target_link_libraries(
    lsfg_test_pattern
    PRIVATE
        lsfg::backend
        lsfg::cache_format
        lsfg::cache_store
        lsfg::image_file
        lsfg::image_graph
        lsfg::logging
        lsfg_project_options
)

nx_create_nro(
    lsfg_test_pattern_nro
    TARGET lsfg_test_pattern
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/lsfg-test-pattern.nro"
)

