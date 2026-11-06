# tests/CMakeLists.txt
#
# Root test configuration for Hikari-Machine.
# Contains dependency-free smoke testing and hooks for future unit, headless,
# golden, and benchmark test suites.

add_executable(smoke_test smoke_test.cpp)

target_link_libraries(smoke_test
    PRIVATE
        hikari::engine_base
)

add_test(
    NAME smoke_test
    COMMAND smoke_test
)
