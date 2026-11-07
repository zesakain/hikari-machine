# engine/render/CMakeLists.txt
#
# High-level rendering module providing Render Graph scheduling.

add_library(engine_render STATIC
    src/render_graph.cpp
)
add_library(hikari::engine_render ALIAS engine_render)

target_include_directories(engine_render
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(engine_render
    PUBLIC
        hikari::engine_flags
        hikari::engine_core
        hikari::engine_rhi
)
