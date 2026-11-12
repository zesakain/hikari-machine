#include "editor/external_editor.h"

#include "editor/external_editor_service.h"

namespace eng::editor {

namespace {
ExternalEditorLauncher::CommandHook sCommandHook = nullptr;
}

void ExternalEditorLauncher::setCommandHook(CommandHook hook) noexcept {
    sCommandHook = hook;
}

void ExternalEditorLauncher::clearCommandHook() noexcept {
    sCommandHook = nullptr;
}

std::string_view ExternalEditorLauncher::getDefaultCommand() noexcept {
#if defined(_WIN32)
    return "ShellExecute";
#elif defined(__APPLE__)
    return "open";
#else
    return "xdg-open";
#endif
}

bool ExternalEditorLauncher::isSupportedScriptFile(const std::filesystem::path& path) noexcept {
    return ExternalEditorService::isSupportedScriptFile(path);
}

Result<void> ExternalEditorLauncher::openScript(const std::filesystem::path& scriptPath) {
    if (sCommandHook != nullptr) {
        std::error_code ec;
        if (!std::filesystem::exists(scriptPath, ec) ||
            !std::filesystem::is_regular_file(scriptPath, ec)) {
            return Result<void>::failure(Error(
                ErrorCode::InvalidArgument, "Script file does not exist or is not a regular file"));
        }

        if (!isSupportedScriptFile(scriptPath)) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "File is not a supported Lua script (.lua)"));
        }

        std::filesystem::path absolutePath = std::filesystem::absolute(scriptPath, ec);
        std::string pathStr = ec ? scriptPath.string() : absolutePath.string();

        if (sCommandHook(getDefaultCommand(), pathStr)) {
            return Result<void>::success();
        }
        return Result<void>::failure(
            Error(ErrorCode::PlatformInitFailed, "Mock command hook rejected launch"));
    }

    ScriptingPreferences defaultPrefs{};
    defaultPrefs.editorType = ExternalEditorType::SystemDefault;
    return ExternalEditorService::openFile(scriptPath, defaultPrefs, 1);
}

} // namespace eng::editor
