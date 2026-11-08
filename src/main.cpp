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
#include "render/mesh_component.h"
#include "render/render_graph.h"
#include "rhi/command_list.h"
#include "rhi/rhi_device.h"
#include "script/script_components.h"
#include "script/script_engine.h"
#include "ui/ui_system.h"
#include "world/world.h"

#if defined(HIKARI_BUILD_EDITOR)
#include "editor/editor_engine.h"
#include "framework/runtime_session.h"
#endif

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

class StateAMode final : public framework::Mode {
public:
    [[nodiscard]] std::string_view getName() const noexcept override { return "State A"; }

    [[nodiscard]] Result<void> enter(world::World& /*world*/) override {
        ENG_LOG_INFO("Conductor", "Entering State A (Active Role)");
        return Result<void>::success();
    }

    void update(world::World& /*world*/, Duration /*delta*/) override {
        // Authoritative State A continuous simulation update
    }

    [[nodiscard]] Result<void> exit(world::World& /*world*/) override {
        ENG_LOG_INFO("Conductor", "Exiting State A");
        return Result<void>::success();
    }

    [[nodiscard]] Result<framework::PreparationStatus> prepare(world::World& /*world*/,
                                                               const Budget& /*budget*/
                                                               ) override {
        // Speculatively prepares entry state for State A
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

class StateBMode final : public framework::Mode {
public:
    [[nodiscard]] std::string_view getName() const noexcept override { return "State B"; }

    [[nodiscard]] Result<void> enter(world::World& /*world*/) override {
        ENG_LOG_INFO("Conductor", "Entering State B (Active Role) with entry slow-motion");
        return Result<void>::success();
    }

    void update(world::World& /*world*/, Duration /*delta*/) override {
        // Authoritative State B continuous simulation update
    }

    [[nodiscard]] Result<void> exit(world::World& /*world*/) override {
        ENG_LOG_INFO("Conductor", "Exiting State B");
        return Result<void>::success();
    }

    [[nodiscard]] Result<framework::PreparationStatus> prepare(world::World& /*world*/,
                                                               const Budget& /*budget*/
                                                               ) override {
        // Standby preparation: speculatively prepares entry state for State B
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
    uint32_t windowWidth = 1280;
    uint32_t windowHeight = 720;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--headless") {
            headless = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            targetFrames = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--width" && i + 1 < argc) {
            windowWidth = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--height" && i + 1 < argc) {
            windowHeight = static_cast<uint32_t>(std::atoi(argv[++i]));
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
            std::cout << "  --width <pixels>   Set window width in pixels (default: 1280)\n";
            std::cout << "  --height <pixels>  Set window height in pixels (default: 720)\n";
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
        auto winRes = platform->createWindow(platform::WindowDesc{.title = "Hikari Engine (C++20)",
                                                                  .width = windowWidth,
                                                                  .height = windowHeight,
                                                                  .vulkan = true});
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
    render::registerRenderComponents(world);

    script::ScriptEngine scriptEngine;
    (void)scriptEngine.initialize();
    scriptEngine.bindWorld(&world);
    // Standard entity update hook for editor / game scripts
    (void)scriptEngine.executeString(R"(
function on_update(entity, dt)
    -- Default Lua entity tick handler
end
)");
    script::registerScriptComponents(world);
    script::registerScriptSystems(world, scriptEngine);

    ui::UiSystem uiSystem;
    (void)uiSystem.initialize({.width = 1280, .height = 720}, *rhiDevice);

    const std::string_view kMainHudRml = R"(
<rml>
<head>
<title>Hikari Diagnostics HUD</title>
<style>
body {
    font-family: Arial;
    font-size: 14px;
    color: #ffffff;
    margin: 0;
    padding: 0;
}
.hud-banner {
    position: absolute;
    top: 20px;
    left: 20px;
    width: 320px;
    height: 48px;
    background-color: #1a2538;
}
.hud-bar {
    position: absolute;
    bottom: 24px;
    left: 20px;
    width: 260px;
    height: 24px;
    background-color: #2b7a3d;
}
</style>
</head>
<body>
<div class="hud-banner"></div>
<div class="hud-bar"></div>
</body>
</rml>
)";
    auto hudRes = uiSystem.loadDocumentFromMemory(kMainHudRml, "main_hud");
    if (hudRes.hasValue()) {
        uiSystem.showDocument(hudRes.value());
    }

    // 6. Two-State Conductor
    auto modeA = std::make_unique<StateAMode>();
    auto modeB = std::make_unique<StateBMode>();
    framework::Conductor conductor(world, std::move(modeA), std::move(modeB));
    auto initRes = conductor.initialize();
    if (!initRes.hasValue()) {
        ENG_LOG_CRITICAL("Conductor", "Failed to initialize Two-State Conductor");
        return 1;
    }

#if defined(HIKARI_BUILD_EDITOR)
    editor::EditorEngine editorEngine;
    if (editorEngine.initialize(*rhiDevice)) {
        editorEngine.bindWorld(&world);
        editorEngine.bindConductor(&conductor);
        ENG_LOG_INFO("Editor",
                     "EditorEngine successfully initialized and bound to World & Conductor");
    }
#endif

    ENG_LOG_INFO("Core", "=== Initialization Complete. Entering Main Loop ===");

    // 7. Authoritative Simulation Frame Loop
    uint32_t frameCount = 0;
    bool running = true;
    const Duration fixedStep = Duration::fromMilliseconds(16);
    uint32_t lastWindowWidth = window ? window->getWidth() : 1280;
    uint32_t lastWindowHeight = window ? window->getHeight() : 720;

    while (running) {
        platform->pollEvents();
        const auto& input = platform->getCurrentInput();
        const auto& prevInput = platform->getPreviousInput();

        if (window && window->shouldClose()) {
            running = false;
            break;
        }

        uint32_t currentWidth = window ? window->getWidth() : 1280;
        uint32_t currentHeight = window ? window->getHeight() : 720;

        if (window && !window->isMinimized()) {
            if (currentWidth != lastWindowWidth || currentHeight != lastWindowHeight) {
                (void)rhiDevice->resize(currentWidth, currentHeight);
                uiSystem.resize(currentWidth, currentHeight);
                lastWindowWidth = currentWidth;
                lastWindowHeight = currentHeight;
            }
        }

        // Test mode switch every 30 frames in headless
        if (headless && frameCount > 0 && (frameCount % 30) == 0) {
            if (conductor.getState() == framework::ConductorState::Active) {
                (void)conductor.requestTransition();
            } else if (conductor.getState() == framework::ConductorState::Prepared) {
                (void)conductor.executeTransition();
            }
        }

        // 1. Swapchain image acquisition (must precede recording passes that target it)
        if (window) {
            (void)rhiDevice->acquireNextImage();
        }

        // 2. Process Input for UI and Editor
        uiSystem.processInput(input, prevInput);
#if defined(HIKARI_BUILD_EDITOR)
        if (editorEngine.isInitialized()) {
            editorEngine.processInput(input);
            editorEngine.beginFrame(fixedStep, currentWidth, currentHeight);
            editorEngine.drawPanels();
        }
#endif

        // 3. Update Conductor, World, and UI clocks
#if defined(HIKARI_BUILD_EDITOR)
        if (editorEngine.isInitialized()) {
            editorEngine.getRuntimeSession().update(fixedStep);
        } else {
            conductor.update(fixedStep);
            world.progress(static_cast<float>(fixedStep.toSeconds()));
        }
#else
        conductor.update(fixedStep);
        world.progress(static_cast<float>(fixedStep.toSeconds()));
#endif
        uiSystem.update(fixedStep);

        // 4. Record Frame Rendering Commands
        mainCmd->begin();

        rhi::RenderPassBeginInfo passInfo{
            .clearColor = {0.08F, 0.12F, 0.20F, 1.0F}, // Deep Hikari Blue background baseline
            .clearTarget = true,
        };
        mainCmd->beginRenderPass(passInfo);

        // Visible diagnostic mode indicator rectangle at top right
        // Blue in State A, Amber in State B, Magenta during transition
        float modeColor[4] = {0.2F, 0.5F, 0.9F, 1.0F};
        if (conductor.getState() == framework::ConductorState::Transitioning) {
            modeColor[0] = 0.8F;
            modeColor[1] = 0.2F;
            modeColor[2] = 0.8F;
        } else if (conductor.getActiveSlot() == framework::ActiveSlot::SlotB) {
            modeColor[0] = 0.9F;
            modeColor[1] = 0.6F;
            modeColor[2] = 0.1F;
        }
        mainCmd->clearRect(1000, 20, 240, 48, modeColor[0], modeColor[1], modeColor[2], 1.0F);

        // Render runtime UI
        uiSystem.render(*mainCmd);

#if defined(HIKARI_BUILD_EDITOR)
        if (editorEngine.isInitialized()) {
            editorEngine.endFrame(*mainCmd);
        }
#endif

        mainCmd->endRenderPass();
        mainCmd->end();

        // 5. Submit recorded commands to GPU queue
        (void)rhiDevice->submit(mainCmd.get());

        // 6. Present rendered swapchain image to display
        if (window) {
            (void)rhiDevice->present();
        }

        frameCount++;

        // Periodic telemetry logging (on frame 1 and every 300 frames)
        if (frameCount == 1 || (frameCount % 300) == 0) {
            const auto& uiStats = uiSystem.renderStats();
            uint32_t editorDraws = 0;
            uint32_t editorVerts = 0;
            uint32_t editorIdxs = 0;
            uint64_t editorPasses = 0;
#if defined(HIKARI_BUILD_EDITOR)
            if (editorEngine.isInitialized()) {
                editorDraws = editorEngine.lastDrawCallCount();
                editorVerts = editorEngine.lastVertexCount();
                editorIdxs = editorEngine.lastIndexCount();
                editorPasses = editorEngine.executedEditorPasses();
            }
#endif
            ENG_LOG_FORMAT(LogLevel::Info, "Render",
                           "Frame %u Telemetry: UI Draws: %u (Tris: %u), Editor Draws: %u (Verts: "
                           "%u, Idxs: %u, Passes: %llu)",
                           frameCount, uiStats.drawCalls, uiStats.triangleCount, editorDraws,
                           editorVerts, editorIdxs, static_cast<unsigned long long>(editorPasses));
        }

        if (targetFrames > 0 && frameCount >= targetFrames) {
            running = false;
        }
    }

    ENG_LOG_FORMAT(LogLevel::Info, "Core",
                   "=== Simulation loop ended after %u frames. Tearing down... ===", frameCount);

    // 8. Graceful Reverse Shutdown
    conductor.shutdown();
#if defined(HIKARI_BUILD_EDITOR)
    editorEngine.shutdown();
#endif
    uiSystem.shutdown();
    scriptEngine.unbindWorld();
    scriptEngine.shutdown();

    // Command lists must be destroyed before the device that allocated them
    mainCmd.reset();

    // RHI device must be destroyed before the window and surface it renders to
    rhiDevice.reset();

    // Window must be destroyed before platform/SDL shutdown
    window.reset();

    platform->shutdown();

    Logger::defaultLogger().flush(stdout);
    std::cout << "Hikari simulation completed cleanly (" << frameCount << " frames).\n";
    return 0;
}
