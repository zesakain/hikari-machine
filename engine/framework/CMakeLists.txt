# engine/framework/CMakeLists.txt
#
# Framework module providing Mode interfaces and Two-State Conductor execution runtime.

add_library(engine_framework STATIC
    src/conductor.cpp
    src/runtime_session.cpp
    src/scene.cpp
    src/scene_serializer.cpp
)
add_library(hikari::engine_framework ALIAS engine_framework)

target_include_directories(engine_framework
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(engine_framework
    PUBLIC
        hikari::engine_flags
        hikari::engine_core
        hikari::engine_world
        hikari::engine_physics
        hikari::engine_render
        hikari::engine_anim
        hikari::engine_script
)
