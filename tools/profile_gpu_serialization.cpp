/// @file profile_gpu_serialization.cpp
/// @brief Comprehensive benchmark tool measuring CPU-GPU serialization, queue timings,
/// and individual GPU pass execution across standard engine workloads.

#include "core/log.h"
#include "core/time.h"
#include "editor/editor_engine.h"
#include "framework/scene_serializer.h"
#include "physics/physics_components.h"
#include "platform/platform.h"
#include "render/asset_manager.h"
#include "render/light_components.h"
#include "render/scene_renderer.h"
#include "rhi/command_list.h"
#include "rhi/rhi_device.h"
#include "world/hierarchy_systems.h"
#include "world/world.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

using namespace eng;

struct PassTimingStats {
    std::vector<float> samples;

    void add(float val) { samples.push_back(val); }

    [[nodiscard]] float min() const {
        return samples.empty() ? 0.0F : *std::min_element(samples.begin(), samples.end());
    }

    [[nodiscard]] float max() const {
        return samples.empty() ? 0.0F : *std::max_element(samples.begin(), samples.end());
    }

    [[nodiscard]] float avg() const {
        if (samples.empty()) {
            return 0.0F;
        }
        double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
        return static_cast<float>(sum / static_cast<double>(samples.size()));
    }

    [[nodiscard]] float p99() const {
        if (samples.empty()) {
            return 0.0F;
        }
        auto copy = samples;
        std::sort(copy.begin(), copy.end());
        size_t idx = static_cast<size_t>(std::ceil(0.99 * static_cast<double>(copy.size()))) - 1;
        if (idx >= copy.size()) {
            idx = copy.size() - 1;
        }
        return copy[idx];
    }
};

struct WorkloadBenchmarkResult {
    std::string name;
    uint32_t warmUpFrames{30};
    uint32_t steadyStateFrames{120};
    uint32_t drawCalls{0};
    uint32_t vertices{0};

    PassTimingStats frameInterval;
    PassTimingStats cpuRecord;
    PassTimingStats queueSubmit;
    PassTimingStats fenceWait;
    PassTimingStats swapchainAcquire;
    PassTimingStats swapchainPresent;

    PassTimingStats gpuTotalScene;
    PassTimingStats gpuShadowPass;
    PassTimingStats gpuMainPbrPass;
    PassTimingStats gpuBloomDown;
    PassTimingStats gpuBloomUp;
    PassTimingStats gpuToneMapping;

    std::string nvidiaSmiTelemetry;
};

static std::string captureNvidiaSmi() {
    FILE* pipe =
        popen("nvidia-smi "
              "--query-gpu=name,utilization.gpu,utilization.memory,clocks.current.graphics,power."
              "draw,temperature.gpu --format=csv,noheader,nounits 2>/dev/null",
              "r");
    if (!pipe) {
        return "N/A";
    }
    char buffer[256];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result.empty() ? "N/A" : result;
}

static WorkloadBenchmarkResult runWorkload(platform::Platform& platform, platform::Window& window,
                                           rhi::RhiDevice& device, editor::EditorEngine& editor,
                                           const std::string& workloadName,
                                           const std::string& scenePath, bool enableShadows,
                                           bool enableBloom, uint32_t warmUpCount = 30,
                                           uint32_t sampleCount = 120) {

    WorkloadBenchmarkResult res{};
    res.name = workloadName;
    res.warmUpFrames = warmUpCount;
    res.steadyStateFrames = sampleCount;

    world::World world;
    physics::registerPhysicsComponents(world);
    render::registerRenderComponents(world);

    editor.bindWorld(&world);

    if (!scenePath.empty()) {
        auto desRes = framework::SceneSerializer::deserializeFromFile(world, scenePath);
        if (!desRes.hasValue()) {
            std::cerr << "Failed to deserialize scene: " << scenePath << " ("
                      << desRes.error().message << ")\n";
        } else if (desRes.value().postProcessSettings.has_value()) {
            editor.setPostProcessSettings(*desRes.value().postProcessSettings);
        }
    }

    // Configure directional shadow
    auto& ecs = world.getRawWorld();
    auto dirQuery = ecs.query<render::DirectionalLightComponent>();
    dirQuery.each([&](flecs::entity e, render::DirectionalLightComponent& dir) {
        static_cast<void>(e);
        dir.castShadows = enableShadows;
    });

    // Configure post processing settings
    render::PostProcessSettings ppSettings = editor.getPostProcessSettings();
    ppSettings.enabled = true;
    ppSettings.bloomEnabled = enableBloom;
    editor.setPostProcessSettings(ppSettings);

    const uint32_t width = window.getWidth();
    const uint32_t height = window.getHeight();
    editor.resizeViewport(width, height);

    auto cmdRes = device.createCommandList();
    if (!cmdRes.hasValue()) {
        std::cerr << "Failed to allocate command list for benchmark\n";
        return res;
    }
    auto mainCmd = std::move(cmdRes.value());

    Timestamp lastFrameTime = Timestamp::now();

    const uint32_t totalFrames = warmUpCount + sampleCount;

    for (uint32_t f = 0; f < totalFrames; ++f) {
        const Timestamp frameStart = Timestamp::now();
        const Duration frameDuration = frameStart - lastFrameTime;
        lastFrameTime = frameStart;

        const bool isSteadyState = (f >= warmUpCount);

        // 1. Image acquisition
        (void)device.acquireNextImage();

        platform.pollEvents();
        const auto& input = platform.getCurrentInput();

        // 2. CPU update and ImGui panel layout
        const Timestamp recordStart = Timestamp::now();

        std::optional<Duration> gpuDur = std::nullopt;
        const auto& priorGpu = editor.getGpuPassTimings();
        if (priorGpu.valid) {
            gpuDur =
                Duration::fromMicroseconds(static_cast<int64_t>(priorGpu.totalSceneMs * 1000.0F));
        }

        editor.recordFrameTelemetry(frameDuration, Duration::fromMilliseconds(1), gpuDur);
        editor.processInput(input);
        editor.beginFrame(frameDuration, width, height);
        editor.drawPanels();

        world.progress(0.016F);

        // 3. Command recording
        mainCmd->begin();
        editor.renderScene(*mainCmd);

        rhi::RenderPassBeginInfo passInfo{
            .clearColor = {0.08F, 0.12F, 0.20F, 1.0F},
            .clearTarget = true,
        };
        mainCmd->beginRenderPass(passInfo);
        editor.endFrame(*mainCmd);
        mainCmd->endRenderPass();
        mainCmd->end();

        const Timestamp recordEnd = Timestamp::now();
        const float recordMs = static_cast<float>((recordEnd - recordStart).asMilliseconds());

        // 4. Submit to GPU queue
        (void)device.submit(mainCmd.get());

        // 5. Present
        (void)device.present();

        if (isSteadyState) {
            const float intervalMs = static_cast<float>(frameDuration.asMilliseconds());
            const auto qStats = device.getQueueTimingStats();
            const auto& gpuTimings = editor.getGpuPassTimings();

            res.frameInterval.add(intervalMs);
            res.cpuRecord.add(recordMs);
            res.queueSubmit.add(qStats.submitTimeMs);
            res.fenceWait.add(qStats.fenceWaitTimeMs);
            res.swapchainAcquire.add(qStats.acquireTimeMs);
            res.swapchainPresent.add(qStats.presentTimeMs);

            if (gpuTimings.valid) {
                res.gpuTotalScene.add(gpuTimings.totalSceneMs);
                res.gpuShadowPass.add(gpuTimings.shadowPassMs);
                res.gpuMainPbrPass.add(gpuTimings.mainPbrPassMs);
                res.gpuBloomDown.add(gpuTimings.bloomDownsampleMs);
                res.gpuBloomUp.add(gpuTimings.bloomUpsampleMs);
                res.gpuToneMapping.add(gpuTimings.toneMappingMs);
            }

            res.drawCalls = editor.lastDrawCallCount();
            res.vertices = editor.lastVertexCount();
        }
    }

    res.nvidiaSmiTelemetry = captureNvidiaSmi();
    return res;
}

int main(int, char**) {
    std::cout << "=========================================================================\n";
    std::cout << " Hikari Engine: GPU Utilization, Frame Latency & Synchronization Profiler \n";
    std::cout << "=========================================================================\n";

    platform::PlatformConfig platConfig{.headless = false};
    auto platRes = platform::createPlatform(platConfig);
    if (!platRes.hasValue()) {
        std::cerr << "Failed to create platform\n";
        return 1;
    }
    auto platform = std::move(platRes.value());

    auto winRes = platform->createWindow(platform::WindowDesc{
        .title = "Hikari Benchmark Profiler",
        .width = 1280,
        .height = 720,
        .vulkan = true,
    });
    if (!winRes.hasValue()) {
        std::cerr << "Failed to create window\n";
        return 1;
    }
    auto window = std::move(winRes.value());

    rhi::VulkanDeviceDesc vkDesc{
        .windowHandle = window->getNativeHandle(),
        .enableValidation = false,
    };
    auto devRes = rhi::createVulkanDevice(vkDesc);
    if (!devRes.hasValue()) {
        std::cerr << "Failed to create Vulkan device\n";
        return 1;
    }
    auto device = std::move(devRes.value());

    editor::EditorEngine editor;
    if (!editor.initialize(*device, {.showDemoWindow = false})) {
        std::cerr << "Failed to initialize EditorEngine\n";
        return 1;
    }

    std::cout << "Selected GPU: " << device->getCaps().deviceName.data() << "\n";
    std::cout << "Timestamp Period: " << device->getCaps().limits.timestampPeriod
              << " ns | Valid Bits: " << device->getCaps().limits.timestampValidBits << "\n";
    std::cout << "Initial NVIDIA GPU Telemetry: " << captureNvidiaSmi() << "\n\n";

    std::vector<WorkloadBenchmarkResult> results;

    // 1. Empty Scene
    std::cout << ">>> Running Workload 1/4: Empty Scene <<<\n";
    results.push_back(
        runWorkload(*platform, *window, *device, editor, "1. Empty Scene", "", false, false));

    // 2. Damaged Helmet
    std::cout << ">>> Running Workload 2/4: Damaged Helmet <<<\n";
    results.push_back(runWorkload(*platform, *window, *device, editor, "2. Damaged Helmet",
                                  "assets/benchmark-scenes/DamagedHelmet_Benchmark.hikari", true,
                                  true));

    // 3. Sponza (No Shadows, No Bloom)
    std::cout << ">>> Running Workload 3/4: Sponza (No Shadows, No Bloom) <<<\n";
    results.push_back(runWorkload(*platform, *window, *device, editor,
                                  "3. Sponza (No Shadows, No Bloom)",
                                  "assets/benchmark-scenes/Sponza_Benchmark.hikari", false, false));

    // 4. Sponza (Shadows & Bloom Enabled)
    std::cout << ">>> Running Workload 4/4: Sponza (Shadows & Bloom Enabled) <<<\n";
    results.push_back(runWorkload(*platform, *window, *device, editor,
                                  "4. Sponza (Shadows & Bloom Enabled)",
                                  "assets/benchmark-scenes/Sponza_Benchmark.hikari", true, true));

    editor.shutdown();

    // Print Results Summary Table
    std::cout << "\n=========================================================================\n";
    std::cout << "                      MEASURED TIMING BREAKDOWN TABLE                     \n";
    std::cout << "=========================================================================\n";
    std::cout << std::fixed << std::setprecision(2);

    for (const auto& r : results) {
        float fps = (r.frameInterval.avg() > 0.0F) ? (1000.0F / r.frameInterval.avg()) : 0.0F;
        std::cout << "\n--- " << r.name << " ---\n";
        std::cout << "  Draw Calls: " << r.drawCalls << " | Vertices: " << r.vertices << "\n";
        std::cout << "  FPS: " << fps << " (Frame Interval: Avg " << r.frameInterval.avg()
                  << " ms, Min " << r.frameInterval.min() << " ms, Max " << r.frameInterval.max()
                  << " ms, p99 " << r.frameInterval.p99() << " ms)\n";
        std::cout << "  CPU Breakdown:\n";
        std::cout << "    - CPU Command Recording: Avg " << r.cpuRecord.avg() << " ms (Min "
                  << r.cpuRecord.min() << ", Max " << r.cpuRecord.max() << ")\n";
        std::cout << "    - CPU vkQueueSubmit:     Avg " << r.queueSubmit.avg() << " ms\n";
        std::cout << "    - CPU vkWaitForFences:   Avg " << r.fenceWait.avg()
                  << " ms (STALL DURATION)\n";
        std::cout << "    - CPU Swapchain Acquire: Avg " << r.swapchainAcquire.avg() << " ms\n";
        std::cout << "    - CPU Swapchain Present: Avg " << r.swapchainPresent.avg() << " ms\n";
        std::cout << "  GPU Hardware Pass Breakdown:\n";
        std::cout << "    - Total Scene GPU:       Avg " << r.gpuTotalScene.avg() << " ms (Min "
                  << r.gpuTotalScene.min() << ", Max " << r.gpuTotalScene.max() << ")\n";
        std::cout << "    - Shadow Pass:           Avg " << r.gpuShadowPass.avg() << " ms\n";
        std::cout << "    - Main PBR Pass:         Avg " << r.gpuMainPbrPass.avg() << " ms\n";
        std::cout << "    - Bloom Downsample:      Avg " << r.gpuBloomDown.avg() << " ms\n";
        std::cout << "    - Bloom Upsample:        Avg " << r.gpuBloomUp.avg() << " ms\n";
        std::cout << "    - Tone Mapping Pass:     Avg " << r.gpuToneMapping.avg() << " ms\n";
        std::cout << "  NVIDIA Driver Telemetry:   " << r.nvidiaSmiTelemetry << "\n";
    }

    std::cout << "\n=========================================================================\n";
    std::cout << " Benchmark Profiling Completed Successfully.\n";
    std::cout << "=========================================================================\n";
    return 0;
}
