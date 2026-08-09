add_library(lsfg_backend STATIC src/cache_load.cpp)
add_library(lsfg::backend ALIAS lsfg_backend)
target_include_directories(lsfg_backend PUBLIC include)
target_link_libraries(
    lsfg_backend
    PUBLIC
        lsfg::cache_store
        lsfg::image_graph
        lsfg_project_options
    PRIVATE
        lsfg::dksh
        lsfg::shader_set
)
