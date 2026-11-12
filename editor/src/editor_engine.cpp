/// @file editor_engine.cpp
/// @brief Dear ImGui developer workspace, docking engine, and Vulkan dynamic rendering backend.

#include "editor/editor_engine.h"

#include "core/assert.h"
#include "core/log.h"
#include "editor/editor_theme.h"
#include "editor/hierarchy_panel.h"
#include "editor/selection_model.h"
#include "editor/workspace_manager.h"
#include "framework/runtime_session.h"
#include "panels/conductor_panel.h"
#include "panels/console_panel.h"
#include "panels/content_browser_panel.h"
#include "panels/inspector_panel.h"
#include "panels/metrics_panel.h"
#include "panels/viewport_panel.h"
#include "platform/input_snapshot.h"
#include "render/asset_manager.h"
#include "render/scene_renderer.h"
#include "rhi/command_list.h"
#include "rhi/rhi_device.h"
#include "rhi/sampler.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <bitset>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <string>
#include <vulkan/vulkan.h>

namespace eng::editor {

namespace {

// Shared in-memory clipboard buffer fallback for headless/null RHI execution
std::string s_headlessClipboard{};

/// Maps platform hardware-neutral KeyCode to Dear ImGui key identifier.
[[nodiscard]] constexpr ImGuiKey toImGuiKey(platform::KeyCode key) noexcept {
    switch (key) {
    case platform::KeyCode::Tab:
        return ImGuiKey_Tab;
    case platform::KeyCode::Left:
        return ImGuiKey_LeftArrow;
    case platform::KeyCode::Right:
        return ImGuiKey_RightArrow;
    case platform::KeyCode::Up:
        return ImGuiKey_UpArrow;
    case platform::KeyCode::Down:
        return ImGuiKey_DownArrow;
    case platform::KeyCode::PageUp:
        return ImGuiKey_PageUp;
    case platform::KeyCode::PageDown:
        return ImGuiKey_PageDown;
    case platform::KeyCode::Home:
        return ImGuiKey_Home;
    case platform::KeyCode::End:
        return ImGuiKey_End;
    case platform::KeyCode::Insert:
        return ImGuiKey_Insert;
    case platform::KeyCode::Delete:
        return ImGuiKey_Delete;
    case platform::KeyCode::Backspace:
        return ImGuiKey_Backspace;
    case platform::KeyCode::Space:
        return ImGuiKey_Space;
    case platform::KeyCode::Return:
        return ImGuiKey_Enter;
    case platform::KeyCode::Escape:
        return ImGuiKey_Escape;
    case platform::KeyCode::Apostrophe:
        return ImGuiKey_Apostrophe;
    case platform::KeyCode::Comma:
        return ImGuiKey_Comma;
    case platform::KeyCode::Minus:
        return ImGuiKey_Minus;
    case platform::KeyCode::Period:
        return ImGuiKey_Period;
    case platform::KeyCode::Slash:
        return ImGuiKey_Slash;
    case platform::KeyCode::Semicolon:
        return ImGuiKey_Semicolon;
    case platform::KeyCode::Equals:
        return ImGuiKey_Equal;
    case platform::KeyCode::LeftBracket:
        return ImGuiKey_LeftBracket;
    case platform::KeyCode::Backslash:
        return ImGuiKey_Backslash;
    case platform::KeyCode::RightBracket:
        return ImGuiKey_RightBracket;
    case platform::KeyCode::Grave:
        return ImGuiKey_GraveAccent;
    case platform::KeyCode::CapsLock:
        return ImGuiKey_CapsLock;
    case platform::KeyCode::ScrollLock:
        return ImGuiKey_ScrollLock;
    case platform::KeyCode::PrintScreen:
        return ImGuiKey_PrintScreen;
    case platform::KeyCode::Pause:
        return ImGuiKey_Pause;
    case platform::KeyCode::LeftCtrl:
        return ImGuiKey_LeftCtrl;
    case platform::KeyCode::LeftShift:
        return ImGuiKey_LeftShift;
    case platform::KeyCode::LeftAlt:
        return ImGuiKey_LeftAlt;
    case platform::KeyCode::LeftGui:
        return ImGuiKey_LeftSuper;
    case platform::KeyCode::RightCtrl:
        return ImGuiKey_RightCtrl;
    case platform::KeyCode::RightShift:
        return ImGuiKey_RightShift;
    case platform::KeyCode::RightAlt:
        return ImGuiKey_RightAlt;
    case platform::KeyCode::RightGui:
        return ImGuiKey_RightSuper;
    case platform::KeyCode::Num0:
        return ImGuiKey_0;
    case platform::KeyCode::Num1:
        return ImGuiKey_1;
    case platform::KeyCode::Num2:
        return ImGuiKey_2;
    case platform::KeyCode::Num3:
        return ImGuiKey_3;
    case platform::KeyCode::Num4:
        return ImGuiKey_4;
    case platform::KeyCode::Num5:
        return ImGuiKey_5;
    case platform::KeyCode::Num6:
        return ImGuiKey_6;
    case platform::KeyCode::Num7:
        return ImGuiKey_7;
    case platform::KeyCode::Num8:
        return ImGuiKey_8;
    case platform::KeyCode::Num9:
        return ImGuiKey_9;
    case platform::KeyCode::A:
        return ImGuiKey_A;
    case platform::KeyCode::B:
        return ImGuiKey_B;
    case platform::KeyCode::C:
        return ImGuiKey_C;
    case platform::KeyCode::D:
        return ImGuiKey_D;
    case platform::KeyCode::E:
        return ImGuiKey_E;
    case platform::KeyCode::F:
        return ImGuiKey_F;
    case platform::KeyCode::G:
        return ImGuiKey_G;
    case platform::KeyCode::H:
        return ImGuiKey_H;
    case platform::KeyCode::I:
        return ImGuiKey_I;
    case platform::KeyCode::J:
        return ImGuiKey_J;
    case platform::KeyCode::K:
        return ImGuiKey_K;
    case platform::KeyCode::L:
        return ImGuiKey_L;
    case platform::KeyCode::M:
        return ImGuiKey_M;
    case platform::KeyCode::N:
        return ImGuiKey_N;
    case platform::KeyCode::O:
        return ImGuiKey_O;
    case platform::KeyCode::P:
        return ImGuiKey_P;
    case platform::KeyCode::Q:
        return ImGuiKey_Q;
    case platform::KeyCode::R:
        return ImGuiKey_R;
    case platform::KeyCode::S:
        return ImGuiKey_S;
    case platform::KeyCode::T:
        return ImGuiKey_T;
    case platform::KeyCode::U:
        return ImGuiKey_U;
    case platform::KeyCode::V:
        return ImGuiKey_V;
    case platform::KeyCode::W:
        return ImGuiKey_W;
    case platform::KeyCode::X:
        return ImGuiKey_X;
    case platform::KeyCode::Y:
        return ImGuiKey_Y;
    case platform::KeyCode::Z:
        return ImGuiKey_Z;
    case platform::KeyCode::F1:
        return ImGuiKey_F1;
    case platform::KeyCode::F2:
        return ImGuiKey_F2;
    case platform::KeyCode::F3:
        return ImGuiKey_F3;
    case platform::KeyCode::F4:
        return ImGuiKey_F4;
    case platform::KeyCode::F5:
        return ImGuiKey_F5;
    case platform::KeyCode::F6:
        return ImGuiKey_F6;
    case platform::KeyCode::F7:
        return ImGuiKey_F7;
    case platform::KeyCode::F8:
        return ImGuiKey_F8;
    case platform::KeyCode::F9:
        return ImGuiKey_F9;
    case platform::KeyCode::F10:
        return ImGuiKey_F10;
    case platform::KeyCode::F11:
        return ImGuiKey_F11;
    case platform::KeyCode::F12:
        return ImGuiKey_F12;
    default:
        return ImGuiKey_None;
    }
}

} // namespace

struct EditorEngine::Impl {
    ImGuiContext* imguiContext{nullptr};
    rhi::RhiDevice* rhiDevice{nullptr};

    EditorConfig config{};
    EditorContext context{};
    WorkspaceManager workspaceManager{};
    SelectionModel selectionModel{};
    render::AssetManager assetManager{};

    render::SceneRenderer sceneRenderer{};
    rhi::SamplerHandle viewportSampler{};
    VkDescriptorSet viewportDescriptorSet{VK_NULL_HANDLE};
    uint32_t viewportWidth{0};
    uint32_t viewportHeight{0};
    Mat4 viewportCameraViewProj{Mat4::identity()};

    framework::RuntimeSession internalRuntimeSession{};
    framework::RuntimeSession* runtimeSession{&internalRuntimeSession};

    std::bitset<static_cast<size_t>(platform::KeyCode::Count)> lastKeys{};
    std::string clipboardData{};

    bool initialized{false};
    bool vulkanBackendInitialized{false};
    framework::SimulationState lastSimulationState{framework::SimulationState::Edit};

    void updateViewportTextureRegistration() {
        if (!vulkanBackendInitialized || rhiDevice == nullptr) {
            return;
        }

        if (viewportDescriptorSet != VK_NULL_HANDLE) {
            rhiDevice->waitIdle();
            ImGui_ImplVulkan_RemoveTexture(viewportDescriptorSet);
            viewportDescriptorSet = VK_NULL_HANDLE;
            context.viewportTextureId = nullptr;
            context.gpuRenderingActive = false;
        }

        if (!sceneRenderer.isInitialized() || viewportWidth == 0 || viewportHeight == 0) {
            return;
        }

        auto colorTarget = sceneRenderer.getColorTarget();
        if (!colorTarget.isValid()) {
            return;
        }

        void* nativeView = rhiDevice->getNativeTextureView(colorTarget);
        void* nativeSampler = rhiDevice->getNativeSampler(viewportSampler);
        if (nativeView != nullptr && nativeSampler != nullptr) {
            viewportDescriptorSet = ImGui_ImplVulkan_AddTexture(
                static_cast<VkSampler>(nativeSampler), static_cast<VkImageView>(nativeView),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            context.viewportTextureId = static_cast<void*>(viewportDescriptorSet);
            context.gpuRenderingActive = (viewportDescriptorSet != VK_NULL_HANDLE);
        }
    }

    bool resizeViewport(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0) {
            return false;
        }
        if (width == viewportWidth && height == viewportHeight && sceneRenderer.isInitialized()) {
            return true;
        }

        if (rhiDevice == nullptr) {
            return false;
        }

        // Wait for GPU idle and remove previous descriptor before recreating render targets
        if (vulkanBackendInitialized && viewportDescriptorSet != VK_NULL_HANDLE) {
            rhiDevice->waitIdle();
            ImGui_ImplVulkan_RemoveTexture(viewportDescriptorSet);
            viewportDescriptorSet = VK_NULL_HANDLE;
            context.viewportTextureId = nullptr;
            context.gpuRenderingActive = false;
        }

        viewportWidth = width;
        viewportHeight = height;

        if (!sceneRenderer.isInitialized()) {
            auto initRes = sceneRenderer.init(*rhiDevice, viewportWidth, viewportHeight);
            if (!initRes.hasValue()) {
                ENG_LOG_ERROR("Editor", "Failed to initialize SceneRenderer for viewport");
                return false;
            }
        } else {
            auto resizeRes = sceneRenderer.resize(viewportWidth, viewportHeight);
            if (!resizeRes.hasValue()) {
                ENG_LOG_ERROR("Editor", "Failed to resize SceneRenderer for viewport");
                return false;
            }
        }

        updateViewportTextureRegistration();
        context.sceneRenderWidth = viewportWidth;
        context.sceneRenderHeight = viewportHeight;
        return true;
    }
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
    impl_->context.assetManager = &impl_->assetManager;
    impl_->context.selection = &impl_->selectionModel;
    impl_->context.editorEngine = this;

    IMGUI_CHECKVERSION();
    impl_->imguiContext = ImGui::CreateContext();
    if (impl_->imguiContext == nullptr) {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Dear ImGui Docking System
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Master keyboard navigation & activation
    io.IniFilename = nullptr;                             // Managed explicitly by WorkspaceManager
    io.DisplaySize = ImVec2(1280.0F, 720.0F);
    io.DeltaTime = 1.0F / 60.0F;

    // Configure OS and fallback clipboard callbacks for text editing and shortcuts
    io.ClipboardUserData = impl_.get();
    io.GetClipboardTextFn = [](void*) -> const char* {
        const char* sdlText = SDL_GetClipboardText();
        if (sdlText != nullptr && sdlText[0] != '\0') {
            return sdlText;
        }
        return s_headlessClipboard.c_str();
    };
    io.SetClipboardTextFn = [](void*, const char* text) {
        if (text != nullptr) {
            SDL_SetClipboardText(text);
            s_headlessClipboard = text;
        }
    };

    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    platformIo.Platform_SetImeDataFn = [](ImGuiContext*, ImGuiViewport* viewport,
                                          ImGuiPlatformImeData* data) {
        if (data == nullptr || viewport == nullptr) {
            return;
        }
        SDL_Window* window = SDL_GetKeyboardFocus();
        if (window != nullptr && data->WantVisible) {
            SDL_Rect r{
                .x = static_cast<int>(data->InputPos.x - viewport->Pos.x),
                .y = static_cast<int>(data->InputPos.y - viewport->Pos.y + data->InputLineHeight),
                .w = 1,
                .h = static_cast<int>(data->InputLineHeight),
            };
            SDL_SetTextInputArea(window, &r, 0);
            SDL_StartTextInput(window);
        }
    };

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
        if (colorFormat == VK_FORMAT_UNDEFINED) {
            colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
        }
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

    impl_->context.sceneRenderer = &impl_->sceneRenderer;

    // Enable HDR, ACES Tone Mapping and Bloom by default in Editor
    render::PostProcessSettings ppSettings{};
    ppSettings.enabled = true;
    ppSettings.exposure = 1.0F;
    ppSettings.toneMapOp = render::ToneMapOperator::AcesFitted;
    ppSettings.bloomEnabled = true;
    ppSettings.bloomThreshold = 1.0F;
    ppSettings.bloomKnee = 0.5F;
    ppSettings.bloomIntensity = 0.04F;
    ppSettings.bloomFilterRadius = 1.0F;
    impl_->sceneRenderer.setPostProcessSettings(ppSettings);

    // Create linear clamp-to-edge sampler for the editor viewport
    rhi::SamplerDesc samplerDesc{
        .minFilter = rhi::Filter::Linear,
        .magFilter = rhi::Filter::Linear,
        .mipmapMode = rhi::Filter::Linear,
        .addressU = rhi::SamplerAddressMode::ClampToEdge,
        .addressV = rhi::SamplerAddressMode::ClampToEdge,
        .addressW = rhi::SamplerAddressMode::ClampToEdge,
        .compareEnable = false,
        .debugName = "EditorViewportSampler",
    };
    auto sRes = rhiDevice.createSampler(samplerDesc);
    if (sRes.hasValue()) {
        impl_->viewportSampler = sRes.value();
    }

    // Initialize viewport renderer with initial baseline dimensions
    (void)impl_->resizeViewport(1280, 720);

    impl_->initialized = true;
    ENG_LOG_INFO("Editor", "Hikari Editor Workspace initialized successfully");
    return true;
}

void EditorEngine::shutdown() {
    if (impl_->initialized) {
        if (impl_->config.autoSaveLayout) {
            (void)impl_->workspaceManager.saveLayout();
        }

        if (impl_->runtimeSession != nullptr && impl_->runtimeSession->isSimulating()) {
            (void)impl_->runtimeSession->stop();
        }

        if (impl_->rhiDevice != nullptr) {
            impl_->rhiDevice->waitIdle();
        }

        if (impl_->vulkanBackendInitialized) {
            if (impl_->viewportDescriptorSet != VK_NULL_HANDLE) {
                ImGui_ImplVulkan_RemoveTexture(impl_->viewportDescriptorSet);
                impl_->viewportDescriptorSet = VK_NULL_HANDLE;
            }
            ImGui_ImplVulkan_Shutdown();
            impl_->vulkanBackendInitialized = false;
        }

        impl_->sceneRenderer.shutdown();
        impl_->assetManager.unloadAll();

        if (impl_->rhiDevice != nullptr && impl_->viewportSampler.isValid()) {
            impl_->rhiDevice->destroySampler(impl_->viewportSampler);
            impl_->viewportSampler = {};
        }

        if (impl_->imguiContext != nullptr) {
            ImGui::DestroyContext(impl_->imguiContext);
            impl_->imguiContext = nullptr;
        }
        impl_->rhiDevice = nullptr;
        impl_->context.world = nullptr;
        impl_->context.conductor = nullptr;
        impl_->context.rhiDevice = nullptr;
        impl_->context.sceneRenderer = nullptr;
        impl_->context.viewportTextureId = nullptr;
        impl_->context.gpuRenderingActive = false;
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

    impl_->context.runtimeSession = impl_->runtimeSession;
    if (impl_->runtimeSession != nullptr) {
        auto currentSimState = impl_->runtimeSession->getSimulationState();
        if (currentSimState != impl_->lastSimulationState) {
            impl_->selectionModel.clear();
            impl_->lastSimulationState = currentSimState;
        }

        impl_->context.simulationState = currentSimState;
        impl_->context.isRuntimeActive = impl_->runtimeSession->isSimulating();
        impl_->context.isSimulating = impl_->runtimeSession->isSimulating();
        impl_->context.isPaused = (currentSimState == framework::SimulationState::Paused);
        impl_->context.world = impl_->runtimeSession->getActiveWorld();
        impl_->context.conductor = impl_->runtimeSession->getActiveConductor();
    }

    // Validate and synchronize entity selection with active World
    if (impl_->context.world != nullptr) {
        impl_->selectionModel.validate(*impl_->context.world);
    }
    impl_->context.selectedEntityId = impl_->selectionModel.getPrimarySelected().id;

    impl_->context.editorEngine = this;
    impl_->context.sceneRenderer = &impl_->sceneRenderer;
    impl_->context.viewportTextureId = static_cast<void*>(impl_->viewportDescriptorSet);
    impl_->context.sceneRenderWidth = impl_->viewportWidth;
    impl_->context.sceneRenderHeight = impl_->viewportHeight;
    impl_->context.gpuRenderingActive = (impl_->viewportDescriptorSet != VK_NULL_HANDLE);
    impl_->context.requestViewportResize = false;

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
    impl_->context.requestViewportResize = false;
    impl_->workspaceManager.drawWorkspace(impl_->context);
    impl_->viewportCameraViewProj = impl_->context.viewportCameraViewProj;

    if (impl_->config.showDemoWindow) {
        ImGui::ShowDemoWindow(&impl_->config.showDemoWindow);
    }
}

bool EditorEngine::resizeViewport(uint32_t width, uint32_t height) {
    if (!impl_->initialized) {
        return false;
    }
    return impl_->resizeViewport(width, height);
}

void EditorEngine::renderScene(rhi::RhiCommandList& cmdList) {
    if (!impl_->initialized || impl_->rhiDevice == nullptr ||
        !impl_->sceneRenderer.isInitialized()) {
        return;
    }
    if (impl_->viewportWidth == 0 || impl_->viewportHeight == 0) {
        return;
    }
    world::World* targetWorld = impl_->context.world;
    if (targetWorld == nullptr) {
        return;
    }

    (void)impl_->sceneRenderer.renderScene(*targetWorld, impl_->assetManager, *impl_->rhiDevice,
                                           cmdList, impl_->viewportCameraViewProj);
    if (impl_->sceneRenderer.isPostProcessingEnabled()) {
        impl_->sceneRenderer.renderPostProcess(cmdList);
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

    ImGui::SetCurrentContext(impl_->imguiContext);
    ImGuiIO& io = ImGui::GetIO();

    // 1. Mouse pointer positioning, buttons, and scrolling
    io.AddMousePosEvent(input.mouseX, input.mouseY);
    io.AddMouseButtonEvent(
        0, input.mouseButtons.test(static_cast<size_t>(platform::MouseButton::Left)));
    io.AddMouseButtonEvent(
        1, input.mouseButtons.test(static_cast<size_t>(platform::MouseButton::Right)));
    io.AddMouseButtonEvent(
        2, input.mouseButtons.test(static_cast<size_t>(platform::MouseButton::Middle)));
    io.AddMouseWheelEvent(0.0F, input.wheelDeltaY);

    // 2. Window focus activation and loss transitions
    if (input.focusChanged) {
        io.AddFocusEvent(input.isWindowFocused);
    }

    // 3. Discrete keyboard press, release, and repeat events
    if (input.keyEventCount > 0) {
        for (uint32_t i = 0; i < input.keyEventCount; ++i) {
            const auto& evt = input.keyEvents[i];
            const ImGuiKey key = toImGuiKey(evt.code);
            if (key != ImGuiKey_None) {
                io.AddKeyEvent(key, evt.down);
            }
        }
    } else {
        // Fallback: Infer state transitions when explicit key events were not populated
        for (size_t i = 0; i < static_cast<size_t>(platform::KeyCode::Count); ++i) {
            const bool down = input.keys.test(i);
            if (down != impl_->lastKeys.test(i)) {
                const ImGuiKey key = toImGuiKey(static_cast<platform::KeyCode>(i));
                if (key != ImGuiKey_None) {
                    io.AddKeyEvent(key, down);
                }
            }
        }
    }
    impl_->lastKeys = input.keys;

    // 4. Modifier key states (Ctrl, Shift, Alt, Super/Gui)
    const bool ctrl = input.isKeyDown(platform::KeyCode::LeftCtrl) ||
                      input.isKeyDown(platform::KeyCode::RightCtrl);
    const bool shift = input.isKeyDown(platform::KeyCode::LeftShift) ||
                       input.isKeyDown(platform::KeyCode::RightShift);
    const bool alt =
        input.isKeyDown(platform::KeyCode::LeftAlt) || input.isKeyDown(platform::KeyCode::RightAlt);
    const bool super =
        input.isKeyDown(platform::KeyCode::LeftGui) || input.isKeyDown(platform::KeyCode::RightGui);

    io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
    io.AddKeyEvent(ImGuiMod_Shift, shift);
    io.AddKeyEvent(ImGuiMod_Alt, alt);
    io.AddKeyEvent(ImGuiMod_Super, super);

    // 5. UTF-8 character text input stream
    if (input.textInputLength > 0 && input.textInput[0] != '\0') {
        io.AddInputCharactersUTF8(input.textInput.data());
    }
}

bool EditorEngine::wantCaptureKeyboard() const noexcept {
    if (!impl_->initialized || impl_->imguiContext == nullptr) {
        return false;
    }
    ImGui::SetCurrentContext(impl_->imguiContext);
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool EditorEngine::wantTextInput() const noexcept {
    if (!impl_->initialized || impl_->imguiContext == nullptr) {
        return false;
    }
    ImGui::SetCurrentContext(impl_->imguiContext);
    return ImGui::GetIO().WantTextInput;
}

void EditorEngine::bindWorld(world::World* world) noexcept {
    if (impl_->runtimeSession != nullptr) {
        impl_->runtimeSession->setEditWorld(world);
        impl_->context.world = impl_->runtimeSession->getActiveWorld();
    } else {
        impl_->context.world = world;
    }
}

void EditorEngine::bindConductor(framework::Conductor* conductor) noexcept {
    if (impl_->runtimeSession != nullptr) {
        impl_->runtimeSession->setEditConductor(conductor);
        impl_->context.conductor = impl_->runtimeSession->getActiveConductor();
    } else {
        impl_->context.conductor = conductor;
    }
}

void EditorEngine::bindRuntimeSession(framework::RuntimeSession* session) noexcept {
    impl_->runtimeSession = (session != nullptr) ? session : &impl_->internalRuntimeSession;
    if (impl_->context.world != nullptr) {
        impl_->runtimeSession->setEditWorld(impl_->context.world);
    }
    if (impl_->context.conductor != nullptr) {
        impl_->runtimeSession->setEditConductor(impl_->context.conductor);
    }
}

framework::RuntimeSession& EditorEngine::getRuntimeSession() noexcept {
    return *impl_->runtimeSession;
}

const framework::RuntimeSession& EditorEngine::getRuntimeSession() const noexcept {
    return *impl_->runtimeSession;
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

render::AssetManager& EditorEngine::getAssetManager() noexcept {
    return impl_->assetManager;
}

const render::AssetManager& EditorEngine::getAssetManager() const noexcept {
    return impl_->assetManager;
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

render::SceneRenderer& EditorEngine::getSceneRenderer() noexcept {
    return impl_->sceneRenderer;
}

const render::SceneRenderer& EditorEngine::getSceneRenderer() const noexcept {
    return impl_->sceneRenderer;
}

const render::PostProcessSettings& EditorEngine::getPostProcessSettings() const noexcept {
    return impl_->sceneRenderer.getPostProcessSettings();
}

void EditorEngine::setPostProcessSettings(const render::PostProcessSettings& settings) noexcept {
    impl_->sceneRenderer.setPostProcessSettings(settings);
}

bool EditorEngine::isHdrActive() const noexcept {
    return impl_->sceneRenderer.isHdrActive();
}

bool EditorEngine::isGpuViewportActive() const noexcept {
    return impl_->context.gpuRenderingActive && (impl_->viewportDescriptorSet != VK_NULL_HANDLE);
}

} // namespace eng::editor
