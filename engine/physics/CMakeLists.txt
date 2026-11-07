# engine/physics/CMakeLists.txt
#
# Physics simulation subsystem integrating Jolt Physics.

include(FetchContent)

set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DISTRIBUTION OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE ON CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.6.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  Build
)
FetchContent_MakeAvailable(JoltPhysics)

if(TARGET Jolt)
    target_compile_options(Jolt PRIVATE -w)
    if(HIKARI_SANITIZER)
        if(NOT MSVC)
            target_compile_options(Jolt PRIVATE
                -fsanitize=${HIKARI_SANITIZER}
                -fno-omit-frame-pointer
            )
            target_link_options(Jolt PRIVATE
                -fsanitize=${HIKARI_SANITIZER}
            )
        endif()
    endif()
endif()

add_library(engine_physics STATIC
    src/jolt_bridge.cpp
    src/physics_system.cpp
    src/physics_world.cpp
    src/physics_components.cpp
)
add_library(hikari::engine_physics ALIAS engine_physics)

target_include_directories(engine_physics
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_include_directories(engine_physics SYSTEM PRIVATE
    $<BUILD_INTERFACE:${joltphysics_SOURCE_DIR}/..>
)

target_link_libraries(engine_physics
    PUBLIC
        hikari::engine_flags
        hikari::engine_core
        hikari::engine_world
    PRIVATE
        Jolt
)
