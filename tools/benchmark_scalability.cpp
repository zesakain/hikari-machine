/// @file benchmark_scalability.cpp
/// @brief Multi-scene, multi-preset graphics scalability benchmark certification tool.
///
/// Milestone 11B Deliverable:
/// Benchmarks Sponza, Flight Helmet, and Damaged Helmet across all 5 quality presets:
/// - Low
/// - Medium
/// - High
/// - Ultra
/// - Cinematic
/// - Plus Debug View: Wireframe
///
/// Measures and reports:
/// - CPU recording time (ms)
/// - GPU execution time (ms) via hardware timestamp queries
/// - Shadow pass duration, Main pass duration, Post-processing duration
/// - Draw calls, triangle counts, and culled primitives
/// - Outputs high-resolution visual verification readbacks.

#include "core/log.h"
#include "framework/scene_serializer.h"
#include "render/asset_manager.h"
#include "render/camera_component.h"
#include "render/graphics_settings.h"
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
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
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
    std::cout << "[BENCHMARK-SAVED] " << path << " (" << width << "x" << height << ")\n";
}

struct BenchmarkRecord {
    std::string scene;
    std::string preset;
    uint32_t shadowRes;
    float renderScale;
    float cpuTimeMs;
    float gpuTotalMs;
    float gpuShadowMs;
    float gpuMainMs;
    float gpuPostMs;
    uint32_t mainDraws;
    uint32_t shadowDraws;
    uint32_t totalDraws;
    uint32_t totalTriangles;
};

int main(int argc, char** argv) {
    std::string outDir = "test-assets";
    if (argc > 1) {
        outDir = argv[1];
    }
    std::filesystem::create_directories(outDir);

    std::cout << "======================================================================\n";
    std::cout << "  Hikari Engine | Milestone 11B: Scalability Benchmark Certification  \n";
    std::cout << "======================================================================\n\n";

    rhi::VulkanDeviceDesc vkDesc{
        .windowHandle = nullptr,
        .enableValidation = false,
    };
    auto devRes = rhi::createVulkanDevice(vkDesc);
    if (!devRes.hasValue()) {
        std::cerr << "Failed to initialize Vulkan device: " << devRes.error().message << '\n';
        return 1;
    }
    auto device = std::move(devRes.value());

    constexpr uint32_t baseWidth = 1280;
    constexpr uint32_t baseHeight = 720;

    render::SceneRenderer renderer;
    auto initRes = renderer.init(*device, baseWidth, baseHeight);
    if (!initRes.hasValue()) {
        std::cerr << "Failed to initialize SceneRenderer: " << initRes.error().message << '\n';
        return 1;
    }

    const uint64_t byteSize = baseWidth * baseHeight * 4;
    rhi::BufferDesc rbDesc{
        .sizeInBytes = byteSize,
        .usage = rhi::BufferUsageFlags::TransferDst,
        .memoryUsage = rhi::MemoryUsage::GpuToCpu,
        .debugName = "BenchmarkReadbackBuffer",
    };
    auto rbRes = device->createBuffer(rbDesc);
    if (!rbRes.hasValue()) {
        std::cerr << "Failed to create readback buffer\n";
        return 1;
    }
    auto readbackBuffer = rbRes.value();

    struct SceneInfo {
        std::string name;
        std::string path;
        Vec3 camPos;
        Vec3 camTarget;
        float fov;
    };

    std::vector<SceneInfo> scenes = {
        {"Sponza Atrium", "assets/benchmark-scenes/Sponza_Benchmark.hikari", Vec3(0.0F, 2.0F, 0.0F),
         Vec3(0.0F, 2.0F, 5.0F), 60.0F},
        {"Flight Helmet", "assets/benchmark-scenes/FlightHelmet_Benchmark.hikari",
         Vec3(0.0F, 0.35F, 0.85F), Vec3(0.0F, 0.20F, 0.0F), 45.0F},
        {"Damaged Helmet", "assets/benchmark-scenes/DamagedHelmet_Benchmark.hikari",
         Vec3(0.0F, 0.0F, 2.4F), Vec3(0.0F, 0.0F, 0.0F), 45.0F},
    };

    struct PresetConfig {
        std::string name;
        render::GraphicsSettings settings;
        bool isWireframe{false};
    };

    auto makePresets = [&]() {
        std::vector<PresetConfig> configs;

        configs.push_back(
            {"Low", render::GraphicsSettings::createPreset(render::GraphicsQualityPreset::Low)});
        configs.push_back({"Medium", render::GraphicsSettings::createPreset(
                                         render::GraphicsQualityPreset::Medium)});
        configs.push_back(
            {"High", render::GraphicsSettings::createPreset(render::GraphicsQualityPreset::High)});
        configs.push_back({"Ultra", render::GraphicsSettings::createPreset(
                                        render::GraphicsQualityPreset::Ultra)});
        configs.push_back({"Cinematic", render::GraphicsSettings::createPreset(
                                            render::GraphicsQualityPreset::Cinematic)});

        auto wire = render::GraphicsSettings::createPreset(render::GraphicsQualityPreset::High);
        wire.debug.viewMode = render::DebugViewMode::Wireframe;
        configs.push_back({"Wireframe", wire, true});

        return configs;
    };

    auto presetConfigs = makePresets();
    std::vector<BenchmarkRecord> records;

    for (const auto& scene : scenes) {
        if (!std::filesystem::exists(scene.path)) {
            std::cerr << "[SKIP] Scene file not found: " << scene.path << '\n';
            continue;
        }

        std::cout << "\n>>> Benchmarking Scene: " << scene.name << " (" << scene.path << ")\n";

        world::World world;
        render::AssetManager assetManager;

        auto loadRes = framework::SceneSerializer::deserializeFromFile(world, scene.path);
        if (!loadRes.hasValue()) {
            std::cerr << "Failed loading scene: " << loadRes.error().message << '\n';
            continue;
        }
        world::updateHierarchyTransforms(world);

        Mat4 view = Mat4::lookAt(scene.camPos, scene.camTarget, Vec3::unitY());
        Mat4 proj = Mat4::perspective(
            toRadians(scene.fov), static_cast<float>(baseWidth) / static_cast<float>(baseHeight),
            0.1F, 100.0F);
        Mat4 vp = proj * view;

        for (const auto& preset : presetConfigs) {
            std::cout << "  * Evaluating Preset: " << std::setw(10) << preset.name << " ... "
                      << std::flush;

            // Apply graphics settings
            auto applyRes = renderer.applyGraphicsSettings(preset.settings);
            if (!applyRes.hasValue()) {
                std::cerr << "Error applying preset: " << applyRes.error().message << '\n';
                continue;
            }

            // Warmup frames (5 frames)
            for (int f = 0; f < 5; ++f) {
                auto cmdRes = device->createCommandList();
                auto cmd = std::move(cmdRes.value());
                cmd->begin();
                (void)renderer.renderScene(world, assetManager, *device, *cmd, vp);
                if (preset.settings.postProcess.enabled) {
                    renderer.renderPostProcess(*cmd);
                }
                cmd->end();
                (void)device->submit(cmd.get());
                device->waitIdle();
                renderer.updateGpuTimings(*device);
            }

            // Measurement frames (30 steady-state frames)
            constexpr int kMeasureFrames = 30;
            double totalCpuMs = 0.0;
            render::RenderStats lastStats{};

            for (int f = 0; f < kMeasureFrames; ++f) {
                auto cmdRes = device->createCommandList();
                auto cmd = std::move(cmdRes.value());
                cmd->begin();

                auto tStart = std::chrono::high_resolution_clock::now();
                lastStats = renderer.renderScene(world, assetManager, *device, *cmd, vp);
                if (preset.settings.postProcess.enabled) {
                    renderer.renderPostProcess(*cmd);
                }
                auto tEnd = std::chrono::high_resolution_clock::now();
                totalCpuMs += std::chrono::duration<double, std::milli>(tEnd - tStart).count();

                cmd->end();
                (void)device->submit(cmd.get());
                device->waitIdle();
                renderer.updateGpuTimings(*device);
            }

            const auto& gpuTimings = renderer.getGpuPassTimings();
            double avgCpuMs = totalCpuMs / kMeasureFrames;
            float postMs = gpuTimings.bloomDownsampleMs + gpuTimings.bloomUpsampleMs +
                           gpuTimings.toneMappingMs;

            BenchmarkRecord rec{
                .scene = scene.name,
                .preset = preset.name,
                .shadowRes = preset.settings.shadows.resolution,
                .renderScale = preset.settings.quality.renderScale,
                .cpuTimeMs = static_cast<float>(avgCpuMs),
                .gpuTotalMs = gpuTimings.totalSceneMs,
                .gpuShadowMs = gpuTimings.shadowPassMs,
                .gpuMainMs = gpuTimings.mainPbrPassMs,
                .gpuPostMs = postMs,
                .mainDraws = lastStats.drawCalls,
                .shadowDraws = lastStats.shadowDrawCalls,
                .totalDraws = lastStats.drawCalls + lastStats.shadowDrawCalls,
                .totalTriangles = lastStats.triangles + lastStats.shadowTriangles,
            };
            records.push_back(rec);

            std::cout << "Done! (GPU: " << std::fixed << std::setprecision(2) << rec.gpuTotalMs
                      << " ms, CPU: " << rec.cpuTimeMs << " ms, Draws: " << rec.totalDraws
                      << ", Tris: " << rec.totalTriangles << ")\n";

            // Visual Verification Readback Image Capture
            {
                auto readCmdRes = device->createCommandList();
                auto readCmd = std::move(readCmdRes.value());
                readCmd->begin();

                rhi::TextureHandle srcTex = renderer.getColorTarget();
                readCmd->copyTextureToBuffer(srcTex, readbackBuffer);

                readCmd->end();
                (void)device->submit(readCmd.get());
                device->waitIdle();

                std::vector<uint8_t> pxBuffer(byteSize, 0);
                auto copyRes = device->readBuffer(readbackBuffer, 0, pxBuffer.data(), byteSize);
                if (copyRes.hasValue()) {
                    std::string cleanScene = scene.name;
                    std::replace(cleanScene.begin(), cleanScene.end(), ' ', '_');
                    std::string ppmPath = outDir + "/" + cleanScene + "_" + preset.name + ".ppm";
                    savePpm(ppmPath, pxBuffer.data(), baseWidth, baseHeight);
                }
            }
        }
    }

    // Output Comparative Certification Tables
    std::cout << "\n==============================================================================="
                 "=========\n";
    std::cout << "                       BENCHMARK SCALABILITY CERTIFICATION REPORT                "
                 "       \n";
    std::cout << "================================================================================="
                 "=======\n\n";

    std::cout << "| Scene | Preset | Render Scale | Shadow Res | GPU Total (ms) | Shadow (ms) | "
                 "PBR (ms) | Post (ms) | CPU (ms) | Draw Calls | Triangles |\n";
    std::cout << "| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | "
                 ":---: |\n";

    for (const auto& r : records) {
        std::cout << "| " << r.scene << " | **" << r.preset << "** | " << std::fixed
                  << std::setprecision(2) << r.renderScale << "x | " << r.shadowRes << " | "
                  << std::setprecision(2) << r.gpuTotalMs << " | " << r.gpuShadowMs << " | "
                  << r.gpuMainMs << " | " << r.gpuPostMs << " | " << r.cpuTimeMs << " | "
                  << r.totalDraws << " (" << r.mainDraws << "+" << r.shadowDraws << ") | "
                  << r.totalTriangles << " |\n";
    }

    std::cout << "\n==============================================================================="
                 "=========\n";
    std::cout << "Scalability benchmark complete!\n";

    if (readbackBuffer.isValid()) {
        device->destroyBuffer(readbackBuffer);
    }
    return 0;
}
