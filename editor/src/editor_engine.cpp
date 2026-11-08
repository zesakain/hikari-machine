/// @file editor_engine.cpp
/// @brief Dear ImGui developer workspace, docking engine, and Vulkan dynamic rendering backend.

#include "editor/editor_engine.h"

#include "core/assert.h"
#include "core/log.h"
#include "editor/editor_theme.h"
#include "editor/hierarchy_panel.h"
#include "editor/selection_model.h"
#include "editor/workspace_manager.h"
#include "panels/conductor_panel.h"
#include "panels/console_panel.h"
#include "panels/content_browser_panel.h"
#include "panels/inspector_panel.h"
#include "panels/metrics_panel.h"
#include "panels/viewport_panel.h"
#include "platform/input_snapshot.h"
#include "rhi/command_list.h"
#include "rhi/rhi_device.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.h>

namespace eng::editor {

struct EditorEngine::Impl {
    ImGuiContext* imguiContext{nullptr};
    rhi::RhiDevice* rhiDevice{nullptr};

    EditorConfig config{};
    EditorContext context{};
    WorkspaceManager workspaceManager{};
    SelectionModel selectionModel{};

    bool initialized{false};
    bool vulkanBackendInitialized{false};
};

EditorEngine::EditorEngine() : impl_(std::make_unique<Impl>()) {}

EditorEngine::~EditorEngine() {
    shutdown();
}

EditorEngine::EditorEngine(EditorEngine&& other) noexcept = default;
EditorEngine& EditorEngine::operator=(EditorEngine&& other) noexcept = default;

bool EditorEngine::initialize(rhi::RhiDevice& rhiDevice, const EditorConfig& config) {
    if (impl_->initialized) {
        return false;
    }

    impl_->rhiDevice = &rhiDevice;
    impl_->config = config;
    impl_->context.rhiDevice = &rhiDevice;
    impl_->context.selection = &impl_->selectionModel;

    IMGUI_CHECKVERSION();
    impl_->imguiContext = ImGui::CreateContext();
    if (impl_->imguiContext == nullptr) {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Dear ImGui Docking System
    io.IniFilename = nullptr;                         // Managed explicitly by WorkspaceManager
    io.DisplaySize = ImVec2(1280.0F, 720.0F);
    io.DeltaTime = 1.0F / 60.0F;

    // Apply official Hikari dark graphite theme and desktop typography
    applyEditorTheme(ThemeColors::defaultDark());
    (void)loadEditorFonts(config.fontScale);

    // Register all standard panels into WorkspaceManager
    impl_->workspaceManager.registerPanel(std::make_unique<HierarchyPanel>());
    impl_->workspaceManager.registerPanel(std::make_unique<InspectorPanel>());
    impl_->workspaceManager.registerPanel(std::make_unique<ViewportPanel>());
    impl_->workspaceManager.registerPanel(std::make_unique<ConsolePanel>());
    impl_->workspaceManager.registerPanel(std::make_unique<MetricsPanel>());
    impl_->workspaceManager.registerPanel(std::make_unique<ConductorPanel>());
    impl_->workspaceManager.registerPanel(std::make_unique<ContentBrowserPanel>());

    impl_->workspaceManager.setLayoutFilePath(config.layoutFilePath);

    // Build default font atlas (required for headless / null RHI execution)
    unsigned char* fontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);

    auto nativeHandles = rhiDevice.getNativeDeviceHandles();
    if (nativeHandles.device != nullptr) {
        auto colorFormat = static_cast<VkFormat>(nativeHandles.colorAttachmentFormat);
        VkPipelineRenderingCreateInfoKHR renderingInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
            .pNext = nullptr,
            .viewMask = 0,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &colorFormat,
            .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
            .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
        };

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.Instance = static_cast<VkInstance>(nativeHandles.instance);
        initInfo.PhysicalDevice = static_cast<VkPhysicalDevice>(nativeHandles.physicalDevice);
        initInfo.Device = static_cast<VkDevice>(nativeHandles.device);
        initInfo.QueueFamily = nativeHandles.graphicsQueueFamily;
        initInfo.Queue = static_cast<VkQueue>(nativeHandles.graphicsQueue);
        initInfo.DescriptorPoolSize = 64;
        initInfo.MinImageCount = 2;
        initInfo.ImageCount = std::max(2U, nativeHandles.swapchainImageCount);
        initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.UseDynamicRendering = true;
        initInfo.PipelineRenderingCreateInfo = renderingInfo;

        if (!ImGui_ImplVulkan_Init(&initInfo)) {
            ENG_LOG_ERROR("Editor", "Failed to initialize ImGui Vulkan backend");
            ImGui::DestroyContext(impl_->imguiContext);
            impl_->imguiContext = nullptr;
            return false;
        }
        impl_->vulkanBackendInitialized = true;
        ENG_LOG_INFO("Editor", "Dear ImGui Vulkan backend initialized with Dynamic Rendering");
    } else {
        ENG_LOG_INFO("Editor", "Dear ImGui initialized in headless/null RHI mode");
    }

    // Attempt to restore layout from persistence file
    (void)impl_->workspaceManager.loadLayout();

    impl_->initialized = true;
    ENG_LOG_INFO("Editor", "Hikari Editor Workspace initialized successfully");
    return true;
}

void EditorEngine::shutdown() {
    if (impl_->initialized) {
        if (impl_->config.autoSaveLayout) {
            (void)impl_->workspaceManager.saveLayout();
        }

        if (impl_->vulkanBackendInitialized) {
            ImGui_ImplVulkan_Shutdown();
            impl_->vulkanBackendInitialized = false;
        }
        if (impl_->imguiContext != nullptr) {
            ImGui::DestroyContext(impl_->imguiContext);
            impl_->imguiContext = nullptr;
        }
        impl_->rhiDevice = nullptr;
        impl_->context.world = nullptr;
        impl_->context.conductor = nullptr;
        impl_->context.rhiDevice = nullptr;
        impl_->initialized = false;
        ENG_LOG_INFO("Editor", "Hikari Editor Workspace shut down cleanly");
    }
}

bool EditorEngine::isInitialized() const noexcept {
    return impl_->initialized;
}

void EditorEngine::beginFrame(Duration dt, uint32_t width, uint32_t height) {
    if (!impl_->initialized) {
        return;
    }

    ImGui::SetCurrentContext(impl_->imguiContext);
    ImGuiIO& io = ImGui::GetIO();

    if (width > 0 && height > 0) {
        io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
        impl_->context.windowWidth = width;
        impl_->context.windowHeight = height;
    }

    float deltaSec = static_cast<float>(dt.toSeconds());
    io.DeltaTime = deltaSec > 0.0F ? deltaSec : (1.0F / 60.0F);

    impl_->context.lastFrameTimeMs = io.DeltaTime * 1000.0F;
    impl_->context.currentFps = io.Framerate > 0.0F ? io.Framerate : 60.0F;

    // Validate and synchronize entity selection with authoritative World
    if (impl_->context.world != nullptr) {
        impl_->selectionModel.validate(*impl_->context.world);
    }
    impl_->context.selectedEntityId = impl_->selectionModel.getPrimarySelected().id;

    if (impl_->vulkanBackendInitialized) {
        ImGui_ImplVulkan_NewFrame();
    }
    ImGui::NewFrame();
}

void EditorEngine::drawPanels() {
    if (!impl_->initialized) {
        return;
    }

    ImGui::SetCurrentContext(impl_->imguiContext);
    impl_->workspaceManager.drawWorkspace(impl_->context);

    if (impl_->config.showDemoWindow) {
        ImGui::ShowDemoWindow(&impl_->config.showDemoWindow);
    }
}

void EditorEngine::endFrame(rhi::RhiCommandList& cmdList) {
    if (!impl_->initialized) {
        return;
    }

    ImGui::SetCurrentContext(impl_->imguiContext);
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData == nullptr) {
        return;
    }

    impl_->context.lastDrawCalls = 0;
    impl_->context.lastVertices = static_cast<uint32_t>(drawData->TotalVtxCount);
    impl_->context.lastIndices = static_cast<uint32_t>(drawData->TotalIdxCount);
    for (int n = 0; n < drawData->CmdListsCount; n++) {
        impl_->context.lastDrawCalls +=
            static_cast<uint32_t>(drawData->CmdLists[n]->CmdBuffer.Size);
    }
    impl_->context.executedPasses++;

    if (impl_->vulkanBackendInitialized) {
        auto nativeCmd = cmdList.getNativeCommandListHandles();
        if (nativeCmd.commandBuffer != nullptr) {
            ImGui_ImplVulkan_RenderDrawData(drawData,
                                            static_cast<VkCommandBuffer>(nativeCmd.commandBuffer));
        }
    }
}

void EditorEngine::processInput(const platform::InputSnapshot& input) {
    if (!impl_->initialized) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(input.mouseX, input.mouseY);
    io.AddMouseButtonEvent(
        0, input.mouseButtons.test(static_cast<size_t>(platform::MouseButton::Left)));
    io.AddMouseButtonEvent(
        1, input.mouseButtons.test(static_cast<size_t>(platform::MouseButton::Right)));
    io.AddMouseButtonEvent(
        2, input.mouseButtons.test(static_cast<size_t>(platform::MouseButton::Middle)));
    io.AddMouseWheelEvent(0.0F, input.wheelDeltaY);
}

void EditorEngine::bindWorld(world::World* world) noexcept {
    impl_->context.world = world;
}

void EditorEngine::bindConductor(framework::Conductor* conductor) noexcept {
    impl_->context.conductor = conductor;
}

WorkspaceManager& EditorEngine::getWorkspaceManager() noexcept {
    return impl_->workspaceManager;
}

const WorkspaceManager& EditorEngine::getWorkspaceManager() const noexcept {
    return impl_->workspaceManager;
}

SelectionModel& EditorEngine::getSelectionModel() noexcept {
    return impl_->selectionModel;
}

const SelectionModel& EditorEngine::getSelectionModel() const noexcept {
    return impl_->selectionModel;
}

bool EditorEngine::saveWorkspaceLayout(const char* filepath) {
    return impl_->workspaceManager.saveLayout(filepath);
}

bool EditorEngine::loadWorkspaceLayout(const char* filepath) {
    return impl_->workspaceManager.loadLayout(filepath);
}

void EditorEngine::resetWorkspaceLayout() noexcept {
    impl_->workspaceManager.requestLayoutReset();
}

uint32_t EditorEngine::lastDrawCallCount() const noexcept {
    return impl_->context.lastDrawCalls;
}

uint32_t EditorEngine::lastVertexCount() const noexcept {
    return impl_->context.lastVertices;
}

uint32_t EditorEngine::lastIndexCount() const noexcept {
    return impl_->context.lastIndices;
}

uint64_t EditorEngine::executedEditorPasses() const noexcept {
    return impl_->context.executedPasses;
}

} // namespace eng::editor
