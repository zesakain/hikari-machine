/// @file external_editor_service.cpp
/// @brief Implementation of external IDE discovery, argument parsing, and process launching.

#include "editor/external_editor_service.h"

#include "core/log.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

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

ExternalEditorService::CommandHook sCommandHook = nullptr;

bool findInPath(const std::string& binaryName, std::string& outPath) {
    const char* pathEnv = std::getenv("PATH");
    if (pathEnv == nullptr || *pathEnv == '\0') {
        return false;
    }

#if defined(_WIN32)
    char delimiter = ';';
#else
    char delimiter = ':';
#endif

    std::stringstream ss(pathEnv);
    std::string dir;
    std::error_code ec;

    while (std::getline(ss, dir, delimiter)) {
        if (dir.empty()) {
            continue;
        }
        std::filesystem::path candidate = std::filesystem::path(dir) / binaryName;
        if (std::filesystem::exists(candidate, ec) &&
            std::filesystem::is_regular_file(candidate, ec)) {
            outPath = candidate.string();
            return true;
        }
#if defined(_WIN32)
        std::filesystem::path exeCandidate = candidate;
        exeCandidate += ".exe";
        if (std::filesystem::exists(exeCandidate, ec) &&
            std::filesystem::is_regular_file(exeCandidate, ec)) {
            outPath = exeCandidate.string();
            return true;
        }
        std::filesystem::path cmdCandidate = candidate;
        cmdCandidate += ".cmd";
        if (std::filesystem::exists(cmdCandidate, ec) &&
            std::filesystem::is_regular_file(cmdCandidate, ec)) {
            outPath = cmdCandidate.string();
            return true;
        }
#endif
    }
    return false;
}

std::string replaceAll(std::string str, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return str;
    }
    size_t startPos = 0;
    while ((startPos = str.find(from, startPos)) != std::string::npos) {
        str.replace(startPos, from.length(), to);
        startPos += to.length();
    }
    return str;
}

std::vector<std::string> tokenizeArguments(const std::string& commandLine) {
    std::vector<std::string> tokens;
    std::string current;
    bool inQuotes = false;
    char quoteChar = '\0';

    for (size_t i = 0; i < commandLine.size(); ++i) {
        char c = commandLine[i];
        if (!inQuotes && (c == ' ' || c == '\t' || c == '\n')) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else if (c == '"' || c == '\'') {
            if (inQuotes && c == quoteChar) {
                inQuotes = false;
                quoteChar = '\0';
            } else if (!inQuotes) {
                inQuotes = true;
                quoteChar = c;
            } else {
                current.push_back(c);
            }
        } else {
            current.push_back(c);
        }
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}

} // namespace

void ExternalEditorService::setCommandHook(CommandHook hook) noexcept {
    sCommandHook = hook;
}

void ExternalEditorService::clearCommandHook() noexcept {
    sCommandHook = nullptr;
}

bool ExternalEditorService::isSupportedScriptFile(const std::filesystem::path& path) noexcept {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return (ext == ".lua");
}

std::vector<DetectedEditor> ExternalEditorService::detectAvailableEditors() {
    std::vector<DetectedEditor> editors;

    // 1. System Default
    DetectedEditor sysDef{};
    sysDef.type = ExternalEditorType::SystemDefault;
    sysDef.name = "System Default";
#if defined(_WIN32)
    sysDef.executablePath = "ShellExecute";
    sysDef.isAvailable = true;
#elif defined(__APPLE__)
    sysDef.executablePath = "/usr/bin/open";
    sysDef.isAvailable = true;
#else
    std::string xdgPath;
    if (findInPath("xdg-open", xdgPath)) {
        sysDef.executablePath = xdgPath;
        sysDef.isAvailable = true;
    } else {
        sysDef.executablePath = "xdg-open";
        sysDef.isAvailable = false;
    }
#endif
    editors.push_back(std::move(sysDef));

    // 2. Visual Studio Code
    DetectedEditor vsc{};
    vsc.type = ExternalEditorType::VisualStudioCode;
    vsc.name = "Visual Studio Code";
    std::string codePath;
    if (findInPath("code", codePath)) {
        vsc.executablePath = codePath;
        vsc.isAvailable = true;
    } else {
        vsc.executablePath = "code";
        vsc.isAvailable = false;
    }
    editors.push_back(std::move(vsc));

    // 3. Neovim
    DetectedEditor nvim{};
    nvim.type = ExternalEditorType::Neovim;
    nvim.name = "Neovim";
    std::string nvimPath;
    if (findInPath("nvim", nvimPath)) {
        nvim.executablePath = nvimPath;
        nvim.isAvailable = true;
    } else {
        nvim.executablePath = "nvim";
        nvim.isAvailable = false;
    }
    editors.push_back(std::move(nvim));

    // 4. Visual Studio (Windows only)
#if defined(_WIN32)
    DetectedEditor vs{};
    vs.type = ExternalEditorType::VisualStudio;
    vs.name = "Visual Studio";
    std::string vsPath;
    if (findInPath("devenv", vsPath)) {
        vs.executablePath = vsPath;
        vs.isAvailable = true;
    } else {
        vs.executablePath = "devenv.exe";
        vs.isAvailable = false;
    }
    editors.push_back(std::move(vs));
#endif

    return editors;
}

std::string ExternalEditorService::resolveExecutable(const ScriptingPreferences& prefs) {
    switch (prefs.editorType) {
    case ExternalEditorType::SystemDefault:
#if defined(_WIN32)
        return "ShellExecute";
#elif defined(__APPLE__)
        return "open";
#else
        return "xdg-open";
#endif

    case ExternalEditorType::VisualStudioCode: {
        std::string path;
        if (findInPath("code", path)) {
            return path;
        }
        return "code";
    }

    case ExternalEditorType::Neovim: {
        std::string path;
        if (findInPath("nvim", path)) {
            return path;
        }
        return "nvim";
    }

    case ExternalEditorType::VisualStudio: {
        std::string path;
        if (findInPath("devenv", path)) {
            return path;
        }
        return "devenv.exe";
    }

    case ExternalEditorType::Custom:
        return prefs.customExecutablePath;
    }

    return "xdg-open";
}

std::vector<std::string>
ExternalEditorService::formatArguments(const std::string& argTemplate,
                                       const std::filesystem::path& filePath, uint32_t line) {
    std::string substituted = argTemplate;
    if (substituted.empty()) {
        substituted = "\"{file}\"";
    }

    std::string absPath = filePath.string();
    std::error_code ec;
    auto absolute = std::filesystem::absolute(filePath, ec);
    if (!ec) {
        absPath = absolute.string();
    }

    substituted = replaceAll(substituted, "{file}", absPath);
    substituted = replaceAll(substituted, "{line}", std::to_string(line));

    return tokenizeArguments(substituted);
}

Result<void> ExternalEditorService::openFile(const std::filesystem::path& filePath,
                                             const ScriptingPreferences& prefs, uint32_t line) {
    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec) || !std::filesystem::is_regular_file(filePath, ec)) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Script file does not exist or is not a regular file"};
    }

    if (!isSupportedScriptFile(filePath)) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "File is not a supported Lua script (.lua)"};
    }

    std::string executable = resolveExecutable(prefs);
    if (executable.empty()) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "No external editor executable configured or resolved"};
    }

    if (prefs.editorType == ExternalEditorType::Custom) {
        if (!std::filesystem::exists(executable, ec)) {
            return Error{.code = ErrorCode::InvalidArgument,
                         .message = "Custom editor executable not found: " + executable};
        }
    }

    std::vector<std::string> args;
    if (prefs.editorType == ExternalEditorType::SystemDefault) {
        std::filesystem::path absPath = std::filesystem::absolute(filePath, ec);
        args.push_back(ec ? filePath.string() : absPath.string());
    } else {
        args = formatArguments(prefs.argumentsTemplate, filePath, line);
    }

    if (sCommandHook != nullptr) {
        if (sCommandHook(executable, args)) {
            return Result<void>::success();
        }
        return Error{.code = ErrorCode::PlatformInitFailed,
                     .message = "Mock command hook rejected launch"};
    }

#if defined(_WIN32)
    if (prefs.editorType == ExternalEditorType::SystemDefault) {
        std::filesystem::path absPath = std::filesystem::absolute(filePath, ec);
        std::wstring widePath = (ec ? filePath : absPath).wstring();
        HINSTANCE res =
            ShellExecuteW(nullptr, L"open", widePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(res) <= 32) {
            return Error{.code = ErrorCode::PlatformInitFailed,
                         .message = "Failed to launch external editor via ShellExecuteW"};
        }
        return Result<void>::success();
    }

    // Windows CreateProcessW with command line
    std::wstring wideCmd = std::filesystem::path(executable).wstring();
    std::wstring cmdLine = L"\"" + wideCmd + L"\"";
    for (const auto& a : args) {
        cmdLine += L" \"" + std::filesystem::path(a).wstring() + L"\"";
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    BOOL ok = CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                             &si, &pi);
    if (!ok) {
        return Error{.code = ErrorCode::PlatformInitFailed,
                     .message = "CreateProcessW failed to launch external editor"};
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return Result<void>::success();

#elif defined(__linux__) || defined(__APPLE__)
    std::vector<char*> argvList;
    argvList.push_back(const_cast<char*>(executable.c_str()));
    for (auto& a : args) {
        argvList.push_back(const_cast<char*>(a.c_str()));
    }
    argvList.push_back(nullptr);

    pid_t pid = 0;
    int ret = posix_spawnp(&pid, executable.c_str(), nullptr, nullptr, argvList.data(), environ);
    if (ret != 0) {
        return Error{.code = ErrorCode::PlatformInitFailed,
                     .message = "posix_spawnp failed to launch external editor: " + executable};
    }
    return Result<void>::success();
#else
    return Error{.code = ErrorCode::NotSupported,
                 .message = "External editor launch not supported on this platform"};
#endif
}

Result<void> ExternalEditorService::testLaunch(const ScriptingPreferences& prefs) {
    std::error_code ec;
    std::filesystem::path tempPath =
        std::filesystem::temp_directory_path() / "hikari_editor_test.lua";
    {
        std::ofstream out(tempPath, std::ios::out | std::ios::trunc);
        out << "-- Hikari Editor External Script Test\n";
        out << "print(\"Hikari script editor integration active!\")\n";
    }

    auto res = openFile(tempPath, prefs, 1);
    return res;
}

} // namespace eng::editor
