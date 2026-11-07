# engine/CMakeLists.txt
#
# Engine root CMake configuration.
# Subsystems link against `hikari::engine_flags` which enforces:
# - C++20 standard
# - Strict warnings (-Wall -Wextra -Wconversion -Wshadow -Wpedantic)
# - No exceptions (-fno-exceptions, _HAS_EXCEPTIONS=0)
# - No RTTI (-fno-rtti, /GR-)

add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(rhi)
add_subdirectory(world)
add_subdirectory(framework)
add_subdirectory(render)
add_subdirectory(physics)
add_subdirectory(anim)
add_subdirectory(script)
add_subdirectory(ui)

add_library(engine_base INTERFACE)
add_library(hikari::engine_base ALIAS engine_base)

target_link_libraries(engine_base
    INTERFACE
        hikari::engine_flags
        hikari::engine_core
        hikari::engine_platform
        hikari::engine_rhi
        hikari::engine_world
        hikari::engine_framework
        hikari::engine_render
        hikari::engine_physics
        hikari::engine_anim
        hikari::engine_script
        hikari::engine_ui
)
