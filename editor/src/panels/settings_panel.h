/// @file settings_panel.h
/// @brief Unified Editor Settings and Preferences dockable panel.
///
/// Purpose:
/// Provides a centralized, dockable ImGui panel integrating both cross-project Editor
/// Preferences (user configuration, scripting IDE, viewport navigation, appearance)
/// and project-specific Graphics & Scalability configuration.
///
/// Invariants & Rules:
/// - Built ONLY when HIKARI_BUILD_EDITOR=ON (EDITOR=1).
/// - Inherits from EditorPanel contract.
/// - Does not spawn independent OS windows; docks into Dear ImGui workspace.
/// - Exception-free and RTTI-free.
///
/// Thread Safety:
/// Main thread only (Dear ImGui UI rendering).

#pragma once

#include "editor/editor_panel.h"
#include "editor/editor_preferences.h"
#include "editor/external_editor_service.h"
#include "render/graphics_settings.h"

#include <vector>

namespace eng::editor {

/// @enum SettingsCategory
/// @brief Categories selectable in the settings navigation sidebar.
enum class SettingsCategory : uint8_t {
    // User Preferences (Cross-Project)
    General = 0,
    Appearance,
    Workspace,
    Viewport,
    Scripting,
    ContentBrowser,
    Keyboard,
    Performance,

    // Project Settings (Project-Specific)
    GraphicsQuality,
    GraphicsLighting,
    GraphicsShadows,
    GraphicsPostProcess,
    GraphicsDisplay,
    GraphicsDebug,
};

/// @class SettingsPanel
/// @brief Unified editor panel managing both user preferences and project graphics settings.
class SettingsPanel final : public EditorPanel {
public:
    SettingsPanel();
    ~SettingsPanel() override = default;

    [[nodiscard]] std::string_view getId() const noexcept override { return "settings"; }

    [[nodiscard]] std::string_view getDisplayName() const noexcept override { return "Settings"; }

    [[nodiscard]] std::string_view getCategory() const noexcept override { return "Settings"; }

    void draw(EditorContext& ctx) override;

    /// Selects an active category and opens the panel.
    void openToCategory(SettingsCategory category) noexcept;

private:
    void drawCategorySidebar();

    // User Preferences Tabs
    void drawGeneralTab(EditorContext& ctx);
    void drawAppearanceTab(EditorContext& ctx);
    void drawWorkspaceTab(EditorContext& ctx);
    void drawViewportTab(EditorContext& ctx);
    void drawScriptingTab(EditorContext& ctx);
    void drawContentBrowserTab(EditorContext& ctx);
    void drawKeyboardTab(EditorContext& ctx);
    void drawPerformanceTab(EditorContext& ctx);

    // Project Graphics Settings Tabs
    void drawQualityTab(EditorContext& ctx);
    void drawLightingTab(EditorContext& ctx);
    void drawShadowsTab(EditorContext& ctx);
    void drawPostProcessTab(EditorContext& ctx);
    void drawDisplayTab(EditorContext& ctx);
    void drawDebugTab(EditorContext& ctx);

    void drawFooter(EditorContext& ctx);
    [[nodiscard]] bool matchesSearch(const char* label) const noexcept;

    // State: Preferences
    EditorPreferences pendingPrefs_{};
    EditorPreferences activePrefs_{};
    bool prefsInitialized_{false};

    // State: Project Graphics Settings
    render::GraphicsSettings pendingGraphics_{};
    render::GraphicsSettings activeGraphics_{};
    bool graphicsInitialized_{false};

    // State: Navigation and UI
    SettingsCategory activeCategory_{SettingsCategory::General};
    char searchBuffer_[128]{""};
    std::string statusMessage_{};
    float statusTimer_{0.0F};

    // Cached detected editors
    std::vector<DetectedEditor> detectedEditors_{};
    bool editorsDetected_{false};
    char customExecBuffer_[256]{""};
    char argsTemplateBuffer_[256]{""};
};

} // namespace eng::editor
