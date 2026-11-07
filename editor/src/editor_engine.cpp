/// @file editor_engine.cpp
/// @brief Implementation of Dear ImGui developer workspace and diagnostic panels.

#include "editor/editor_engine.h"

#include "core/assert.h"
#include "core/log.h"
#include "framework/conductor.h"
#include "platform/input_snapshot.h"
#include "rhi/command_list.h"
#include "rhi/rhi_device.h"
#include "world/world.h"

#include <imgui.h>

namespace eng::editor {

struct EditorEngine::Impl {
    ImGuiContext* imguiContext{nullptr};
    rhi::RhiDevice* rhiDevice{nullptr};
    world::World* boundWorld{nullptr};
    framework::Conductor* boundConductor{nullptr};

    EditorConfig config{};
    uint32_t lastDrawCalls{0};
    uint32_t lastVertices{0};
    bool initialized{false};

    void drawConductorPanel();
    void drawEntityInspector();
    void drawMetricsPanel();
    void drawConsolePanel();
};

void EditorEngine::Impl::drawConductorPanel() {
    if (!config.showConductorPanel) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(400.0f, 300.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Conductor Monitor", &config.showConductorPanel)) {
        if (boundConductor != nullptr) {
            auto activeSlot = boundConductor->getActiveSlot();
            auto state = boundConductor->getState();
            ImGui::Text("Active Slot: %s (%s)",
                        activeSlot == framework::ActiveSlot::SlotA ? "Slot A" : "Slot B",
                        boundConductor->getActiveMode().getName().data());
            ImGui::Text("Standby Mode: %s", boundConductor->getStandbyMode().getName().data());

            const char* stateStr = "Active";
            switch (state) {
            case framework::ConductorState::Uninitialized:
                stateStr = "Uninitialized";
                break;
            case framework::ConductorState::Active:
                stateStr = "Active";
                break;
            case framework::ConductorState::Preparing:
                stateStr = "Preparing";
                break;
            case framework::ConductorState::Prepared:
                stateStr = "Prepared";
                break;
            case framework::ConductorState::Committing:
                stateStr = "Committing";
                break;
            case framework::ConductorState::Transitioning:
                stateStr = "Transitioning";
                break;
            case framework::ConductorState::Terminated:
                stateStr = "Terminated";
                break;
            }
            ImGui::Text("Lifecycle State: %s", stateStr);
            ImGui::Text("Committed Transitions: %u",
                        boundConductor->getSuccessfulTransitionsCount());

            ImGui::Separator();
            if (state == framework::ConductorState::Active) {
                if (ImGui::Button("Request Transition")) {
                    (void)boundConductor->requestTransition();
                }
            } else if (state == framework::ConductorState::Prepared) {
                if (ImGui::Button("Execute Transition")) {
                    (void)boundConductor->executeTransition();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    boundConductor->cancelTransition();
                }
            } else if (state == framework::ConductorState::Preparing) {
                ImGui::Text("Preparing standby mode...");
                if (ImGui::Button("Cancel")) {
                    boundConductor->cancelTransition();
                }
            }
        } else {
            ImGui::TextDisabled("No active Conductor bound");
        }
    }
    ImGui::End();
}

void EditorEngine::Impl::drawEntityInspector() {
    if (!config.showEntityInspector) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(300.0f, 400.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("World Entity Inspector", &config.showEntityInspector)) {
        if (boundWorld != nullptr) {
            ImGui::Text("Shared World Active");
            ImGui::Separator();
            ImGui::Text("Entity Hierarchy:");
            ImGui::BulletText("Authoritative simulation entities alive");
        } else {
            ImGui::TextDisabled("No Shared World bound");
        }
    }
    ImGui::End();
}

void EditorEngine::Impl::drawMetricsPanel() {
    if (!config.showMetricsPanel) {
        return;
    }

    if (ImGui::Begin("Metrics & Telemetry", &config.showMetricsPanel)) {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("Application FPS: %.1f", static_cast<double>(io.Framerate));
        ImGui::Text("Frame Time: %.3f ms",
                    static_cast<double>(1000.0f / (io.Framerate > 0.0f ? io.Framerate : 60.0f)));
        ImGui::Separator();
        ImGui::Text("ImGui Draw Calls: %u", lastDrawCalls);
        ImGui::Text("ImGui Vertices: %u", lastVertices);
    }
    ImGui::End();
}

void EditorEngine::Impl::drawConsolePanel() {
    if (!config.showConsolePanel) {
        return;
    }

    if (ImGui::Begin("Log Console", &config.showConsolePanel)) {
        auto& logger = Logger::defaultLogger();
        if (ImGui::Button("Clear Logs")) {
            logger.clear();
        }
        ImGui::SameLine();
        ImGui::Text("Buffered: %zu / %zu", logger.count(), logger.capacity());
        ImGui::Separator();

        ImGui::BeginChild("LogScrollingRegion", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar);
        for (size_t i = 0; i < logger.count(); ++i) {
            const auto* record = logger.peek(i);
            if (record != nullptr) {
                ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                if (record->level == LogLevel::Warning) {
                    color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                } else if (record->level >= LogLevel::Error) {
                    color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                }
                ImGui::TextColored(color, "[%s] %.*s", toString(record->level).data(),
                                   static_cast<int>(record->messageLength), record->message.data());
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

// -----------------------------------------------------------------------------
// EditorEngine Public Implementation
// -----------------------------------------------------------------------------
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

    IMGUI_CHECKVERSION();
    impl_->imguiContext = ImGui::CreateContext();
    if (impl_->imguiContext == nullptr) {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // Disable ini persistence in headless/unit tests
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;

    // Dark engine styling
    ImGui::StyleColorsDark();

    // Build font texture atlas
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    impl_->initialized = true;
    ENG_LOG_INFO("Editor", "Dear ImGui editor engine initialized successfully");
    return true;
}

void EditorEngine::shutdown() {
    if (impl_->initialized) {
        if (impl_->imguiContext != nullptr) {
            ImGui::DestroyContext(impl_->imguiContext);
            impl_->imguiContext = nullptr;
        }
        impl_->rhiDevice = nullptr;
        impl_->boundWorld = nullptr;
        impl_->boundConductor = nullptr;
        impl_->initialized = false;
        ENG_LOG_INFO("Editor", "Dear ImGui editor engine shut down");
    }
}

bool EditorEngine::isInitialized() const noexcept {
    return impl_->initialized;
}

void EditorEngine::beginFrame(Duration dt) {
    if (!impl_->initialized) {
        return;
    }

    ImGui::SetCurrentContext(impl_->imguiContext);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    float deltaSec = static_cast<float>(dt.toSeconds());
    io.DeltaTime = deltaSec > 0.0f ? deltaSec : (1.0f / 60.0f);

    ImGui::NewFrame();
}

void EditorEngine::drawPanels() {
    if (!impl_->initialized) {
        return;
    }

    ImGui::SetCurrentContext(impl_->imguiContext);
    impl_->drawConductorPanel();
    impl_->drawEntityInspector();
    impl_->drawMetricsPanel();
    impl_->drawConsolePanel();
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

    impl_->lastDrawCalls = 0;
    impl_->lastVertices = 0;

    for (int n = 0; n < drawData->CmdListsCount; n++) {
        const ImDrawList* cmd_list = drawData->CmdLists[n];
        impl_->lastVertices += static_cast<uint32_t>(cmd_list->VtxBuffer.Size);
        impl_->lastDrawCalls += static_cast<uint32_t>(cmd_list->CmdBuffer.Size);

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            int32_t clipX = static_cast<int32_t>(pcmd->ClipRect.x);
            int32_t clipY = static_cast<int32_t>(pcmd->ClipRect.y);
            uint32_t clipW =
                static_cast<uint32_t>(std::max(0.0f, pcmd->ClipRect.z - pcmd->ClipRect.x));
            uint32_t clipH =
                static_cast<uint32_t>(std::max(0.0f, pcmd->ClipRect.w - pcmd->ClipRect.y));

            cmdList.setScissor(clipX, clipY, clipW, clipH);
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
}

void EditorEngine::bindWorld(world::World* world) noexcept {
    impl_->boundWorld = world;
}

void EditorEngine::bindConductor(framework::Conductor* conductor) noexcept {
    impl_->boundConductor = conductor;
}

uint32_t EditorEngine::lastDrawCallCount() const noexcept {
    return impl_->lastDrawCalls;
}

uint32_t EditorEngine::lastVertexCount() const noexcept {
    return impl_->lastVertices;
}

} // namespace eng::editor
