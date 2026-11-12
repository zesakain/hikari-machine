/// @file editor_preferences.h
/// @brief Cross-project user preferences data model for the Hikari Editor.
///
/// Purpose:
/// Defines structured configuration for user-level editor preferences that persist across
/// projects and engine sessions, cleanly isolated from project-specific assets and settings.
///
/// Ownership and Lifetime:
/// Owned by EditorEngine or created as value objects. No unmanaged resources.
///
/// Thread Safety:
/// Plain data structs. Safe to read across threads if not concurrently mutated.
///
/// Cost:
/// Non-allocating in hot path. Small value types with standard strings.
///
/// Invariants:
/// Values conform to validated bounds (e.g., autosave interval >= 0, positive camera speeds).

#pragma once

#include <cstdint>
#include <string>

namespace eng::editor {

/// @enum ExternalEditorType
/// @brief Supported host external script editors.
enum class ExternalEditorType : uint8_t {
    SystemDefault = 0,
    VisualStudioCode = 1,
    Neovim = 2,
    VisualStudio = 3,
    Custom = 4,
};

/// Returns human-readable display name for an external editor type.
[[nodiscard]] const char* externalEditorTypeToString(ExternalEditorType type) noexcept;

/// Parses external editor type from string identifier; falls back to SystemDefault if unknown.
[[nodiscard]] ExternalEditorType stringToExternalEditorType(const char* str) noexcept;

/// @struct GeneralPreferences
/// @brief Editor lifecycle, persistence, and undo configuration.
struct GeneralPreferences {
    uint32_t autosaveIntervalSeconds{300};
    bool openLastProjectOnStartup{true};
    bool showWelcomeDialog{false};
    uint32_t undoHistoryLimit{100};
};

/// @struct AppearancePreferences
/// @brief Visual styling, fonts, and UI scaling configuration.
struct AppearancePreferences {
    std::string themeName{"Hikari Dark"};
    float uiScaleFactor{1.0F};
    bool useMonospaceFontForScripts{true};
};

/// @struct WorkspacePreferences
/// @brief Layout restoration and safety confirmation dialogues.
struct WorkspacePreferences {
    bool restoreLayoutOnStartup{true};
    bool confirmOnExit{true};
    bool confirmSceneRevert{true};
};

/// @struct ViewportPreferences
/// @brief Interactive 3D scene camera navigation and grid defaults.
struct ViewportPreferences {
    float defaultCameraSpeed{5.0F};
    float mouseSensitivity{0.15F};
    bool invertYAxis{false};
    bool showGridByDefault{true};
    float fieldOfViewDegrees{60.0F};
};

/// @struct ScriptingPreferences
/// @brief External script editor selection and launch arguments configuration.
struct ScriptingPreferences {
    ExternalEditorType editorType{ExternalEditorType::SystemDefault};
    std::string customExecutablePath{""};
    std::string argumentsTemplate{"\"{file}\""};
    bool autoSaveBeforeLaunch{true};
};

/// @struct ContentBrowserPreferences
/// @brief File browser presentation mode and filtering flags.
struct ContentBrowserPreferences {
    bool viewModeGrid{true};
    float thumbnailSize{72.0F};
    bool showFileExtensions{true};
    bool showHiddenFiles{false};
};

/// @struct KeyboardPreferences
/// @brief Configurable hotkey references for major editor actions.
struct KeyboardPreferences {
    std::string playPauseShortcut{"Ctrl+P"};
    std::string stepShortcut{"F10"};
    std::string stopShortcut{"Ctrl+X"};
    std::string saveSceneShortcut{"Ctrl+S"};
    std::string duplicateShortcut{"Ctrl+D"};
    std::string deleteShortcut{"Delete"};
};

/// @struct PerformancePreferences
/// @brief Background execution limits and real-time telemetry visibility.
struct PerformancePreferences {
    uint32_t maxBackgroundFps{30};
    bool throttleWhenUnfocused{true};
    bool showTelemetryOverlay{false};
};

/// @struct EditorPreferences
/// @brief Aggregated user-level editor configuration object.
struct EditorPreferences {
    GeneralPreferences general{};
    AppearancePreferences appearance{};
    WorkspacePreferences workspace{};
    ViewportPreferences viewport{};
    ScriptingPreferences scripting{};
    ContentBrowserPreferences contentBrowser{};
    KeyboardPreferences keyboard{};
    PerformancePreferences performance{};

    /// Resets all preference categories to canonical default values.
    void resetToDefaults() noexcept;

    /// Validates and clamps all preferences within safe runtime ranges.
    void validateAndClamp() noexcept;
};

} // namespace eng::editor
