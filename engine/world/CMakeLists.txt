# engine/world/CMakeLists.txt
#
# Simulation World module encapsulating Flecs ECS.

add_library(flecs_static STATIC
    src/flecs/flecs.c
)
target_include_directories(flecs_static
    SYSTEM PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/flecs>
)
set_target_properties(flecs_static PROPERTIES
    LINKER_LANGUAGE C
    C_STANDARD 99
)
target_compile_options(flecs_static PRIVATE -w)

add_library(engine_world STATIC
    src/world.cpp
    src/hierarchy_systems.cpp
)
add_library(hikari::engine_world ALIAS engine_world)

target_include_directories(engine_world
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(engine_world
    PUBLIC
        hikari::engine_flags
        hikari::engine_core
        flecs_static
)
