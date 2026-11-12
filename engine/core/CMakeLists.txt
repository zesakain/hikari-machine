# engine/core/CMakeLists.txt
#
# Core module library providing foundational engine primitives.

find_package(Threads REQUIRED)

add_library(engine_core STATIC
    src/arena_allocator.cpp
    src/assert.cpp
    src/budget.cpp
    src/clock.cpp
    src/event.cpp
    src/frame_telemetry.cpp
    src/job_system.cpp
    src/json.cpp
    src/linear_allocator.cpp
    src/log.cpp
    src/pool_allocator.cpp
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
        Threads::Threads
)
