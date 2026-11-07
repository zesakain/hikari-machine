# engine/rhi/CMakeLists.txt
#
# Rendering Hardware Interface (RHI) abstraction, Null mock backend, and Vulkan backend.

find_package(Vulkan REQUIRED)
find_package(SDL3 CONFIG REQUIRED)

add_library(engine_rhi STATIC
    src/null/null_command_list.cpp
    src/null/null_device.cpp
    src/vulkan/vulkan_command_list.cpp
    src/vulkan/vulkan_device.cpp
    src/vulkan/vulkan_instance.cpp
    src/vulkan/vulkan_memory.cpp
    src/vulkan/vulkan_physical_device.cpp
    src/vulkan/vulkan_swapchain.cpp
)
add_library(hikari::engine_rhi ALIAS engine_rhi)

target_include_directories(engine_rhi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(engine_rhi
    PUBLIC
        hikari::engine_flags
        hikari::engine_core
    PRIVATE
        Vulkan::Vulkan
        SDL3::SDL3
)
