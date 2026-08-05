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
