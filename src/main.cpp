/// @file main.cpp
/// @brief Primary engine executable for Hikari: Two-State Conductor over Shared World.

#include "anim/animation_components.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/time.h"
#include "framework/conductor.h"
#include "framework/mode.h"
#include "physics/physics_components.h"
#include "physics/physics_world.h"
#include "platform/platform.h"
#include "render/render_graph.h"
#include "rhi/command_list.h"
#include "rhi/rhi_device.h"
#include "script/script_components.h"
#include "script/script_engine.h"
#include "ui/ui_system.h"
#include "world/world.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

using namespace eng;

namespace {

// -----------------------------------------------------------------------------
// Demonstration Gameplay Modes
// -----------------------------------------------------------------------------

class ExplorationMode final : public framework::Mode {
public:
    [[nodiscard]] std::string_view getName() const noexcept override {
        return "ExplorationMode (State A)";
    }

    [[nodiscard]] Result<void> enter(world::World& /*world*/) override {
        ENG_LOG_INFO("Conductor", "Entering ExplorationMode (State A)");
        return Result<void>::success();
    }

    void update(world::World& /*world*/, Duration /*delta*/) override {
        // Authoritative State A continuous gameplay update
    }

    [[nodiscard]] Result<void> exit(world::World& /*world*/) override {
        ENG_LOG_INFO("Conductor", "Exiting ExplorationMode (State A)");
        return Result<void>::success();
    }

    [[nodiscard]] Result<framework::PreparationStatus> prepare(world::World& /*world*/,
                                                               const Budget& /*budget*/
                                                               ) override {
        // Speculatively prepares options and pose for State A
        return framework::PreparationStatus::Ready;
    }

    [[nodiscard]] Result<void> commit(world::World& /*world*/) override {
        return Result<void>::success();
    }

    void cancelPrepare() override {}

    [[nodiscard]] framework::ModeState getModeState() const noexcept override {
        return framework::ModeState::Active;
    }
};

class SkillMode final : public framework::Mode {
public:
    [[nodiscard]] std::string_view getName() const noexcept override {
        return "SkillMode (State B)";
    }

    [[nodiscard]] Result<void> enter(world::World& /*world*/) override {
        ENG_LOG_INFO("Conductor", "Entering SkillMode (State B) with entry slow-motion");
        return Result<void>::success();
    }

    void update(world::World& /*world*/, Duration /*delta*/) override {
        // Authoritative State B gameplay update
    }

    [[nodiscard]] Result<void> exit(world::World& /*world*/) override {
        ENG_LOG_INFO("Conductor", "Exiting SkillMode (State B)");
        return Result<void>::success();
    }

    [[nodiscard]] Result<framework::PreparationStatus> prepare(world::World& /*world*/,
                                                               const Budget& /*budget*/
                                                               ) override {
        // Standby preparation: pre-warms effects, target option matrix, camera
        return framework::PreparationStatus::Ready;
    }

    [[nodiscard]] Result<void> commit(world::World& /*world*/) override {
        return Result<void>::success();
    }

    void cancelPrepare() override {}

    [[nodiscard]] framework::ModeState getModeState() const noexcept override {
        return framework::ModeState::Active;
    }
};

} // namespace

int main(int argc, char* argv[]) {
    bool headless = false;
    uint32_t targetFrames = 0; // 0 = continuous until window close / quit

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--headless") {
            headless = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            targetFrames = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--version") {
            std::cout << "hikari version 0.1.0 (Phase 6 Architecture Complete)\n";
            std::cout << "Architecture: Two-State Conductor over Shared World (C++20)\n";
            return 0;
        } else if (arg == "--help") {
            std::cout << "Hikari 3D Game Engine\n";
            std::cout << "Usage: hikari [options]\n\n";
            std::cout << "Options:\n";
            std::cout << "  --headless         Run continuous headless simulation without window\n";
            std::cout << "  --frames <count>   Execute for <count> simulation frames then exit\n";
            std::cout << "  --version          Display engine build version\n";
            std::cout << "  --help             Display command-line options\n";
            return 0;
        }
    }

    if (headless && targetFrames == 0) {
        targetFrames = 60; // Default headless run limit for CI / automated runs
    }

    ENG_LOG_INFO("Core", "=== Hikari Engine 0.1.0 Initializing ===");

    // 1. Core Job System
    JobSystemConfig jobConfig{.workerCount = 2, .maxJobs = 128};
    size_t jobBytes = JobSystem::requiredBytes(jobConfig);
    std::vector<std::byte> jobStorage(jobBytes);
    JobSystem jobSystem(jobStorage, jobConfig);

    // 2. Platform Subsystem
    platform::PlatformConfig platConfig{.headless = headless};
    auto platRes = platform::createPlatform(platConfig);
    if (!platRes.hasValue()) {
        ENG_LOG_CRITICAL("Platform", "Failed to initialize platform subsystem");
        return 1;
    }
    auto platform = std::move(platRes.value());

    // 3. Window & RHI Device
    std::unique_ptr<rhi::RhiDevice> rhiDevice;
    std::unique_ptr<platform::Window> window;

    if (headless) {
        auto devRes = rhi::createNullDevice();
        ENG_ASSERT(devRes.hasValue(), "Failed to create Null RHI device in headless mode");
        rhiDevice = std::move(devRes.value());
    } else {
        auto winRes = platform->createWindow(platform::WindowDesc{
            .title = "Hikari Engine (C++20)", .width = 1280, .height = 720, .vulkan = true});
        if (winRes.hasValue()) {
            window = std::move(winRes.value());
            rhi::VulkanDeviceDesc vkDesc{.windowHandle = window->getNativeHandle(),
                                         .enableValidation = false};
            auto devRes = rhi::createVulkanDevice(vkDesc);
            if (devRes.hasValue()) {
                rhiDevice = std::move(devRes.value());
            }
        }
        if (!rhiDevice) {
            ENG_LOG_WARN("RHI", "Vulkan device unavailable; falling back to NullDevice");
            auto devRes = rhi::createNullDevice();
            rhiDevice = std::move(devRes.value());
        }
    }

    // 4. Command List
    auto cmdRes = rhiDevice->createCommandList();
    if (!cmdRes.hasValue()) {
        ENG_LOG_CRITICAL("RHI", "Failed to create main command list");
        return 1;
    }
    auto mainCmd = std::move(cmdRes.value());

    // 5. Shared Simulation World & Subsystems
    world::World world;

    physics::PhysicsWorld physicsWorld;
    physics::registerPhysicsComponents(world);
    physics::registerPhysicsSystems(world, physicsWorld);

    anim::registerAnimationComponents(world);

    script::ScriptEngine scriptEngine;
    (void)scriptEngine.initialize();
    scriptEngine.bindWorld(&world);
    script::registerScriptComponents(world);
    script::registerScriptSystems(world, scriptEngine);

    ui::UiSystem uiSystem;
    (void)uiSystem.initialize({.width = 1280, .height = 720}, *rhiDevice);

    // 6. Two-State Conductor
    auto modeA = std::make_unique<ExplorationMode>();
    auto modeB = std::make_unique<SkillMode>();
    framework::Conductor conductor(world, std::move(modeA), std::move(modeB));
    auto initRes = conductor.initialize();
    if (!initRes.hasValue()) {
        ENG_LOG_CRITICAL("Conductor", "Failed to initialize Two-State Conductor");
        return 1;
    }

    ENG_LOG_INFO("Core", "=== Initialization Complete. Entering Main Loop ===");

    // 7. Authoritative Simulation Frame Loop
    uint32_t frameCount = 0;
    bool running = true;
    const Duration fixedStep = Duration::fromMilliseconds(16);

    while (running) {
        platform->pollEvents();
        const auto& input = platform->getCurrentInput();
        const auto& prevInput = platform->getPreviousInput();

        if (window && window->shouldClose()) {
            running = false;
            break;
        }

        // Test mode switch every 30 frames in headless
        if (headless && frameCount > 0 && (frameCount % 30) == 0) {
            if (conductor.getState() == framework::ConductorState::Active) {
                (void)conductor.requestTransition();
            } else if (conductor.getState() == framework::ConductorState::Prepared) {
                (void)conductor.executeTransition();
            }
        }

        // Update Conductor (updates clocks and ticks active mode & standby preparation)
        conductor.update(fixedStep);

        // Step authoritative Flecs world
        world.progress(static_cast<float>(fixedStep.toSeconds()));

        // Update UI using unscaled UiClock delta
        uiSystem.processInput(input, prevInput);
        uiSystem.update(fixedStep);

        // Render Graph Execution
        render::RenderGraph renderGraph;
        uiSystem.render(renderGraph);
        auto compileRes = renderGraph.compile();
        if (compileRes.hasValue()) {
            mainCmd->begin();
            (void)renderGraph.execute(*rhiDevice, *mainCmd);
            mainCmd->end();
        }

        // Swapchain present if windowed Vulkan
        if (window) {
            (void)rhiDevice->acquireNextImage();
            (void)rhiDevice->present();
        }

        frameCount++;
        if (targetFrames > 0 && frameCount >= targetFrames) {
            running = false;
        }
    }

    ENG_LOG_FORMAT(LogLevel::Info, "Core",
                   "=== Simulation loop ended after %u frames. Tearing down... ===", frameCount);

    // 8. Graceful Reverse Shutdown
    conductor.shutdown();
    uiSystem.shutdown();
    scriptEngine.unbindWorld();
    scriptEngine.shutdown();
    rhiDevice.reset();
    platform->shutdown();

    Logger::defaultLogger().flush(stdout);
    std::cout << "Hikari simulation completed cleanly (" << frameCount << " frames).\n";
    return 0;
}
