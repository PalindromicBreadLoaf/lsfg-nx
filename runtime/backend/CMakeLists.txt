set(_backend_sources src/binding.cpp src/cache_load.cpp src/layout.cpp)

if(NINTENDO_SWITCH)
    list(APPEND _backend_sources src/device.cpp src/executor.cpp)
endif()

add_library(lsfg_backend STATIC ${_backend_sources})
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

if(NINTENDO_SWITCH)
    target_link_libraries(lsfg_backend PUBLIC $<IF:$<CONFIG:Debug>,deko3dd,deko3d>)
endif()
