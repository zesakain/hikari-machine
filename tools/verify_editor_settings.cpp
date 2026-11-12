/// @file verify_editor_settings.cpp
/// @brief Offscreen GPU capture and verification tool for unified Editor Settings & Preferences.

#include "core/log.h"
#include "editor/editor_engine.h"
#include "editor/workspace_manager.h"
#include "panels/settings_panel.h"
#include "physics/physics_components.h"
#include "rhi/command_list.h"
#include "rhi/rhi_device.h"
#include "world/world.h"

#include <chrono>
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
    std::cout << " Hikari Editor Settings & Preferences Verification Tool \n";
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
        .debugName = "EditorSettingsColorTarget",
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
        .debugName = "EditorSettingsReadbackBuffer",
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

    auto* settingsPanel =
        static_cast<editor::SettingsPanel*>(editor.getWorkspaceManager().findPanel("settings"));
    if (settingsPanel == nullptr) {
        std::cerr << "SettingsPanel not found in WorkspaceManager registry!\n";
        return 1;
    }

    auto renderAndCapture = [&](editor::SettingsCategory cat, const std::string& filename) {
        settingsPanel->openToCategory(cat);

        for (uint32_t f = 0; f < 5; ++f) {
            editor.beginFrame(Duration::fromMilliseconds(16), width, height);
            editor.drawPanels();

            auto cmdRes = device->createCommandList();
            if (!cmdRes.hasValue())
                continue;
            auto cmd = std::move(cmdRes.value());

            cmd->begin();
            editor.renderScene(*cmd);

            rhi::RenderPassBeginInfo passInfo{
                .colorTarget = rtHandle,
                .clearColor = {0.12F, 0.14F, 0.18F, 1.0F},
                .clearTarget = true,
            };
            cmd->beginRenderPass(passInfo);
            editor.endFrame(*cmd);
            cmd->endRenderPass();

            if (f == 4) {
                cmd->copyTextureToBuffer(rtHandle, rbHandle);
            }

            cmd->end();
            (void)device->submit(cmd.get());
            device->waitIdle();
        }

        std::vector<uint8_t> pxBuffer(byteSize, 0);
        auto copyRes = device->readBuffer(rbHandle, 0, pxBuffer.data(), byteSize);
        if (copyRes.hasValue()) {
            savePpmBgra(outDir + "/" + filename, pxBuffer.data(), width, height);
        }
    };

    std::cout << "\n>>> Capturing SettingsPanel: User Preferences (General) <<<\n";
    renderAndCapture(editor::SettingsCategory::General, "settings_user_general.ppm");

    std::cout << "\n>>> Capturing SettingsPanel: User Preferences (Appearance & Theme) <<<\n";
    renderAndCapture(editor::SettingsCategory::Appearance, "settings_user_appearance.ppm");

    std::cout << "\n>>> Capturing SettingsPanel: User Preferences (Scripting IDE) <<<\n";
    renderAndCapture(editor::SettingsCategory::Scripting, "settings_user_scripting.ppm");

    std::cout << "\n>>> Capturing SettingsPanel: Project Settings (Graphics Quality) <<<\n";
    renderAndCapture(editor::SettingsCategory::GraphicsQuality, "settings_project_quality.ppm");

    std::cout << "\n>>> Capturing SettingsPanel: Project Settings (Post-Processing) <<<\n";
    renderAndCapture(editor::SettingsCategory::GraphicsPostProcess,
                     "settings_project_postprocess.ppm");

    editor.shutdown();
    device->destroyBuffer(rbHandle);
    device->destroyTexture(rtHandle);

    std::cout << "\nAll settings verification captures completed successfully!\n";
    return 0;
}
