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

# Platform Unit Tests
add_executable(platform_test unit/platform/platform_test.cpp)
target_link_libraries(platform_test PRIVATE hikari::engine_platform)
add_test(NAME platform_test COMMAND platform_test)




