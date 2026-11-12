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

struct RawQuerySample {
    uint64_t rawTicks[12]{};
    float passTimeMs[6]{}; // 0: total, 1: shadow, 2: main, 3: bloomDown, 4: bloomUp, 5: toneMap
    bool valid{false};
};

struct WorkloadBenchmarkResult {
    std::string name;
    uint32_t warmUpFrames{30};
    uint32_t steadyStateFrames{120};

    // 3D Scene Geometry
    uint32_t mainPbrDrawCalls{0};
    uint32_t shadowDrawCalls{0};
    uint32_t sceneTriangles{0};
    uint32_t shadowTriangles{0};
    uint32_t sceneVertices{0};
    uint32_t culledPrimitives{0};
    uint32_t loadedEntities{0};

    // Dear ImGui UI Overlay Geometry
    uint32_t imguiDrawCalls{0};
    uint32_t imguiVertices{0};

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

    RawQuerySample sampleQueries;
    std::string gpu0Telemetry;
    std::string gpu1Telemetry;
};

static std::string captureNvidiaSmi(uint32_t gpuId) {
    std::string cmd =
        "nvidia-smi --id=" + std::to_string(gpuId) +
        " --query-gpu=name,utilization.gpu,utilization.memory,clocks.current.graphics,"
        "power.draw,temperature.gpu --format=csv,noheader,nounits 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
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
            const auto& sceneStats = editor.lastSceneRenderStats();

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

            res.mainPbrDrawCalls = sceneStats.drawCalls;
            res.shadowDrawCalls = sceneStats.shadowDrawCalls;
            res.sceneTriangles = sceneStats.triangles;
            res.shadowTriangles = sceneStats.shadowTriangles;
            res.sceneVertices = sceneStats.vertices;
            res.culledPrimitives = sceneStats.culledPrimitives;
            res.loadedEntities = sceneStats.meshesRendered;

            res.imguiDrawCalls = editor.lastDrawCallCount();
            res.imguiVertices = editor.lastVertexCount();

            // Capture raw query sample during steady-state
            if (f == warmUpCount + 10 && !res.sampleQueries.valid) {
                const uint32_t readbackBase = (((f) % 2)) * 16;
                uint64_t rawTimestamps[16]{};
                if (device.getQueryResults(readbackBase, 12, rawTimestamps)) {
                    std::memcpy(res.sampleQueries.rawTicks, rawTimestamps, sizeof(uint64_t) * 12);
                    res.sampleQueries.passTimeMs[0] = gpuTimings.totalSceneMs;
                    res.sampleQueries.passTimeMs[1] = gpuTimings.shadowPassMs;
                    res.sampleQueries.passTimeMs[2] = gpuTimings.mainPbrPassMs;
                    res.sampleQueries.passTimeMs[3] = gpuTimings.bloomDownsampleMs;
                    res.sampleQueries.passTimeMs[4] = gpuTimings.bloomUpsampleMs;
                    res.sampleQueries.passTimeMs[5] = gpuTimings.toneMappingMs;
                    res.sampleQueries.valid = true;
                }
            }
        }
    }

    res.gpu0Telemetry = captureNvidiaSmi(0);
    res.gpu1Telemetry = captureNvidiaSmi(1);
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
    std::cout << "Initial GPU 0 (P106-100) Telemetry: " << captureNvidiaSmi(0) << "\n";
    std::cout << "Initial GPU 1 (GT 1030) Telemetry:   " << captureNvidiaSmi(1) << "\n\n";

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
        std::cout << "  3D Scene Geometry:\n";
        std::cout << "    - Main PBR Draw Calls:   " << r.mainPbrDrawCalls << " calls\n";
        std::cout << "    - Shadow Pass Draw Calls:" << r.shadowDrawCalls << " calls\n";
        std::cout << "    - Total Scene Draw Calls:" << (r.mainPbrDrawCalls + r.shadowDrawCalls)
                  << " calls\n";
        std::cout << "    - Main Pass Triangles:   " << r.sceneTriangles << " triangles\n";
        std::cout << "    - Shadow Pass Triangles: " << r.shadowTriangles << " triangles\n";
        std::cout << "    - Total Scene Vertices:  " << r.sceneVertices << " vertices\n";
        std::cout << "    - Culled / Skipped:      " << r.culledPrimitives << " primitives\n";
        std::cout << "    - Evaluated ECS Meshes:  " << r.loadedEntities << " entities\n";
        std::cout << "  UI Overlay Geometry (Dear ImGui):\n";
        std::cout << "    - ImGui Draw Calls:      " << r.imguiDrawCalls << " calls\n";
        std::cout << "    - ImGui Vertices:        " << r.imguiVertices << " vertices\n";
        std::cout << "  Frame Latency & FPS:\n";
        std::cout << "    - FPS:                   " << fps
                  << " (Avg Interval: " << r.frameInterval.avg()
                  << " ms, Min: " << r.frameInterval.min() << " ms, Max: " << r.frameInterval.max()
                  << " ms, p99: " << r.frameInterval.p99() << " ms)\n";
        std::cout << "  CPU Breakdown:\n";
        std::cout << "    - CPU Command Recording: Avg " << r.cpuRecord.avg() << " ms (Min "
                  << r.cpuRecord.min() << ", Max " << r.cpuRecord.max() << ")\n";
        std::cout << "    - CPU vkQueueSubmit:     Avg " << r.queueSubmit.avg() << " ms\n";
        std::cout << "    - CPU vkWaitForFences:   Avg " << r.fenceWait.avg()
                  << " ms (STALL DURATION)\n";
        std::cout << "    - CPU Swapchain Acquire: Avg " << r.swapchainAcquire.avg() << " ms\n";
        std::cout << "    - CPU Swapchain Present: Avg " << r.swapchainPresent.avg() << " ms\n";
        std::cout << "  GPU Hardware Pass Breakdown:\n";
        const float totalPassSum = r.gpuShadowPass.avg() + r.gpuMainPbrPass.avg() +
                                   r.gpuBloomDown.avg() + r.gpuBloomUp.avg() +
                                   r.gpuToneMapping.avg();
        std::cout << "    - Total Scene GPU:       Avg " << r.gpuTotalScene.avg() << " ms (Min "
                  << r.gpuTotalScene.min() << ", Max " << r.gpuTotalScene.max() << ")\n";
        std::cout << "    - Shadow Pass:           Avg " << r.gpuShadowPass.avg() << " ms ("
                  << (r.gpuTotalScene.avg() > 0
                          ? (r.gpuShadowPass.avg() / r.gpuTotalScene.avg() * 100.0F)
                          : 0.0F)
                  << "%)\n";
        std::cout << "    - Main PBR Pass:         Avg " << r.gpuMainPbrPass.avg() << " ms ("
                  << (r.gpuTotalScene.avg() > 0
                          ? (r.gpuMainPbrPass.avg() / r.gpuTotalScene.avg() * 100.0F)
                          : 0.0F)
                  << "%)\n";
        std::cout << "    - Bloom Downsample:      Avg " << r.gpuBloomDown.avg() << " ms\n";
        std::cout << "    - Bloom Upsample:        Avg " << r.gpuBloomUp.avg() << " ms\n";
        std::cout << "    - Tone Mapping Pass:     Avg " << r.gpuToneMapping.avg() << " ms\n";
        std::cout << "    - Pass Sum vs Total:     Sum " << totalPassSum << " ms vs Total "
                  << r.gpuTotalScene.avg()
                  << " ms (Delta: " << (r.gpuTotalScene.avg() - totalPassSum)
                  << " ms barrier gap)\n";
        if (r.sampleQueries.valid) {
            std::cout << "  Raw Timestamp Sample (Steady-State Frame):\n";
            std::cout << "    - Query 0  (Scene Start TopOfPipe):    "
                      << r.sampleQueries.rawTicks[0] << "\n";
            std::cout << "    - Query 2  (Shadow Start TopOfPipe):   "
                      << r.sampleQueries.rawTicks[2] << "\n";
            std::cout << "    - Query 3  (Shadow End BottomOfPipe):  "
                      << r.sampleQueries.rawTicks[3]
                      << " (Delta: " << (r.sampleQueries.rawTicks[3] - r.sampleQueries.rawTicks[2])
                      << " ticks, " << r.sampleQueries.passTimeMs[1] << " ms)\n";
            std::cout << "    - Query 4  (Main PBR Start TopOfPipe): "
                      << r.sampleQueries.rawTicks[4] << "\n";
            std::cout << "    - Query 5  (Main PBR End BottomOfPipe):"
                      << r.sampleQueries.rawTicks[5]
                      << " (Delta: " << (r.sampleQueries.rawTicks[5] - r.sampleQueries.rawTicks[4])
                      << " ticks, " << r.sampleQueries.passTimeMs[2] << " ms)\n";
            std::cout << "    - Query 6  (Bloom Down Start):         "
                      << r.sampleQueries.rawTicks[6] << "\n";
            std::cout << "    - Query 7  (Bloom Down End):           "
                      << r.sampleQueries.rawTicks[7] << "\n";
            std::cout << "    - Query 8  (Bloom Up Start):           "
                      << r.sampleQueries.rawTicks[8] << "\n";
            std::cout << "    - Query 9  (Bloom Up End):             "
                      << r.sampleQueries.rawTicks[9] << "\n";
            std::cout << "    - Query 10 (Tone Map Start):           "
                      << r.sampleQueries.rawTicks[10] << "\n";
            std::cout << "    - Query 11 (Tone Map End):             "
                      << r.sampleQueries.rawTicks[11] << "\n";
            std::cout << "    - Query 1  (Scene End BottomOfPipe):   "
                      << r.sampleQueries.rawTicks[1] << " (Total Delta: "
                      << (r.sampleQueries.rawTicks[1] - r.sampleQueries.rawTicks[0]) << " ticks, "
                      << r.sampleQueries.passTimeMs[0] << " ms)\n";
        }
        std::cout << "  NVIDIA GPU 0 (P106-100 Render):  " << r.gpu0Telemetry << "\n";
        std::cout << "  NVIDIA GPU 1 (GT 1030 Display):   " << r.gpu1Telemetry << "\n";
    }

    std::cout << "\n=========================================================================\n";
    std::cout << " Benchmark Profiling Completed Successfully.\n";
    std::cout << "=========================================================================\n";
    return 0;
}
