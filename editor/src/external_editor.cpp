#include "editor/external_editor.h"

#include <algorithm>
#include <cctype>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <shellapi.h>
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

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
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return (ext == ".lua");
}

Result<void> ExternalEditorLauncher::openScript(const std::filesystem::path& scriptPath) {
    std::error_code ec;
    if (!std::filesystem::exists(scriptPath, ec) ||
        !std::filesystem::is_regular_file(scriptPath, ec)) {
        return Result<void>::failure(Error(ErrorCode::InvalidArgument,
                                           "Script file does not exist or is not a regular file"));
    }

    if (!isSupportedScriptFile(scriptPath)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "File is not a supported Lua script (.lua)"));
    }

    std::filesystem::path absolutePath = std::filesystem::absolute(scriptPath, ec);
    std::string pathStr = ec ? scriptPath.string() : absolutePath.string();

    if (sCommandHook != nullptr) {
        if (sCommandHook(getDefaultCommand(), pathStr)) {
            return Result<void>::success();
        }
        return Result<void>::failure(
            Error(ErrorCode::PlatformInitFailed, "Mock command hook rejected launch"));
    }

#if defined(_WIN32)
    std::wstring widePath = absolutePath.wstring();
    HINSTANCE res =
        ShellExecuteW(nullptr, L"open", widePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(res) <= 32) {
        return Result<void>::failure(Error(ErrorCode::PlatformInitFailed,
                                           "Failed to launch external editor via ShellExecuteW"));
    }
    return Result<void>::success();
#elif defined(__linux__) || defined(__APPLE__)
    const char* cmd =
#if defined(__APPLE__)
        "open";
#else
        "xdg-open";
#endif
    pid_t pid = 0;
    char* const argv[] = {const_cast<char*>(cmd), const_cast<char*>(pathStr.c_str()), nullptr};
    int ret = posix_spawnp(&pid, cmd, nullptr, nullptr, argv, environ);
    if (ret != 0) {
        return Result<void>::failure(
            Error(ErrorCode::PlatformInitFailed, "posix_spawnp failed to launch external editor"));
    }
    return Result<void>::success();
#else
    return Result<void>::failure(
        Error(ErrorCode::NotSupported, "External editor launch not supported on this platform"));
#endif
}

} // namespace eng::editor
