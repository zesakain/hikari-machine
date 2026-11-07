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

add_library(engine_base INTERFACE)
add_library(hikari::engine_base ALIAS engine_base)

target_link_libraries(engine_base
    INTERFACE
        hikari::engine_flags
        hikari::engine_core
        hikari::engine_platform
)
