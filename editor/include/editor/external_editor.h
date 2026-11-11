/// @file external_editor.h
/// @brief Safe platform launcher for editing script files in external host OS editors.
///
/// Purpose:
/// Provides safe, non-blocking execution of the host operating system's default editor
/// for Lua script assets without invoking shell interpreters or exposing shell injection risks.
///
/// Contract:
/// - Target path must exist, be a regular file, and have a valid .lua extension.
/// - The launched process runs asynchronously and detached from the editor engine.
///
/// Ownership and Lifetime:
/// Pure static utility; no state owned.
///
/// Thread Safety:
/// Safe to call from the main thread.
///
/// Cost:
/// Non-allocating in hot path; cold path operation that spawns an external process.
///
/// Errors:
/// Returns Result<void>::failure with ErrorCode::InvalidArgument if target path does not exist,
/// is not a regular file, or does not have a supported script extension, or if process spawning
/// fails.

#pragma once

#include "core/error.h"
#include "core/result.h"

#include <filesystem>
#include <string_view>

namespace eng::editor {

/// @class ExternalEditorLauncher
/// @brief Launches host OS default application for script editing safely without shell invocation.
class ExternalEditorLauncher {
public:
    /// Checks whether the given path points to a supported script file (.lua).
    [[nodiscard]] static bool isSupportedScriptFile(const std::filesystem::path& path) noexcept;

    /// Launches the host OS default editor for the given script path asynchronously.
    ///
    /// Linux: xdg-open via posix_spawnp
    /// macOS: open via posix_spawnp
    /// Windows: ShellExecuteW
    ///
    /// @param scriptPath Filesystem path to the script file.
    /// @return Result<void> indicating success or failure error code.
    static Result<void> openScript(const std::filesystem::path& scriptPath);

    /// Command runner hook for unit testing (allows verifying arguments without spawning OS app).
    using CommandHook = bool (*)(std::string_view command, std::string_view argument);
    static void setCommandHook(CommandHook hook) noexcept;
    static void clearCommandHook() noexcept;

    /// Retrieves the platform default editor executable name.
    [[nodiscard]] static std::string_view getDefaultCommand() noexcept;
};

} // namespace eng::editor
