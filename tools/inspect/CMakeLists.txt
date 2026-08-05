add_executable(lsfg_inspect main.cpp)
target_link_libraries(
    lsfg_inspect
    PRIVATE
        lsfg::cache_format
        lsfg::hash
        lsfg::pe
        lsfg::shader_set
        lsfg::spirv
        lsfg::version
        lsfg_project_options
)
set_target_properties(lsfg_inspect PROPERTIES OUTPUT_NAME lsfg-inspect)
