/// @file external_editor_service.h
/// @brief Service for discovering host code editors and launching scripts safely.
///
/// Purpose:
/// Discovers installed development environments (VS Code, Neovim, Visual Studio, system default)
/// and launches external processes safely without invoking shell interpreters or exposing shell
/// injection vulnerabilities.
///
/// Ownership and Lifetime:
/// Service class with static helpers and mock hooks for automated unit testing.
///
/// Thread Safety:
/// Main thread only (launch operations spawn asynchronous detached child processes).
///
/// Cost:
/// Non-allocating in hot path; cold path operation that executes detached processes.
///
/// Errors:
/// Returns Result<void> with ErrorCode::InvalidArgument if target files or custom binaries
/// cannot be resolved, or ErrorCode::PlatformInitFailed if process spawning fails.

#pragma once

#include "core/error.h"
#include "core/result.h"
#include "editor/editor_preferences.h"

#include <filesystem>
#include <string>
#include <vector>

namespace eng::editor {

/// @struct DetectedEditor
/// @brief Information about a discovered host code editor.
struct DetectedEditor {
    ExternalEditorType type{ExternalEditorType::SystemDefault};
    std::string name{};
    std::string executablePath{};
    bool isAvailable{false};
};

/// @class ExternalEditorService
/// @brief Primary engine service for external IDE / script editor integration.
class ExternalEditorService {
public:
    /// Scans the host system PATH and standard directories for supported editors.
    [[nodiscard]] static std::vector<DetectedEditor> detectAvailableEditors();

    /// Resolves the executable path or launch command for the specified preferences.
    [[nodiscard]] static std::string resolveExecutable(const ScriptingPreferences& prefs);

    /// Formats argument template tokens ({file}, {line}) into individual argument tokens.
    [[nodiscard]] static std::vector<std::string>
    formatArguments(const std::string& argTemplate, const std::filesystem::path& filePath,
                    uint32_t line = 1);

    /// Launches the configured external script editor for the given script path.
    ///
    /// Preconditions: filePath must exist, be a regular file, and have a supported extension
    /// (.lua). Postconditions: Spawns a detached process without blocking the main engine thread.
    static Result<void> openFile(const std::filesystem::path& filePath,
                                 const ScriptingPreferences& prefs, uint32_t line = 1);

    /// Validates and performs a test launch of the configured editor.
    static Result<void> testLaunch(const ScriptingPreferences& prefs);

    /// Command runner hook for unit testing (allows verifying executable and arguments).
    using CommandHook = bool (*)(std::string_view executable,
                                 const std::vector<std::string>& arguments);
    static void setCommandHook(CommandHook hook) noexcept;
    static void clearCommandHook() noexcept;

    /// Checks if a file path is a supported script file (.lua).
    [[nodiscard]] static bool isSupportedScriptFile(const std::filesystem::path& path) noexcept;
};

} // namespace eng::editor
