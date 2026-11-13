/// @file sponza_sustained_benchmark.cpp
/// @brief 1,000-frame sustained steady-state 60 FPS verification on NVIDIA Pascal hardware.

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

int main() {
    std::cout << "======================================================================\n";
    std::cout << "  Hikari Engine | Sustained 1,000-Frame 60 FPS Certification Benchmark \n";
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

    constexpr uint32_t width = 1280;
    constexpr uint32_t height = 720;

    render::SceneRenderer renderer;
    auto initRes = renderer.init(*device, width, height);
    if (!initRes.hasValue()) {
        std::cerr << "Failed to initialize SceneRenderer: " << initRes.error().message << '\n';
        return 1;
    }

    world::World world;
    render::AssetManager assetManager;

    auto desRes = framework::SceneSerializer::deserializeFromFile(
        world, "assets/benchmark-scenes/Sponza_Benchmark.hikari");
    if (!desRes.hasValue()) {
        std::cerr << "Failed to load Sponza: " << desRes.error().message << '\n';
        return 1;
    }
    world::updateHierarchyTransforms(world);

    // Apply High preset (target baseline: 1280x720, 2048x2048 directional shadow, ACES, bloom)
    auto settings = render::GraphicsSettings::createPreset(render::GraphicsQualityPreset::High);
    auto applyRes = renderer.applyGraphicsSettings(settings);
    if (!applyRes.hasValue()) {
        std::cerr << "Failed to apply High settings\n";
        return 1;
    }

    const Mat4 view = Mat4::lookAt(Vec3(0.0F, 2.0F, 0.0F), Vec3(0.0F, 2.0F, 5.0F), Vec3::unitY());
    const Mat4 proj = Mat4::perspective(
        toRadians(60.0F), static_cast<float>(width) / static_cast<float>(height), 0.1F, 100.0F);
    const Mat4 vp = proj * view;

    std::cout << "Warming up renderer (30 frames)...\n";
    for (int i = 0; i < 30; ++i) {
        auto cmdRes = device->createCommandList();
        auto cmd = std::move(cmdRes.value());
        cmd->begin();
        (void)renderer.renderScene(world, assetManager, *device, *cmd, vp);
        if (settings.postProcess.enabled) {
            renderer.renderPostProcess(*cmd);
        }
        cmd->end();
        (void)device->submit(cmd.get());
        device->waitIdle();
        renderer.updateGpuTimings(*device);
    }

    constexpr int kSustainedFrames = 1000;
    std::cout << "Executing sustained benchmark (" << kSustainedFrames << " steady-state frames)...\n";

    std::vector<double> gpuFrameTimes;
    gpuFrameTimes.reserve(kSustainedFrames);
    std::vector<double> shadowPassTimes;
    shadowPassTimes.reserve(kSustainedFrames);
    std::vector<double> mainPassTimes;
    mainPassTimes.reserve(kSustainedFrames);
    std::vector<double> cpuRecordTimes;
    cpuRecordTimes.reserve(kSustainedFrames);

    render::RenderStats lastStats{};

    for (int f = 0; f < kSustainedFrames; ++f) {
        auto cmdRes = device->createCommandList();
        auto cmd = std::move(cmdRes.value());
        cmd->begin();

        const auto t0 = std::chrono::high_resolution_clock::now();
        lastStats = renderer.renderScene(world, assetManager, *device, *cmd, vp);
        if (settings.postProcess.enabled) {
            renderer.renderPostProcess(*cmd);
        }
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double cpuMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        cpuRecordTimes.push_back(cpuMs);

        cmd->end();
        (void)device->submit(cmd.get());
        device->waitIdle();
        renderer.updateGpuTimings(*device);

        const auto& timings = renderer.getGpuPassTimings();
        if (timings.valid) {
            gpuFrameTimes.push_back(timings.totalSceneMs);
            shadowPassTimes.push_back(timings.shadowPassMs);
            mainPassTimes.push_back(timings.mainPbrPassMs);
        }
    }

    if (gpuFrameTimes.empty()) {
        std::cerr << "No GPU timing data collected!\n";
        return 1;
    }

    std::vector<double> sortedGpu = gpuFrameTimes;
    std::sort(sortedGpu.begin(), sortedGpu.end());

    const double minGpu = sortedGpu.front();
    const double maxGpu = sortedGpu.back();
    const double medianGpu = sortedGpu[sortedGpu.size() / 2];
    const double p95Gpu = sortedGpu[static_cast<size_t>(static_cast<double>(sortedGpu.size()) * 0.95)];
    const double p99Gpu = sortedGpu[static_cast<size_t>(static_cast<double>(sortedGpu.size()) * 0.99)];
    const double avgGpu = std::accumulate(sortedGpu.begin(), sortedGpu.end(), 0.0) / static_cast<double>(sortedGpu.size());

    double variance = 0.0;
    for (double v : sortedGpu) {
        variance += (v - avgGpu) * (v - avgGpu);
    }
    const double stdDev = std::sqrt(variance / static_cast<double>(sortedGpu.size()));

    const double avgShadow = std::accumulate(shadowPassTimes.begin(), shadowPassTimes.end(), 0.0) / static_cast<double>(shadowPassTimes.size());
    const double avgMain = std::accumulate(mainPassTimes.begin(), mainPassTimes.end(), 0.0) / static_cast<double>(mainPassTimes.size());
    const double avgCpu = std::accumulate(cpuRecordTimes.begin(), cpuRecordTimes.end(), 0.0) / static_cast<double>(cpuRecordTimes.size());

    const double avgFps = 1000.0 / avgGpu;
    const double p95Fps = 1000.0 / p95Gpu;

    std::cout << "\n======================================================================\n";
    std::cout << "                   SUSTAINED BENCHMARK RESULTS (1000 FRAMES)          \n";
    std::cout << "======================================================================\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Scene: Sponza Atrium | Resolution: 1280x720 | Preset: High\n";
    std::cout << "Total Geometry: " << lastStats.drawCalls + lastStats.shadowDrawCalls << " draw calls ("
              << lastStats.drawCalls << " main + " << lastStats.shadowDrawCalls << " shadow), "
              << lastStats.triangles + lastStats.shadowTriangles << " triangles\n";
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "Metric                    Value       Target       Status\n";
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "Average Frame Time:       " << std::setw(6) << avgGpu << " ms   <= 16.67 ms   " << (avgGpu <= 16.67 ? "PASSED" : "FAILED") << '\n';
    std::cout << "Median Frame Time:        " << std::setw(6) << medianGpu << " ms   <= 16.67 ms   " << (medianGpu <= 16.67 ? "PASSED" : "FAILED") << '\n';
    std::cout << "P95 Frame Time:           " << std::setw(6) << p95Gpu << " ms   <= 16.67 ms   " << (p95Gpu <= 16.67 ? "PASSED" : "FAILED") << '\n';
    std::cout << "P99 Frame Time:           " << std::setw(6) << p99Gpu << " ms   <= 16.67 ms   " << (p99Gpu <= 16.67 ? "PASSED" : "FAILED") << '\n';
    std::cout << "Min Frame Time:           " << std::setw(6) << minGpu << " ms\n";
    std::cout << "Max Frame Time:           " << std::setw(6) << maxGpu << " ms\n";
    std::cout << "Frame Time Std Dev:       " << std::setw(6) << stdDev << " ms   <=  0.50 ms   " << (stdDev <= 0.50 ? "EXCELLENT" : "STABLE") << '\n';
    std::cout << "Average FPS:              " << std::setw(6) << avgFps << " FPS  >=  60.0 FPS  " << (avgFps >= 60.0 ? "PASSED" : "FAILED") << '\n';
    std::cout << "P95 FPS Floor:            " << std::setw(6) << p95Fps << " FPS  >=  60.0 FPS  " << (p95Fps >= 60.0 ? "PASSED" : "FAILED") << '\n';
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "Pass Breakdown (Averages):\n";
    std::cout << "  Directional Shadow Pass: " << std::setw(6) << avgShadow << " ms (" << (avgShadow / avgGpu * 100.0) << "%)\n";
    std::cout << "  Main PBR Pass:           " << std::setw(6) << avgMain << " ms (" << (avgMain / avgGpu * 100.0) << "%)\n";
    std::cout << "  CPU Recording Overhead:  " << std::setw(6) << avgCpu << " ms\n";
    std::cout << "======================================================================\n";

    return (p95Gpu <= 16.67) ? 0 : 1;
}
