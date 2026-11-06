# engine/core/CMakeLists.txt
#
# Core module library providing foundational engine primitives.

add_library(engine_core STATIC
    src/assert.cpp
)
add_library(hikari::engine_core ALIAS engine_core)

target_include_directories(engine_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(engine_core
    PUBLIC
        hikari::engine_flags
)
