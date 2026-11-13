/// @file benchmark_sponza_1080p.cpp
/// @brief 1080p Native Sponza Benchmark Harness with Dynamic Camera Paths and Telemetry.

#include "core/log.h"
#include "framework/scene_serializer.h"
#include "render/asset_manager.h"
#include "render/graphics_settings.h"
#include "render/scene_renderer.h"
#include "rhi/command_list.h"
#include "rhi/rhi_device.h"
#include "world/hierarchy_systems.h"
#include "world/world.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
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

struct FrameSample {
    double totalGpuMs{0.0};
    double shadowGpuMs{0.0};
    double depthPrepassGpuMs{0.0};
    double mainPbrGpuMs{0.0};
    double postProcessGpuMs{0.0};
    double cpuRecordMs{0.0};
    double queueSubmitMs{0.0};
    double waitIdleMs{0.0};
    uint32_t mainDraws{0};
    uint32_t shadowDraws{0};
    uint32_t mainTriangles{0};
    uint32_t shadowTriangles{0};
    uint32_t culledPrimitives{0};
};

struct PathStats {
    std::string name;
    size_t sampleCount{0};
    double avgGpuMs{0.0};
    double medianGpuMs{0.0};
    double p95GpuMs{0.0};
    double p99GpuMs{0.0};
    double minGpuMs{0.0};
    double maxGpuMs{0.0};
    double stdDevGpuMs{0.0};
    double avgFps{0.0};
    double p95Fps{0.0};
    double avgShadowMs{0.0};
    double avgDepthPrepassMs{0.0};
    double avgMainPbrMs{0.0};
    double avgPostMs{0.0};
    double avgCpuRecordMs{0.0};
    uint32_t avgMainDraws{0};
    uint32_t avgShadowDraws{0};
    uint32_t avgTriangles{0};
};

static PathStats computeStats(const std::string& name, const std::vector<FrameSample>& samples) {
    PathStats stats{};
    stats.name = name;
    stats.sampleCount = samples.size();
    if (samples.empty()) {
        return stats;
    }

    std::vector<double> gpuTimes;
    gpuTimes.reserve(samples.size());
    double sumShadow = 0.0;
    double sumDepthPrepass = 0.0;
    double sumMain = 0.0;
    double sumPost = 0.0;
    double sumCpu = 0.0;
    uint64_t sumMainDraws = 0;
    uint64_t sumShadowDraws = 0;
    uint64_t sumTriangles = 0;

    for (const auto& s : samples) {
        gpuTimes.push_back(s.totalGpuMs);
        sumShadow += s.shadowGpuMs;
        sumDepthPrepass += s.depthPrepassGpuMs;
        sumMain += s.mainPbrGpuMs;
        sumPost += s.postProcessGpuMs;
        sumCpu += s.cpuRecordMs;
        sumMainDraws += s.mainDraws;
        sumShadowDraws += s.shadowDraws;
        sumTriangles += (s.mainTriangles + s.shadowTriangles);
    }

    std::sort(gpuTimes.begin(), gpuTimes.end());
    const double n = static_cast<double>(gpuTimes.size());
    stats.minGpuMs = gpuTimes.front();
    stats.maxGpuMs = gpuTimes.back();
    stats.medianGpuMs = gpuTimes[gpuTimes.size() / 2];
    stats.p95GpuMs = gpuTimes[static_cast<size_t>(n * 0.95)];
    stats.p99GpuMs = gpuTimes[static_cast<size_t>(n * 0.99)];
    stats.avgGpuMs = std::accumulate(gpuTimes.begin(), gpuTimes.end(), 0.0) / n;

    double variance = 0.0;
    for (double v : gpuTimes) {
        variance += (v - stats.avgGpuMs) * (v - stats.avgGpuMs);
    }
    stats.stdDevGpuMs = std::sqrt(variance / n);

    stats.avgFps = 1000.0 / stats.avgGpuMs;
    stats.p95Fps = 1000.0 / stats.p95GpuMs;

    stats.avgShadowMs = sumShadow / n;
    stats.avgDepthPrepassMs = sumDepthPrepass / n;
    stats.avgMainPbrMs = sumMain / n;
    stats.avgPostMs = sumPost / n;
    stats.avgCpuRecordMs = sumCpu / n;
    stats.avgMainDraws = static_cast<uint32_t>(sumMainDraws / gpuTimes.size());
    stats.avgShadowDraws = static_cast<uint32_t>(sumShadowDraws / gpuTimes.size());
    stats.avgTriangles = static_cast<uint32_t>(sumTriangles / gpuTimes.size());

    return stats;
}

static void printPathStats(const PathStats& s) {
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "Path: " << s.name << " (" << s.sampleCount << " frames)\n";
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Average Frame Time:       " << std::setw(6) << s.avgGpuMs
              << " ms   <= 16.67 ms   " << (s.avgGpuMs <= 16.67 ? "PASSED" : "FAILED") << '\n';
    std::cout << "Median Frame Time:        " << std::setw(6) << s.medianGpuMs
              << " ms   <= 16.67 ms   " << (s.medianGpuMs <= 16.67 ? "PASSED" : "FAILED") << '\n';
    std::cout << "P95 Frame Time:           " << std::setw(6) << s.p95GpuMs
              << " ms   <= 16.67 ms   " << (s.p95GpuMs <= 16.67 ? "PASSED" : "FAILED") << '\n';
    std::cout << "P99 Frame Time:           " << std::setw(6) << s.p99GpuMs
              << " ms   <= 16.67 ms   " << (s.p99GpuMs <= 16.67 ? "PASSED" : "FAILED") << '\n';
    std::cout << "Min / Max Frame Time:     " << std::setw(6) << s.minGpuMs << " ms / "
              << s.maxGpuMs << " ms\n";
    std::cout << "Frame Time Std Dev:       " << std::setw(6) << s.stdDevGpuMs << " ms\n";
    std::cout << "Average FPS:              " << std::setw(6) << s.avgFps << " FPS  >=  60.0 FPS  "
              << (s.avgFps >= 60.0 ? "PASSED" : "FAILED") << '\n';
    std::cout << "P95 FPS Floor:            " << std::setw(6) << s.p95Fps << " FPS  >=  60.0 FPS  "
              << (s.p95Fps >= 60.0 ? "PASSED" : "FAILED") << '\n';
    std::cout << "Draw Calls:               " << (s.avgMainDraws + s.avgShadowDraws) << " ("
              << s.avgMainDraws << " main + " << s.avgShadowDraws << " shadow)\n";
    std::cout << "Triangles:                " << s.avgTriangles << '\n';
    std::cout << "Pass Breakdown (Averages):\n";
    std::cout << "  Shadow Pass:            " << std::setw(6) << s.avgShadowMs << " ms ("
              << (s.avgGpuMs > 0 ? s.avgShadowMs / s.avgGpuMs * 100.0 : 0.0) << "%)\n";
    if (s.avgDepthPrepassMs > 0.0001) {
        std::cout << "  Depth Prepass:          " << std::setw(6) << s.avgDepthPrepassMs << " ms ("
                  << (s.avgGpuMs > 0 ? s.avgDepthPrepassMs / s.avgGpuMs * 100.0 : 0.0) << "%)\n";
    }
    std::cout << "  Main PBR Pass:          " << std::setw(6) << s.avgMainPbrMs << " ms ("
              << (s.avgGpuMs > 0 ? s.avgMainPbrMs / s.avgGpuMs * 100.0 : 0.0) << "%)\n";
    std::cout << "  Post-Processing:        " << std::setw(6) << s.avgPostMs << " ms\n";
    std::cout << "  CPU Recording Overhead: " << std::setw(6) << s.avgCpuRecordMs << " ms\n";
}

int main(int argc, char** argv) {
    int framesPerPath = 300;
    int sustainedTotal = 0;
    bool ultraQuality = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--frames" && i + 1 < argc) {
            framesPerPath = std::max(50, std::stoi(argv[++i]));
        } else if (arg == "--sustained" && i + 1 < argc) {
            sustainedTotal = std::max(100, std::stoi(argv[++i]));
        } else if (arg == "--ultra") {
            ultraQuality = true;
        }
    }

    std::cout << "======================================================================\n";
    std::cout << "  Hikari Engine | Native 1080p Sponza Benchmark Certification Suite   \n";
    std::cout << "======================================================================\n\n";

    constexpr uint32_t width = 1920;
    constexpr uint32_t height = 1080;

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

    std::cout << "Device: " << device->getCaps().deviceName << '\n';
    std::cout << "Resolution: " << width << "x" << height << " (Native 1080p)\n";
    std::cout << "Quality Preset: " << (ultraQuality ? "Ultra (4096 Shadow)" : "High (2048 Shadow)")
              << "\n\n";

    render::SceneRenderer renderer;
    auto initRes = renderer.init(*device, width, height);
    if (!initRes.hasValue()) {
        std::cerr << "Failed to initialize SceneRenderer: " << initRes.error().message << '\n';
        return 1;
    }

    world::World world;
    render::AssetManager assetManager;

    const std::string scenePath = "assets/benchmark-scenes/Sponza_Benchmark.hikari";
    auto loadRes = framework::SceneSerializer::deserializeFromFile(world, scenePath);
    if (!loadRes.hasValue()) {
        std::cerr << "Failed to load Sponza scene: " << loadRes.error().message << '\n';
        return 1;
    }
    world::updateHierarchyTransforms(world);

    auto settings = render::GraphicsSettings::createPreset(
        ultraQuality ? render::GraphicsQualityPreset::Ultra : render::GraphicsQualityPreset::High);
    settings.performance.depthPrepass = false;
    auto applyRes = renderer.applyGraphicsSettings(settings);
    if (!applyRes.hasValue()) {
        std::cerr << "Failed to apply graphics settings\n";
        return 1;
    }

    const Mat4 proj = Mat4::perspective(
        toRadians(65.0F), static_cast<float>(width) / static_cast<float>(height), 0.1F, 100.0F);

    // Warm-up renderer (30 frames)
    std::cout << "Warming up shaders, pipelines, and descriptor sets (30 frames)...\n";
    const Mat4 warmVp =
        proj * Mat4::lookAt(Vec3(0.0F, 2.0F, 0.0F), Vec3(0.0F, 2.0F, 5.0F), Vec3::unitY());
    for (int i = 0; i < 30; ++i) {
        auto cmdRes = device->createCommandList();
        auto cmd = std::move(cmdRes.value());
        cmd->begin();
        (void)renderer.renderScene(world, assetManager, *device, *cmd, warmVp);
        if (renderer.isPostProcessingEnabled()) {
            renderer.renderPostProcess(*cmd);
        }
        cmd->end();
        (void)device->submit(cmd.get());
        device->waitIdle();
        renderer.updateGpuTimings(*device);
    }

    struct CameraPathDef {
        std::string name;
        int frameCount;
        std::function<Mat4(float t)> computeView;
    };

    std::vector<CameraPathDef> paths;

    // Path 1: Central Atrium Longitudinal Traverse (Z: -9.0 -> +9.0)
    paths.push_back({"1. Central Atrium Traverse", framesPerPath, [](float t) {
                         float z = -9.0F + t * 18.0F;
                         Vec3 pos(0.0F, 2.0F, z);
                         Vec3 target(0.0F, 2.0F, z + 5.0F);
                         return Mat4::lookAt(pos, target, Vec3::unitY());
                     }});

    // Path 2: Ground Floor Side Corridor Walk (Colonnade Arches)
    paths.push_back({"2. Side Corridor Colonnade Walk", framesPerPath, [](float t) {
                         float z = -9.0F + t * 18.0F;
                         Vec3 pos(-3.5F, 1.8F, z);
                         Vec3 target(-3.5F, 1.8F, z + 4.0F);
                         return Mat4::lookAt(pos, target, Vec3::unitY());
                     }});

    // Path 3: Upper Gallery Balcony View
    paths.push_back({"3. Upper Gallery Balcony Walk", framesPerPath, [](float t) {
                         float z = -8.0F + t * 16.0F;
                         Vec3 pos(0.0F, 5.5F, z);
                         Vec3 target(0.0F, 1.5F, z + 6.0F);
                         return Mat4::lookAt(pos, target, Vec3::unitY());
                     }});

    // Path 4: Rapid 360-Degree Atrium Yaw Rotation
    paths.push_back({"4. Rapid 360-Degree Rotation", framesPerPath, [](float t) {
                         float angle = t * 6.283185307F;
                         Vec3 pos(0.0F, 2.0F, 0.0F);
                         Vec3 dir(std::sin(angle), 0.0F, std::cos(angle));
                         return Mat4::lookAt(pos, pos + dir, Vec3::unitY());
                     }});

    // Path 5: Worst-Case View (Corner Diagonal through Layered Arches & Shadows)
    paths.push_back({"5. Worst-Case Corner Diagonal", framesPerPath, [](float t) {
                         float p = (std::sin(t * 3.14159265F) + 1.0F) * 0.5F;
                         Vec3 pos = Vec3(-4.8F, 1.6F, -9.5F) + Vec3(0.5F, 0.2F, 0.5F) * p;
                         Vec3 target = Vec3(4.5F, 4.0F, 9.5F);
                         return Mat4::lookAt(pos, target, Vec3::unitY());
                     }});

    std::vector<FrameSample> allSamples;
    std::vector<PathStats> allPathStats;

    if (sustainedTotal > 0) {
        std::cout << ">>> Running Sustained Stress Benchmark (" << sustainedTotal
                  << " frames continuous)...\n";
        allSamples.reserve(static_cast<size_t>(sustainedTotal));

        for (int f = 0; f < sustainedTotal; ++f) {
            float progress = static_cast<float>(f % 1000) / 1000.0F;
            // Continuous composite loop traversing paths
            int pathIdx = (f / 1000) % 5;
            Mat4 view = paths[static_cast<size_t>(pathIdx)].computeView(progress);
            Mat4 vp = proj * view;

            auto cmdRes = device->createCommandList();
            auto cmd = std::move(cmdRes.value());
            cmd->begin();

            const auto tRec0 = std::chrono::high_resolution_clock::now();
            auto stats = renderer.renderScene(world, assetManager, *device, *cmd, vp);
            if (renderer.isPostProcessingEnabled()) {
                renderer.renderPostProcess(*cmd);
            }
            const auto tRec1 = std::chrono::high_resolution_clock::now();
            const double cpuRecMs =
                std::chrono::duration<double, std::milli>(tRec1 - tRec0).count();

            cmd->end();

            const auto tSub0 = std::chrono::high_resolution_clock::now();
            (void)device->submit(cmd.get());
            const auto tSub1 = std::chrono::high_resolution_clock::now();
            const double submitMs =
                std::chrono::duration<double, std::milli>(tSub1 - tSub0).count();

            const auto tWait0 = std::chrono::high_resolution_clock::now();
            device->waitIdle();
            const auto tWait1 = std::chrono::high_resolution_clock::now();
            const double waitMs =
                std::chrono::duration<double, std::milli>(tWait1 - tWait0).count();

            renderer.updateGpuTimings(*device);
            const auto& timings = renderer.getGpuPassTimings();

            if (timings.valid) {
                FrameSample sample{};
                sample.totalGpuMs = timings.totalSceneMs;
                sample.shadowGpuMs = timings.shadowPassMs;
                sample.depthPrepassGpuMs = timings.depthPrepassMs;
                sample.mainPbrGpuMs = timings.mainPbrPassMs;
                sample.postProcessGpuMs =
                    timings.bloomDownsampleMs + timings.bloomUpsampleMs + timings.toneMappingMs;
                sample.cpuRecordMs = cpuRecMs;
                sample.queueSubmitMs = submitMs;
                sample.waitIdleMs = waitMs;
                sample.mainDraws = stats.drawCalls;
                sample.shadowDraws = stats.shadowDrawCalls;
                sample.mainTriangles = stats.triangles;
                sample.shadowTriangles = stats.shadowTriangles;
                sample.culledPrimitives = stats.culledPrimitives;
                allSamples.push_back(sample);
            }

            if ((f + 1) % 1000 == 0) {
                std::cout << "  Progress: " << (f + 1) << " / " << sustainedTotal
                          << " frames completed.\n";
            }
        }

        auto finalStats = computeStats("10,000-Frame Sustained Run", allSamples);
        std::cout << "\n======================================================================\n";
        std::cout << "              10,000-FRAME SUSTAINED 1080P BENCHMARK RESULTS          \n";
        std::cout << "======================================================================\n";
        printPathStats(finalStats);
        return (finalStats.p95GpuMs <= 16.67) ? 0 : 1;
    }

    // Otherwise run per-path evaluation
    for (const auto& pathDef : paths) {
        std::cout << "\n>>> Evaluating " << pathDef.name << " (" << pathDef.frameCount
                  << " frames)...\n";
        std::vector<FrameSample> pathSamples;
        pathSamples.reserve(static_cast<size_t>(pathDef.frameCount));

        for (int f = 0; f < pathDef.frameCount; ++f) {
            float t = static_cast<float>(f) / static_cast<float>(pathDef.frameCount);
            Mat4 view = pathDef.computeView(t);
            Mat4 vp = proj * view;

            auto cmdRes = device->createCommandList();
            auto cmd = std::move(cmdRes.value());
            cmd->begin();

            const auto tRec0 = std::chrono::high_resolution_clock::now();
            auto stats = renderer.renderScene(world, assetManager, *device, *cmd, vp);
            if (renderer.isPostProcessingEnabled()) {
                renderer.renderPostProcess(*cmd);
            }
            const auto tRec1 = std::chrono::high_resolution_clock::now();
            const double cpuRecMs =
                std::chrono::duration<double, std::milli>(tRec1 - tRec0).count();

            cmd->end();

            const auto tSub0 = std::chrono::high_resolution_clock::now();
            (void)device->submit(cmd.get());
            const auto tSub1 = std::chrono::high_resolution_clock::now();
            const double submitMs =
                std::chrono::duration<double, std::milli>(tSub1 - tSub0).count();

            const auto tWait0 = std::chrono::high_resolution_clock::now();
            device->waitIdle();
            const auto tWait1 = std::chrono::high_resolution_clock::now();
            const double waitMs =
                std::chrono::duration<double, std::milli>(tWait1 - tWait0).count();

            renderer.updateGpuTimings(*device);
            const auto& timings = renderer.getGpuPassTimings();

            if (timings.valid) {
                FrameSample sample{};
                sample.totalGpuMs = timings.totalSceneMs;
                sample.shadowGpuMs = timings.shadowPassMs;
                sample.depthPrepassGpuMs = timings.depthPrepassMs;
                sample.mainPbrGpuMs = timings.mainPbrPassMs;
                sample.postProcessGpuMs =
                    timings.bloomDownsampleMs + timings.bloomUpsampleMs + timings.toneMappingMs;
                sample.cpuRecordMs = cpuRecMs;
                sample.queueSubmitMs = submitMs;
                sample.waitIdleMs = waitMs;
                sample.mainDraws = stats.drawCalls;
                sample.shadowDraws = stats.shadowDrawCalls;
                sample.mainTriangles = stats.triangles;
                sample.shadowTriangles = stats.shadowTriangles;
                sample.culledPrimitives = stats.culledPrimitives;
                pathSamples.push_back(sample);
                allSamples.push_back(sample);
            }
        }

        auto pStats = computeStats(pathDef.name, pathSamples);
        printPathStats(pStats);
        allPathStats.push_back(pStats);
    }

    std::cout << "\n======================================================================\n";
    std::cout << "                 OVERALL 1080P BENCHMARK SUMMARY                      \n";
    std::cout << "======================================================================\n";
    auto overallStats = computeStats("All 5 Camera Paths Combined", allSamples);
    printPathStats(overallStats);

    return (overallStats.p95GpuMs <= 16.67) ? 0 : 1;
}
