# engine/anim/CMakeLists.txt
#
# Skeletal animation, keyframe sampling, blending, and forward kinematics.

add_library(engine_anim STATIC
    src/skeleton.cpp
    src/animation_clip.cpp
    src/animation_sampler.cpp
    src/animation_controller.cpp
    src/animation_components.cpp
)
add_library(hikari::engine_anim ALIAS engine_anim)

target_include_directories(engine_anim
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(engine_anim
    PUBLIC
        hikari::engine_flags
        hikari::engine_core
        hikari::engine_world
)
