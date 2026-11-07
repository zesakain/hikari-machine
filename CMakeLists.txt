cmake_minimum_required(VERSION 3.25)

project(
    hikari-machine
    VERSION 0.1.0
    DESCRIPTION "A general-purpose 3D game engine with a two-state Conductor architecture"
    HOMEPAGE_URL "https://github.com/zesakain/hikari-machine"
    LANGUAGES C CXX
)

# Enforce C++20 standard globally across all targets
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for language servers and static analysis (clangd, clang-tidy)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Project options
option(HIKARI_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(HIKARI_BUILD_TESTS "Build test suite" ON)
option(HIKARI_BUILD_EDITOR "Build editor tooling" OFF)
set(HIKARI_SANITIZER "" CACHE STRING "Sanitizers to enable (e.g. 'address,undefined' or 'thread')")

# -----------------------------------------------------------------------------
# Compiler Flags Interface Target
# -----------------------------------------------------------------------------
add_library(hikari_compiler_flags INTERFACE)
add_library(hikari::compiler_flags ALIAS hikari_compiler_flags)

target_compile_features(hikari_compiler_flags INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(hikari_compiler_flags INTERFACE
        /W4
        /permissive-
        /volatile:iso
        /Zc:inline
        /Zc:preprocessor
        /utf-8
    )
    if(HIKARI_WARNINGS_AS_ERRORS)
        target_compile_options(hikari_compiler_flags INTERFACE /WX)
    endif()
else()
    target_compile_options(hikari_compiler_flags INTERFACE
        -Wall
        -Wextra
        -Wconversion
        -Wshadow
        -Wpedantic
    )
    if(HIKARI_WARNINGS_AS_ERRORS)
        target_compile_options(hikari_compiler_flags INTERFACE -Werror)
    endif()
endif()

# Editor flag definition
if(HIKARI_BUILD_EDITOR)
    target_compile_definitions(hikari_compiler_flags INTERFACE EDITOR=1)
endif()

# Sanitizer configuration
if(HIKARI_SANITIZER)
    if(MSVC)
        if(HIKARI_SANITIZER MATCHES "address")
            target_compile_options(hikari_compiler_flags INTERFACE /fsanitize=address)
        endif()
    else()
        target_compile_options(hikari_compiler_flags INTERFACE
            -fsanitize=${HIKARI_SANITIZER}
            -fno-omit-frame-pointer
        )
        target_link_options(hikari_compiler_flags INTERFACE
            -fsanitize=${HIKARI_SANITIZER}
        )
    endif()
endif()

# -----------------------------------------------------------------------------
# Engine Flags Interface Target (Engine-Specific Restrictions)
# Prohibits exceptions and RTTI for all engine components.
# -----------------------------------------------------------------------------
add_library(hikari_engine_flags INTERFACE)
add_library(hikari::engine_flags ALIAS hikari_engine_flags)

target_link_libraries(hikari_engine_flags INTERFACE hikari::compiler_flags)

if(MSVC)
    target_compile_options(hikari_engine_flags INTERFACE
        /EHs-c-    # Disable C++ exceptions
        /GR-       # Disable RTTI
    )
    target_compile_definitions(hikari_engine_flags INTERFACE
        _HAS_EXCEPTIONS=0
    )
else()
    target_compile_options(hikari_engine_flags INTERFACE
        -fno-exceptions
        -fno-rtti
    )
endif()

# -----------------------------------------------------------------------------
# Subdirectories
# -----------------------------------------------------------------------------
add_subdirectory(engine)

if(HIKARI_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()
