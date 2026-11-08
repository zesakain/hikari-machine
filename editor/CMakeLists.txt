# editor/CMakeLists.txt
#
# Dear ImGui editor tooling subsystem, guarded by EDITOR flag.

if(NOT HIKARI_BUILD_EDITOR)
    return()
endif()

find_package(Vulkan REQUIRED)

include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8-docking
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
    add_library(imgui_static STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    )
    set_target_properties(imgui_static PROPERTIES SYSTEM TRUE)
    target_include_directories(imgui_static
        SYSTEM PUBLIC
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(imgui_static
        PUBLIC
            Vulkan::Vulkan
    )
    target_compile_options(imgui_static PRIVATE -w)
endif()

add_library(editor_lib STATIC
    src/editor_camera.cpp
    src/editor_engine.cpp
    src/editor_theme.cpp
    src/selection_model.cpp
    src/ui_helpers.cpp
    src/workspace_manager.cpp
    src/panels/hierarchy_panel.cpp
    src/panels/inspector_panel.cpp
    src/panels/viewport_panel.cpp
    src/panels/console_panel.cpp
    src/panels/metrics_panel.cpp
    src/panels/conductor_panel.cpp
    src/panels/content_browser_panel.cpp
)
add_library(hikari::editor_lib ALIAS editor_lib)

target_include_directories(editor_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    SYSTEM PRIVATE
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(editor_lib
    PUBLIC
        hikari::engine_flags
        hikari::engine_core
        hikari::engine_platform
        hikari::engine_rhi
        hikari::engine_world
        hikari::engine_framework
        hikari::engine_physics
        hikari::engine_script
    PRIVATE
        imgui_static
        Vulkan::Vulkan
)
