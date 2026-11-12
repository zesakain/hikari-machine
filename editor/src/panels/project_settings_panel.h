/// @file project_settings_panel.h
/// @brief Centralized Graphics Settings & Scalability editor configuration panel.
///
/// Purpose:
/// Provides an interactive Dear ImGui workspace for configuring rendering fidelity,
/// quality presets, lighting, shadow parameters, post-processing, display sync,
/// and diagnostic rendering modes.
///
/// Invariants & Rules:
/// - Built ONLY when HIKARI_BUILD_EDITOR=ON (EDITOR=1).
/// - Inherits from EditorPanel contract.
/// - Distinct separation between operational controls and future/planned capabilities.
/// - Exception-free and RTTI-free.
///
/// Thread Safety:
/// Main thread only (Dear ImGui UI rendering).

#pragma once

#include "editor/editor_panel.h"
#include "render/graphics_settings.h"

namespace eng::editor {

/// @class ProjectSettingsPanel
/// @brief Editor panel managing global graphics, lighting, shadow, and display scalability.
class ProjectSettingsPanel final : public EditorPanel {
public:
    ProjectSettingsPanel();
    ~ProjectSettingsPanel() override = default;

    [[nodiscard]] std::string_view getId() const noexcept override { return "ProjectSettings"; }

    [[nodiscard]] std::string_view getDisplayName() const noexcept override {
        return "Project Settings";
    }

    [[nodiscard]] std::string_view getCategory() const noexcept override { return "Settings"; }

    void draw(EditorContext& ctx) override;

private:
    void drawCategorySidebar();
    void drawQualityTab(EditorContext& ctx);
    void drawLightingTab(EditorContext& ctx);
    void drawShadowsTab(EditorContext& ctx);
    void drawPostProcessTab(EditorContext& ctx);
    void drawPerformanceTab(EditorContext& ctx);
    void drawDebugTab(EditorContext& ctx);
    void drawFooter(EditorContext& ctx);

    bool matchesSearch(const char* label) const noexcept;

    enum class Category : uint8_t {
        Quality = 0,
        Lighting = 1,
        Shadows = 2,
        PostProcess = 3,
        Performance = 4,
        Debug = 5,
    };

    render::GraphicsSettings pendingSettings_{};
    render::GraphicsSettings activeSettings_{};
    Category activeCategory_{Category::Quality};
    char searchBuffer_[128]{""};
    bool initialized_{false};
    std::string statusMessage_{};
    float statusTimer_{0.0F};
};

} // namespace eng::editor
