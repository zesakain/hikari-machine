# tests/CMakeLists.txt
#
# Root test configuration for Hikari-Machine.

add_executable(smoke_test smoke_test.cpp)
target_link_libraries(smoke_test PRIVATE hikari::engine_base)
add_test(NAME smoke_test COMMAND smoke_test)

# Core Unit Tests
add_executable(result_test unit/core/result_test.cpp)
target_link_libraries(result_test PRIVATE hikari::engine_core)
add_test(NAME result_test COMMAND result_test)

add_executable(assert_test unit/core/assert_test.cpp)
target_link_libraries(assert_test PRIVATE hikari::engine_core)
add_test(NAME assert_test COMMAND assert_test)

add_executable(linear_allocator_test unit/core/linear_allocator_test.cpp)
target_link_libraries(linear_allocator_test PRIVATE hikari::engine_core)
add_test(NAME linear_allocator_test COMMAND linear_allocator_test)

add_executable(arena_allocator_test unit/core/arena_allocator_test.cpp)
target_link_libraries(arena_allocator_test PRIVATE hikari::engine_core)
add_test(NAME arena_allocator_test COMMAND arena_allocator_test)

add_executable(pool_allocator_test unit/core/pool_allocator_test.cpp)
target_link_libraries(pool_allocator_test PRIVATE hikari::engine_core)
add_test(NAME pool_allocator_test COMMAND pool_allocator_test)

add_executable(generational_handle_test unit/core/generational_handle_test.cpp)
target_link_libraries(generational_handle_test PRIVATE hikari::engine_core)
add_test(NAME generational_handle_test COMMAND generational_handle_test)

add_executable(time_test unit/core/time_test.cpp)
target_link_libraries(time_test PRIVATE hikari::engine_core)
add_test(NAME time_test COMMAND time_test)

add_executable(log_test unit/core/log_test.cpp)
target_link_libraries(log_test PRIVATE hikari::engine_core)
add_test(NAME log_test COMMAND log_test)

add_executable(slot_map_test unit/core/slot_map_test.cpp)
target_link_libraries(slot_map_test PRIVATE hikari::engine_core)
add_test(NAME slot_map_test COMMAND slot_map_test)

add_executable(id_test unit/core/id_test.cpp)
target_link_libraries(id_test PRIVATE hikari::engine_core)
add_test(NAME id_test COMMAND id_test)

add_executable(event_test unit/core/event_test.cpp)
target_link_libraries(event_test PRIVATE hikari::engine_core)
add_test(NAME event_test COMMAND event_test)

add_executable(job_system_test unit/core/job_system_test.cpp)
target_link_libraries(job_system_test PRIVATE hikari::engine_core)
add_test(NAME job_system_test COMMAND job_system_test)

add_executable(math_test unit/core/math_test.cpp)
target_link_libraries(math_test PRIVATE hikari::engine_core)
add_test(NAME math_test COMMAND math_test)

# Platform Unit Tests
add_executable(platform_test unit/platform/platform_test.cpp)
target_link_libraries(platform_test PRIVATE hikari::engine_platform)
add_test(NAME platform_test COMMAND platform_test)

# RHI Unit Tests
add_executable(null_rhi_test unit/rhi/null_rhi_test.cpp)
target_link_libraries(null_rhi_test PRIVATE hikari::engine_rhi)
add_test(NAME null_rhi_test COMMAND null_rhi_test)

add_executable(vulkan_rhi_test unit/rhi/vulkan_rhi_test.cpp)
target_link_libraries(vulkan_rhi_test PRIVATE hikari::engine_rhi hikari::engine_platform)
add_test(NAME vulkan_rhi_test COMMAND vulkan_rhi_test)

# World Unit Tests
add_executable(world_test unit/world/world_test.cpp)
target_link_libraries(world_test PRIVATE hikari::engine_world)
add_test(NAME world_test COMMAND world_test)

# Framework Unit Tests
add_executable(conductor_test unit/framework/conductor_test.cpp)
target_link_libraries(conductor_test PRIVATE hikari::engine_framework)
add_test(NAME conductor_test COMMAND conductor_test)

add_executable(clock_domain_test unit/framework/clock_domain_test.cpp)
target_link_libraries(clock_domain_test PRIVATE hikari::engine_framework)
add_test(NAME clock_domain_test COMMAND clock_domain_test)

# Headless Integration / Stress Tests
add_executable(conductor_stress_test headless/conductor_stress_test.cpp)
target_link_libraries(conductor_stress_test PRIVATE hikari::engine_framework)
add_test(NAME conductor_stress_test COMMAND conductor_stress_test)

# Render Unit Tests
add_executable(render_graph_test unit/render/render_graph_test.cpp)
target_link_libraries(render_graph_test PRIVATE hikari::engine_render)
add_test(NAME render_graph_test COMMAND render_graph_test)

# Physics Unit Tests
add_executable(physics_test unit/physics/physics_test.cpp)
target_link_libraries(physics_test PRIVATE hikari::engine_physics)
add_test(NAME physics_test COMMAND physics_test)

# Animation Unit Tests
add_executable(anim_test unit/anim/anim_test.cpp)
target_link_libraries(anim_test PRIVATE hikari::engine_anim)
add_test(NAME anim_test COMMAND anim_test)

# Script Unit Tests
add_executable(script_test unit/script/script_test.cpp)
target_link_libraries(script_test PRIVATE hikari::engine_script hikari::engine_world)
add_test(NAME script_test COMMAND script_test)

# UI Unit Tests
add_executable(ui_test unit/ui/ui_test.cpp)
target_link_libraries(ui_test PRIVATE hikari::engine_ui hikari::engine_render hikari::engine_rhi)
add_test(NAME ui_test COMMAND ui_test)

# Editor Unit Tests (Guarded by EDITOR flag)
if(HIKARI_BUILD_EDITOR)
    add_executable(editor_test unit/editor/editor_test.cpp)
    target_link_libraries(editor_test PRIVATE hikari::editor_lib hikari::engine_framework hikari::engine_world hikari::engine_rhi)
    add_test(NAME editor_test COMMAND editor_test)
endif()








