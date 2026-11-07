# engine/platform/CMakeLists.txt
#
# Platform module library managing SDL3 windowing and input abstractions.

find_package(SDL3 CONFIG REQUIRED)

add_library(engine_platform STATIC
    src/sdl_platform.cpp
    src/sdl_window.cpp
)
add_library(hikari::engine_platform ALIAS engine_platform)

target_include_directories(engine_platform
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(engine_platform
    PUBLIC
        hikari::engine_flags
        hikari::engine_core
    PRIVATE
        SDL3::SDL3
)
