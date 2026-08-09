add_executable(lsfg_inspect main.cpp)
target_link_libraries(
    lsfg_inspect
    PRIVATE
        lsfg::backend
        lsfg::cache_format
        lsfg::cache_store
        lsfg::dksh
        lsfg::hash
        lsfg::image_graph
        lsfg::pe
        lsfg::prepare
        lsfg::shader_set
        lsfg::spirv
        lsfg::translate
        lsfg::version
        lsfg_project_options
)
set_target_properties(lsfg_inspect PROPERTIES OUTPUT_NAME lsfg-inspect)
