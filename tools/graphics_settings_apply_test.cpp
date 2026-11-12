/// @file graphics_settings_apply_test.cpp
/// @brief Regression test verifying crash-free Graphics Settings Apply operations mid-frame.
///
/// Invariants & Verification:
/// - Verifies that applying graphics settings (shadow resolution, VSync, tone mapping,
///   bloom, quality presets) while Dear ImGui and Viewport panels are actively rendering
///   does not crash or dereference freed Vulkan descriptor sets.
/// - Validates deferred descriptor cleanup and swapchain recreation at frame boundaries.

#include "core/log.h"
#include "editor/editor_engine.h"
#include "editor/workspace_manager.h"
#include "panels/settings_panel.h"
#include "platform/platform.h"
#include "render/graphics_settings.h"
#include "render/scene_renderer.h"
#include "rhi/command_list.h"
#include "rhi/rhi_device.h"
#include "world/world.h"

#include <iostream>
#include <memory>
#include <vector>

using namespace eng;

int main() {
    std::cout << "Starting Graphics Settings Apply stability regression test...\n";

    platform::PlatformConfig platConfig{};
    auto platRes = platform::createPlatform(platConfig);
    if (!platRes.hasValue()) {
        std::cerr << "Failed to create platform: " << platRes.error().message << '\n';
        return 1;
    }
    auto platform = std::move(platRes.value());
    auto winRes = platform->createWindow(platform::WindowDesc{
        .title = "Hikari Settings Apply Regression Test",
        .width = 1280,
        .height = 720,
        .vulkan = true,
    });
    if (!winRes.hasValue()) {
        std::cerr << "Failed to create window: " << winRes.error().message << '\n';
        return 1;
    }
    auto window = std::move(winRes.value());

    rhi::VulkanDeviceDesc vkDesc{
        .windowHandle = window ? window->getNativeHandle() : nullptr,
        .enableValidation = true,
    };
    auto devRes = rhi::createVulkanDevice(vkDesc);
    if (!devRes.hasValue()) {
        std::cerr << "Failed to create Vulkan device: " << devRes.error().message << '\n';
        return 1;
    }
    auto device = std::move(devRes.value());

    editor::EditorEngine editor;
    if (!editor.initialize(*device, {.showDemoWindow = false})) {
        std::cerr << "Failed to initialize EditorEngine\n";
        return 1;
    }

    world::World world;
    editor.bindWorld(&world);

    auto* sp =
        static_cast<editor::SettingsPanel*>(editor.getWorkspaceManager().findPanel("settings"));
    if (sp != nullptr) {
        sp->setOpen(true);
    }

    auto cmdRes = device->createCommandList();
    if (!cmdRes.hasValue()) {
        std::cerr << "Failed to create command list\n";
        return 1;
    }
    auto cmd = std::move(cmdRes.value());

    // Execute 15 frames testing various graphics settings apply operations mid-frame
    for (uint32_t frame = 0; frame < 15; ++frame) {
        (void)device->acquireNextImage();
        editor.beginFrame(Duration::fromMilliseconds(16), 1280, 720);

        editor.drawPanels();

        // Simulate SettingsPanel::drawFooter() clicking "Apply" across frames
        if (frame == 1) {
            auto s = editor.getGraphicsSettings();
            s.shadows.resolution = 4096;
            bool ok = editor.applyGraphicsSettings(s);
            if (!ok) {
                std::cerr << "Failed to apply shadow resolution\n";
                return 1;
            }
        } else if (frame == 2) {
            auto s = editor.getGraphicsSettings();
            s.performance.vsync = !s.performance.vsync;
            bool ok = editor.applyGraphicsSettings(s);
            if (!ok) {
                std::cerr << "Failed to toggle VSync\n";
                return 1;
            }
        } else if (frame == 3) {
            auto s = editor.getGraphicsSettings();
            s.postProcess.toneMapOp = render::ToneMapOperator::FilmicReinhard;
            s.postProcess.bloomIntensity = 0.2F;
            bool ok = editor.applyGraphicsSettings(s);
            if (!ok) {
                std::cerr << "Failed to apply post process settings\n";
                return 1;
            }
        } else if (frame == 4) {
            bool ok = editor.resizeViewport(1920, 1080);
            if (!ok) {
                std::cerr << "Failed to resize viewport\n";
                return 1;
            }
        } else if (frame == 5) {
            auto s = editor.getGraphicsSettings();
            s.shadows.resolution = 1024;
            bool ok = editor.applyGraphicsSettings(s);
            if (!ok) {
                std::cerr << "Failed to apply shadow resolution\n";
                return 1;
            }
        } else if (frame == 6) {
            auto s = editor.getGraphicsSettings();
            s.performance.vsync = !s.performance.vsync;
            bool ok = editor.applyGraphicsSettings(s);
            if (!ok) {
                std::cerr << "Failed to toggle VSync back\n";
                return 1;
            }
        } else if (frame == 7) {
            auto s = render::GraphicsSettings::createPreset(render::GraphicsQualityPreset::Ultra);
            bool ok = editor.applyGraphicsSettings(s);
            if (!ok) {
                std::cerr << "Failed to apply Ultra preset\n";
                return 1;
            }
        }

        cmd->begin();
        editor.renderScene(*cmd);

        rhi::RenderPassBeginInfo passInfo{
            .clearColor = {0.08F, 0.12F, 0.20F, 1.0F},
            .clearTarget = true,
        };
        cmd->beginRenderPass(passInfo);
        editor.endFrame(*cmd);
        cmd->endRenderPass();
        cmd->end();

        (void)device->submit(cmd.get());
        (void)device->present();
    }

    editor.shutdown();
    std::cout
        << "[PASS] Graphics Settings Apply stability regression test completed successfully!\n";
    return 0;
}
