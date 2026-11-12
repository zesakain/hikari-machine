/// @file capture_benchmark_scenes.cpp
/// @brief Offscreen GPU capture tool for persistent .hikari benchmark scenes.
///
/// Purpose:
/// Loads persistent .hikari benchmark scenes from assets/benchmark-scenes/,
/// sets up Vulkan GPU rendering, executes the scene renderer with PBR shaders,
/// shadow mapping, and post-processing, reads back the color buffer, and saves
/// PPM verification images.

#include "core/log.h"
#include "framework/scene_serializer.h"
#include "render/asset_manager.h"
#include "render/camera_component.h"
#include "render/light_components.h"
#include "render/mesh_component.h"
#include "render/post_process_types.h"
#include "render/scene_renderer.h"
#include "rhi/command_list.h"
#include "rhi/rhi_device.h"
#include "world/hierarchy_systems.h"
#include "world/world.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

#if defined(__SANITIZE_ADDRESS__)
extern "C" const char* __lsan_default_suppressions() {
    return "leak:libdbus-1.so.3\n"
           "leak:libvulkan.so.1\n"
           "leak:libnvidia-glcore.so\n";
}
#endif

using namespace eng;

static constexpr float toRadians(float deg) noexcept {
    return deg * (3.14159265358979323846F / 180.0F);
}

static void savePpm(const std::string& path, const uint8_t* rgba, uint32_t width, uint32_t height) {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) {
        std::cerr << "Failed to open PPM output: " << path << '\n';
        return;
    }
    f << "P6\n" << width << " " << height << "\n255\n";
    std::vector<uint8_t> rgb(width * height * 3);
    for (size_t i = 0; i < width * height; ++i) {
        rgb[(i * 3) + 0] = rgba[(i * 4) + 0];
        rgb[(i * 3) + 1] = rgba[(i * 4) + 1];
        rgb[(i * 3) + 2] = rgba[(i * 4) + 2];
    }
    f.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    std::cout << "[SAVED] " << path << " (" << width << "x" << height << ")\n";
}

int main(int argc, char** argv) {
    std::string outDir = "test-assets";
    if (argc > 1) {
        outDir = argv[1];
    }

    rhi::VulkanDeviceDesc vkDesc{
        .windowHandle = nullptr,
        .enableValidation = false,
    };
    auto devRes = rhi::createVulkanDevice(vkDesc);
    if (!devRes.hasValue()) {
        std::cerr << "Failed to create Vulkan device: " << devRes.error().message << '\n';
        return 1;
    }
    auto device = std::move(devRes.value());

    constexpr uint32_t width = 1280;
    constexpr uint32_t height = 720;

    render::SceneRenderer renderer;
    auto initRes = renderer.init(*device, width, height);
    if (!initRes.hasValue()) {
        std::cerr << "Failed to init SceneRenderer: " << initRes.error().message << '\n';
        return 1;
    }

    const uint64_t byteSize = width * height * 4;
    rhi::BufferDesc rbDesc{
        .sizeInBytes = byteSize,
        .usage = rhi::BufferUsageFlags::TransferDst,
        .memoryUsage = rhi::MemoryUsage::GpuToCpu,
        .debugName = "CaptureReadbackBuffer",
    };
    auto rbRes = device->createBuffer(rbDesc);
    if (!rbRes.hasValue()) {
        std::cerr << "Failed to create readback buffer\n";
        return 1;
    }
    auto rbHandle = rbRes.value();

    auto renderAndCapture = [&](world::World& world, render::AssetManager& assetMgr,
                                const Mat4& viewProj, const std::string& filename) {
        auto cmdRes = device->createCommandList();
        if (!cmdRes.hasValue())
            return;
        auto cmd = std::move(cmdRes.value());

        // Warm up and render
        cmd->begin();
        renderer.renderScene(world, assetMgr, *device, *cmd, viewProj);
        if (renderer.isPostProcessingEnabled()) {
            renderer.renderPostProcess(*cmd);
        }
        cmd->end();
        (void)device->submit(cmd.get());
        device->waitIdle();

        // Second pass: actual frame + copy to readback buffer
        cmdRes = device->createCommandList();
        cmd = std::move(cmdRes.value());
        cmd->begin();
        renderer.renderScene(world, assetMgr, *device, *cmd, viewProj);
        if (renderer.isPostProcessingEnabled()) {
            renderer.renderPostProcess(*cmd);
        }
        rhi::TextureHandle colorTarget = renderer.getColorTarget();
        cmd->copyTextureToBuffer(colorTarget, rbHandle);
        cmd->end();
        (void)device->submit(cmd.get());
        device->waitIdle();

        std::vector<uint8_t> pxBuffer(byteSize, 0);
        auto copyRes = device->readBuffer(rbHandle, 0, pxBuffer.data(), byteSize);
        if (copyRes.hasValue()) {
            savePpm(outDir + "/" + filename, pxBuffer.data(), width, height);
        }
    };

    // 1. Capture Sponza_Benchmark.hikari
    {
        std::cout << "\n--- Loading Sponza_Benchmark.hikari ---\n";
        world::World world;
        render::AssetManager assetMgr;
        const std::string sponzaScenePath = "assets/benchmark-scenes/Sponza_Benchmark.hikari";
        auto desRes = framework::SceneSerializer::deserializeFromFile(world, sponzaScenePath);
        if (desRes.hasValue()) {
            std::cout << "Loaded " << desRes.value().entitiesLoaded << " entities.\n";
            if (desRes.value().postProcessSettings.has_value()) {
                renderer.setPostProcessSettings(*desRes.value().postProcessSettings);
            }

            // Overview Camera: looking down the colonnade
            Mat4 viewOverview =
                Mat4::lookAt(Vec3(0.0F, 2.0F, 0.0F), Vec3(0.0F, 2.0F, -6.0F), Vec3::unitY());
            Mat4 proj = Mat4::perspective(toRadians(65.0F),
                                          static_cast<float>(width) / static_cast<float>(height),
                                          0.1F, 100.0F);
            renderAndCapture(world, assetMgr, proj * viewOverview,
                             "sponza_persistent_overview.ppm");

            // Detail Camera: archway and columns
            Mat4 viewDetail =
                Mat4::lookAt(Vec3(2.5F, 1.8F, -1.0F), Vec3(3.8F, 1.8F, -1.0F), Vec3::unitY());
            renderAndCapture(world, assetMgr, proj * viewDetail,
                             "sponza_persistent_detail_arch.ppm");

            // Shadows Camera: sunlit floor and arches
            Mat4 viewShadows =
                Mat4::lookAt(Vec3(-4.0F, 5.0F, 2.0F), Vec3(0.0F, 1.0F, -2.0F), Vec3::unitY());
            renderAndCapture(world, assetMgr, proj * viewShadows, "sponza_persistent_shadows.ppm");
        } else {
            std::cerr << "Failed to deserialize Sponza scene: " << desRes.error().message << '\n';
        }
    }

    // 2. Capture FlightHelmet_Benchmark.hikari
    {
        std::cout << "\n--- Loading FlightHelmet_Benchmark.hikari ---\n";
        world::World world;
        render::AssetManager assetMgr;
        const std::string flightScenePath = "assets/benchmark-scenes/FlightHelmet_Benchmark.hikari";
        auto desRes = framework::SceneSerializer::deserializeFromFile(world, flightScenePath);
        if (desRes.hasValue()) {
            std::cout << "Loaded " << desRes.value().entitiesLoaded << " entities.\n";
            if (desRes.value().postProcessSettings.has_value()) {
                renderer.setPostProcessSettings(*desRes.value().postProcessSettings);
            }

            Mat4 view =
                Mat4::lookAt(Vec3(0.0F, 0.35F, 0.85F), Vec3(0.0F, 0.2F, 0.0F), Vec3::unitY());
            Mat4 proj = Mat4::perspective(toRadians(60.0F),
                                          static_cast<float>(width) / static_cast<float>(height),
                                          0.05F, 50.0F);
            renderAndCapture(world, assetMgr, proj * view, "flight_helmet_persistent_scene.ppm");
        } else {
            std::cerr << "Failed to deserialize FlightHelmet scene: " << desRes.error().message
                      << '\n';
        }
    }

    // 3. Capture DamagedHelmet_Benchmark.hikari
    {
        std::cout << "\n--- Loading DamagedHelmet_Benchmark.hikari ---\n";
        world::World world;
        render::AssetManager assetMgr;
        const std::string damagedScenePath =
            "assets/benchmark-scenes/DamagedHelmet_Benchmark.hikari";
        auto desRes = framework::SceneSerializer::deserializeFromFile(world, damagedScenePath);
        if (desRes.hasValue()) {
            std::cout << "Loaded " << desRes.value().entitiesLoaded << " entities.\n";
            if (desRes.value().postProcessSettings.has_value()) {
                renderer.setPostProcessSettings(*desRes.value().postProcessSettings);
            }

            Mat4 view = Mat4::lookAt(Vec3(0.0F, 0.0F, 2.5F), Vec3(0.0F, 0.0F, 0.0F), Vec3::unitY());
            Mat4 proj = Mat4::perspective(toRadians(60.0F),
                                          static_cast<float>(width) / static_cast<float>(height),
                                          0.05F, 50.0F);
            renderAndCapture(world, assetMgr, proj * view, "damaged_helmet_persistent_scene.ppm");
        } else {
            std::cerr << "Failed to deserialize DamagedHelmet scene: " << desRes.error().message
                      << '\n';
        }
    }

    device->destroyBuffer(rbHandle);
    renderer.shutdown();
    std::cout << "\nAll captures completed successfully.\n";
    return 0;
}
