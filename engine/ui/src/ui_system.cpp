/// @file ui_system.cpp
/// @brief Implementation of the Hikari UiSystem bridging RmlUi into engine architecture.

#include "ui/ui_system.h"

#include "core/assert.h"
#include "core/log.h"
#include "platform/input_snapshot.h"
#include "render/render_graph.h"
#include "rmlui_render_backend.h"
#include "rmlui_system_backend.h"

#include <RmlUi/Core.h>
#include <unordered_map>

namespace eng::ui {

struct UiSystem::Impl {
    HikariSystemInterface systemInterface;
    HikariRenderInterface renderInterface;
    Rml::Context* context{nullptr};
    std::unordered_map<uint64_t, Rml::ElementDocument*> documents;
    uint64_t nextDocId{1};
    UiConfig config{};
    bool initialized{false};
};

UiSystem::UiSystem() : impl_(std::make_unique<Impl>()) {}

UiSystem::~UiSystem() {
    shutdown();
}

UiSystem::UiSystem(UiSystem&& other) noexcept = default;
UiSystem& UiSystem::operator=(UiSystem&& other) noexcept = default;

Result<void, UiError> UiSystem::initialize(const UiConfig& config, rhi::RhiDevice& rhiDevice) {
    if (impl_->initialized) {
        return UiError::InvalidState;
    }

    impl_->config = config;
    impl_->renderInterface.setDevice(&rhiDevice);

    Rml::SetSystemInterface(&impl_->systemInterface);
    Rml::SetRenderInterface(&impl_->renderInterface);

    if (!Rml::Initialise()) {
        ENG_LOG_ERROR("UI", "Failed to initialise RmlUi core runtime");
        return UiError::InitializationFailed;
    }

    impl_->context =
        Rml::CreateContext("hikari_main_context", Rml::Vector2i(static_cast<int>(config.width),
                                                                static_cast<int>(config.height)));

    if (impl_->context == nullptr) {
        ENG_LOG_ERROR("UI", "Failed to create RmlUi context");
        Rml::Shutdown();
        return UiError::ContextCreationFailed;
    }

    impl_->initialized = true;
    ENG_LOG_FORMAT(LogLevel::Info, "UI", "UiSystem initialized (%ux%u, fontScale: %.2f)",
                   config.width, config.height, static_cast<double>(config.fontScale));

    return Result<void, UiError>::success();
}

void UiSystem::shutdown() {
    if (impl_->initialized) {
        for (auto& [id, doc] : impl_->documents) {
            if (doc != nullptr) {
                doc->Close();
            }
        }
        impl_->documents.clear();

        if (impl_->context != nullptr) {
            Rml::RemoveContext(impl_->context->GetName());
            impl_->context = nullptr;
        }

        Rml::Shutdown();
        impl_->initialized = false;
        ENG_LOG_INFO("UI", "UiSystem shut down successfully");
    }
}

bool UiSystem::isInitialized() const noexcept {
    return impl_->initialized;
}

Result<UiDocumentHandle, UiError> UiSystem::loadDocumentFromMemory(std::string_view rmlContent,
                                                                   std::string_view documentName) {
    if (!impl_->initialized || impl_->context == nullptr) {
        return UiError::InvalidState;
    }

    std::string rmlString(rmlContent);
    std::string nameString(documentName);

    Rml::ElementDocument* doc = impl_->context->LoadDocumentFromMemory(rmlString, nameString);
    if (doc == nullptr || doc->GetNumChildren() == 0) {
        if (doc != nullptr) {
            doc->Close();
        }
        ENG_LOG_FORMAT(LogLevel::Error, "UI", "Failed to parse RML document '%.*s'",
                       static_cast<int>(documentName.size()), documentName.data());
        return UiError::DocumentLoadFailed;
    }

    uint64_t handleId = impl_->nextDocId++;
    impl_->documents[handleId] = doc;
    impl_->renderInterface.setActiveDocuments(static_cast<uint32_t>(impl_->documents.size()));

    return UiDocumentHandle{handleId};
}

void UiSystem::closeDocument(UiDocumentHandle handle) {
    if (!impl_->initialized) {
        return;
    }

    auto it = impl_->documents.find(handle.id);
    if (it != impl_->documents.end()) {
        if (it->second != nullptr) {
            it->second->Close();
        }
        impl_->documents.erase(it);
        impl_->renderInterface.setActiveDocuments(static_cast<uint32_t>(impl_->documents.size()));
    }
}

void UiSystem::showDocument(UiDocumentHandle handle) {
    if (!impl_->initialized) {
        return;
    }

    auto it = impl_->documents.find(handle.id);
    if (it != impl_->documents.end() && it->second != nullptr) {
        it->second->Show();
    }
}

void UiSystem::hideDocument(UiDocumentHandle handle) {
    if (!impl_->initialized) {
        return;
    }

    auto it = impl_->documents.find(handle.id);
    if (it != impl_->documents.end() && it->second != nullptr) {
        it->second->Hide();
    }
}

void UiSystem::update(Duration unscaledUiDelta) {
    if (!impl_->initialized || impl_->context == nullptr) {
        return;
    }

    double dt = unscaledUiDelta.toSeconds();
    impl_->systemInterface.advanceTime(dt);
    impl_->context->Update();
}

void UiSystem::render(rhi::RhiCommandList& cmdList) {
    if (!impl_->initialized || impl_->context == nullptr) {
        return;
    }

    impl_->renderInterface.resetStats();
    impl_->renderInterface.setCommandList(&cmdList);
    impl_->context->Render();
    impl_->renderInterface.setCommandList(nullptr);
}

void UiSystem::render(render::RenderGraph& renderGraph) {
    if (!impl_->initialized || impl_->context == nullptr) {
        return;
    }

    renderGraph.addPass(
        "UiPass",
        [](render::RenderPassBuilder& /*builder*/) {
            // Setup pass inputs/outputs
        },
        [this](rhi::RhiCommandList& cmdList) { render(cmdList); });
}

void UiSystem::processInput(const platform::InputSnapshot& currentInput,
                            const platform::InputSnapshot& previousInput) {
    if (!impl_->initialized || impl_->context == nullptr) {
        return;
    }

    // Process mouse movement
    impl_->context->ProcessMouseMove(static_cast<int>(currentInput.mouseX),
                                     static_cast<int>(currentInput.mouseY), 0);

    // Process left mouse button
    if (currentInput.wasMouseButtonPressed(platform::MouseButton::Left, previousInput)) {
        impl_->context->ProcessMouseButtonDown(0, 0);
    } else if (currentInput.wasMouseButtonReleased(platform::MouseButton::Left, previousInput)) {
        impl_->context->ProcessMouseButtonUp(0, 0);
    }

    // Process right mouse button
    if (currentInput.wasMouseButtonPressed(platform::MouseButton::Right, previousInput)) {
        impl_->context->ProcessMouseButtonDown(1, 0);
    } else if (currentInput.wasMouseButtonReleased(platform::MouseButton::Right, previousInput)) {
        impl_->context->ProcessMouseButtonUp(1, 0);
    }
}

void UiSystem::resize(uint32_t width, uint32_t height) {
    if (!impl_->initialized || impl_->context == nullptr) {
        return;
    }

    impl_->config.width = width;
    impl_->config.height = height;
    impl_->context->SetDimensions(Rml::Vector2i(static_cast<int>(width), static_cast<int>(height)));
}

const UiRenderStats& UiSystem::renderStats() const noexcept {
    return impl_->renderInterface.stats();
}

} // namespace eng::ui
