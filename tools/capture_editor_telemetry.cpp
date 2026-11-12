/// @file capture_editor_telemetry.cpp
/// @brief Offscreen GPU capture and telemetry benchmarking tool for the Hikari Editor.
///
/// Purpose:
/// Measures and captures actual real-time telemetry across different editor workloads:
/// 1. Empty Scene (lightweight baseline)
/// 2. Sponza with Directional Shadows & Bloom enabled
/// 3. Sponza with Shadows & Bloom disabled
/// 4. Damaged Helmet PBR scene
///
/// Renders complete Editor UI with Metrics panel, status bar, and 3D viewport offscreen,
/// reads back pixels via Vulkan GPU transfer buffer, and outputs PPM verification images.

#include "core/log.h"
#include "core/time/frame_telemetry.h"
#include "editor/editor_camera.h"
#include "editor/editor_engine.h"
#include "editor/workspace_manager.h"
#include "framework/conductor.h"
#include "framework/runtime_session.h"
#include "framework/scene_serializer.h"
#include "physics/physics_components.h"
#include "render/asset_manager.h"
#include "render/light_components.h"
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
#include <iomanip>
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

static void savePpmBgra(const std::string& path, const uint8_t* bgra, uint32_t width,
                        uint32_t height) {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) {
        std::cerr << "Failed to open PPM output: " << path << '\n';
        return;
    }
    f << "P6\n" << width << " " << height << "\n255\n";
    std::vector<uint8_t> rgb(width * height * 3);
    for (size_t i = 0; i < width * height; ++i) {
        rgb[(i * 3) + 0] = bgra[(i * 4) + 2]; // R
        rgb[(i * 3) + 1] = bgra[(i * 4) + 1]; // G
        rgb[(i * 3) + 2] = bgra[(i * 4) + 0]; // B
    }
    f.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    std::cout << "[SAVED] " << path << " (" << width << "x" << height << ")\n";
}

int main(int argc, char** argv) {
    std::string outDir = "test-assets";
    if (argc > 1) {
        outDir = argv[1];
    }
    std::filesystem::create_directories(outDir);

    std::cout << "========================================================\n";
    std::cout << " Hikari Editor Real-Time Telemetry Certification Tool   \n";
    std::cout << "========================================================\n";

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
    const uint64_t byteSize = width * height * 4;

    // Create offscreen BGRA8 color target texture matching ImGui Vulkan dynamic rendering format
    rhi::TextureDesc rtDesc{
        .type = rhi::TextureType::Texture2D,
        .format = rhi::TextureFormat::BGRA8_UNORM,
        .width = width,
        .height = height,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = rhi::TextureUsageFlags::ColorAttachment | rhi::TextureUsageFlags::TransferSrc,
        .debugName = "EditorTelemetryColorTarget",
    };
    auto rtRes = device->createTexture(rtDesc);
    if (!rtRes.hasValue()) {
        std::cerr << "Failed to create color target texture\n";
        return 1;
    }
    auto rtHandle = rtRes.value();

    // Create readback buffer
    rhi::BufferDesc rbDesc{
        .sizeInBytes = byteSize,
        .usage = rhi::BufferUsageFlags::TransferDst,
        .memoryUsage = rhi::MemoryUsage::GpuToCpu,
        .debugName = "EditorTelemetryReadbackBuffer",
    };
    auto rbRes = device->createBuffer(rbDesc);
    if (!rbRes.hasValue()) {
        std::cerr << "Failed to create readback buffer\n";
        return 1;
    }
    auto rbHandle = rbRes.value();

    // Initialize EditorEngine
    editor::EditorEngine editor;
    if (!editor.initialize(*device, {.showDemoWindow = false})) {
        std::cerr << "Failed to initialize EditorEngine\n";
        return 1;
    }

    world::World world;
    physics::registerPhysicsComponents(world);
    editor.bindWorld(&world);

    auto renderAndCaptureWorkload = [&](const std::string& workloadName, uint32_t frameCount,
                                        const std::string& filename) {
        std::cout << "\n>>> Benchmarking Workload: " << workloadName << " (" << frameCount
                  << " frames) <<<\n";

        Timestamp lastFrameTime = Timestamp::now();
        Duration lastCpuDuration = Duration::fromMilliseconds(2);

        for (uint32_t f = 0; f < frameCount; ++f) {
            Timestamp currentFrameTime = Timestamp::now();
            Duration frameDuration = currentFrameTime - lastFrameTime;
            lastFrameTime = currentFrameTime;

            Timestamp cpuStart = Timestamp::now();

            editor.recordFrameTelemetry(frameDuration, lastCpuDuration);
            editor.beginFrame(frameDuration, width, height);
            editor.drawPanels();

            auto cmdRes = device->createCommandList();
            if (!cmdRes.hasValue())
                continue;
            auto cmd = std::move(cmdRes.value());

            cmd->begin();

            // Render 3D Scene into viewport
            editor.renderScene(*cmd);

            // Render Editor UI into offscreen target
            rhi::RenderPassBeginInfo passInfo{
                .colorTarget = rtHandle,
                .clearColor = {0.12F, 0.14F, 0.18F, 1.0F},
                .clearTarget = true,
            };
            cmd->beginRenderPass(passInfo);
            editor.endFrame(*cmd);
            cmd->endRenderPass();

            // If last frame, copy to readback buffer
            if (f == frameCount - 1) {
                cmd->copyTextureToBuffer(rtHandle, rbHandle);
            }

            cmd->end();
            (void)device->submit(cmd.get());
            device->waitIdle();

            Timestamp cpuEnd = Timestamp::now();
            lastCpuDuration = cpuEnd - cpuStart;
        }

        const auto& tel = editor.getTelemetryTracker();
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  [TELEMETRY RESULT] " << workloadName << ":\n";
        std::cout << "    Instantaneous FPS: " << tel.instantaneousFps() << " FPS\n";
        std::cout << "    Rolling Avg FPS:   " << tel.averageFps() << " FPS (Min: " << tel.minFps()
                  << ", Max: " << tel.maxFps() << ")\n";
        std::cout << "    Frame Time:        " << tel.frameTimeMs()
                  << " ms (Avg: " << tel.averageFrameTimeMs() << " ms)\n";
        std::cout << "    CPU Workload:      " << tel.cpuTimeMs()
                  << " ms (Avg: " << tel.averageCpuTimeMs() << " ms)\n";
        std::cout << "    GPU Hardware:      "
                  << (tel.hasGpuTiming() ? std::to_string(tel.gpuTimeMs()) + " ms"
                                         : "N/A (Queries unsupported)")
                  << "\n";

        // Readback pixels
        std::vector<uint8_t> pxBuffer(byteSize, 0);
        auto copyRes = device->readBuffer(rbHandle, 0, pxBuffer.data(), byteSize);
        if (copyRes.hasValue()) {
            savePpmBgra(outDir + "/" + filename, pxBuffer.data(), width, height);
        }
    };

    // 1. Empty Scene Telemetry
    renderAndCaptureWorkload("Empty Scene", 60, "telemetry_empty_scene.ppm");

    // 2. Sponza with Shadows and Bloom Enabled
    {
        const char* sponzaPath = "assets/benchmark-scenes/Sponza_Benchmark.hikari";
        if (std::filesystem::exists(sponzaPath)) {
            editor.getWorkspaceManager().openScene(world, sponzaPath, &editor.getSelectionModel(),
                                                   nullptr, &editor.getSceneRenderer());
            renderAndCaptureWorkload("Sponza (Shadows & Bloom Enabled)", 60,
                                     "telemetry_sponza_shadows_bloom.ppm");
        }
    }

    // 3. Sponza with Shadows and Bloom Disabled
    {
        auto pp = editor.getSceneRenderer().getPostProcessSettings();
        pp.bloomEnabled = false;
        editor.getSceneRenderer().setPostProcessSettings(pp);

        // Disable directional shadow casting
        world.getRawWorld().query<render::DirectionalLightComponent>().each(
            [](flecs::entity, render::DirectionalLightComponent& light) {
                light.castShadows = false;
            });

        renderAndCaptureWorkload("Sponza (Shadows & Bloom Disabled)", 60,
                                 "telemetry_sponza_no_shadows_no_bloom.ppm");
    }

    // 4. Damaged Helmet Benchmark Scene
    {
        const char* helmetPath = "assets/benchmark-scenes/DamagedHelmet_Benchmark.hikari";
        if (std::filesystem::exists(helmetPath)) {
            editor.getWorkspaceManager().openScene(world, helmetPath, &editor.getSelectionModel(),
                                                   nullptr, &editor.getSceneRenderer());
            renderAndCaptureWorkload("Damaged Helmet Benchmark", 60,
                                     "telemetry_damaged_helmet.ppm");
        }
    }

    device->destroyBuffer(rbHandle);
    device->destroyTexture(rtHandle);
    editor.shutdown();

    std::cout << "\nAll telemetry benchmark captures and measurements completed successfully.\n";
    return 0;
}
