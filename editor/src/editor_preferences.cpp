/// @file editor_preferences.cpp
/// @brief Implementation of EditorPreferences defaults, validation, and type conversions.

#include "editor/editor_preferences.h"

#include <algorithm>
#include <cstring>

namespace eng::editor {

const char* externalEditorTypeToString(ExternalEditorType type) noexcept {
    switch (type) {
    case ExternalEditorType::SystemDefault:
        return "System Default";
    case ExternalEditorType::VisualStudioCode:
        return "Visual Studio Code";
    case ExternalEditorType::Neovim:
        return "Neovim";
    case ExternalEditorType::VisualStudio:
        return "Visual Studio";
    case ExternalEditorType::Custom:
        return "Custom Executable";
    }
    return "System Default";
}

ExternalEditorType stringToExternalEditorType(const char* str) noexcept {
    if (str == nullptr) {
        return ExternalEditorType::SystemDefault;
    }
    if (std::strcmp(str, "VisualStudioCode") == 0 || std::strcmp(str, "Visual Studio Code") == 0 ||
        std::strcmp(str, "vscode") == 0 || std::strcmp(str, "code") == 0) {
        return ExternalEditorType::VisualStudioCode;
    }
    if (std::strcmp(str, "Neovim") == 0 || std::strcmp(str, "nvim") == 0) {
        return ExternalEditorType::Neovim;
    }
    if (std::strcmp(str, "VisualStudio") == 0 || std::strcmp(str, "Visual Studio") == 0 ||
        std::strcmp(str, "vs") == 0 || std::strcmp(str, "devenv") == 0) {
        return ExternalEditorType::VisualStudio;
    }
    if (std::strcmp(str, "Custom") == 0 || std::strcmp(str, "Custom Executable") == 0) {
        return ExternalEditorType::Custom;
    }
    return ExternalEditorType::SystemDefault;
}

void EditorPreferences::resetToDefaults() noexcept {
    general = GeneralPreferences{};
    appearance = AppearancePreferences{};
    workspace = WorkspacePreferences{};
    viewport = ViewportPreferences{};
    scripting = ScriptingPreferences{};
    contentBrowser = ContentBrowserPreferences{};
    keyboard = KeyboardPreferences{};
    performance = PerformancePreferences{};
}

void EditorPreferences::validateAndClamp() noexcept {
    // General
    general.autosaveIntervalSeconds = std::clamp(general.autosaveIntervalSeconds, 10U, 3600U);
    general.undoHistoryLimit = std::clamp(general.undoHistoryLimit, 10U, 1000U);

    // Appearance
    appearance.uiScaleFactor = std::clamp(appearance.uiScaleFactor, 0.5F, 3.0F);

    // Viewport
    viewport.defaultCameraSpeed = std::clamp(viewport.defaultCameraSpeed, 0.1F, 100.0F);
    viewport.mouseSensitivity = std::clamp(viewport.mouseSensitivity, 0.01F, 2.0F);
    viewport.fieldOfViewDegrees = std::clamp(viewport.fieldOfViewDegrees, 30.0F, 120.0F);

    // Content Browser
    contentBrowser.thumbnailSize = std::clamp(contentBrowser.thumbnailSize, 32.0F, 256.0F);

    // Performance
    performance.maxBackgroundFps = std::clamp(performance.maxBackgroundFps, 5U, 240U);

    // Scripting
    if (scripting.argumentsTemplate.empty()) {
        scripting.argumentsTemplate = "\"{file}\"";
    }
}

} // namespace eng::editor
