add_executable(lsfg_common_tests common_tests.cpp)
target_link_libraries(
    lsfg_common_tests
    PRIVATE
        lsfg::logging
        lsfg::protocol
        lsfg::cache_format
        lsfg_project_options
)
add_test(NAME lsfg_common_tests COMMAND lsfg_common_tests)

add_executable(lsfg_extraction_tests extraction_tests.cpp)
target_link_libraries(
    lsfg_extraction_tests
    PRIVATE
        lsfg::cache_format
        lsfg::hash
        lsfg::pe
        lsfg::shader_set
        lsfg::spirv
        lsfg_project_options
)
add_test(NAME lsfg_extraction_tests COMMAND lsfg_extraction_tests)
