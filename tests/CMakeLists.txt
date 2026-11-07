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
