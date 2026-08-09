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

add_executable(lsfg_graph_tests graph_tests.cpp)
target_link_libraries(
    lsfg_graph_tests
    PRIVATE
        lsfg::image_graph
        lsfg::shader_set
        lsfg_project_options
)
add_test(NAME lsfg_graph_tests COMMAND lsfg_graph_tests)

add_executable(lsfg_cache_tests cache_tests.cpp)
target_link_libraries(
    lsfg_cache_tests
    PRIVATE
        lsfg::cache_store
        lsfg::image_graph
        lsfg::shader_set
        lsfg_project_options
)
add_test(NAME lsfg_cache_tests COMMAND lsfg_cache_tests)

add_executable(lsfg_backend_tests backend_tests.cpp)
target_link_libraries(
    lsfg_backend_tests
    PRIVATE
        lsfg::backend
        lsfg::dksh
        lsfg::image_graph
        lsfg::shader_set
        lsfg_project_options
)
add_test(NAME lsfg_backend_tests COMMAND lsfg_backend_tests)

add_executable(lsfg_translation_tests translation_tests.cpp)
target_link_libraries(
    lsfg_translation_tests
    PRIVATE
        lsfg::dksh
        lsfg::spirv
        lsfg::translate
        lsfg_project_options
)
add_test(NAME lsfg_translation_tests COMMAND lsfg_translation_tests)
