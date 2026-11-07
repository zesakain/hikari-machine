# engine/ui/CMakeLists.txt
#
# Runtime user interface subsystem integrating RmlUi.

include(FetchContent)

set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(RMLUI_THIRDPARTY_CONTAINERS OFF CACHE BOOL "" FORCE)
set(RMLUI_CUSTOM_RTTI ON CACHE BOOL "" FORCE)
set(RMLUI_COMPILER_OPTIONS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    RmlUi
    GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
    GIT_TAG        6.0
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(RmlUi)
if(NOT rmlui_POPULATED)
    FetchContent_Populate(RmlUi)
    add_subdirectory(${rmlui_SOURCE_DIR} ${rmlui_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

if(TARGET rmlui_core)
    set_target_properties(rmlui_core PROPERTIES SYSTEM TRUE)
    target_compile_options(rmlui_core PRIVATE -w)
endif()

add_library(engine_ui STATIC
    src/ui_system.cpp
    src/rmlui_render_backend.cpp
    src/rmlui_system_backend.cpp
)
add_library(hikari::engine_ui ALIAS engine_ui)

target_include_directories(engine_ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    SYSTEM PRIVATE
        ${rmlui_SOURCE_DIR}/Include
)

target_link_libraries(engine_ui
    PUBLIC
        hikari::engine_flags
        hikari::engine_core
        hikari::engine_platform
        hikari::engine_rhi
        hikari::engine_render
    PRIVATE
        rmlui_core
)
