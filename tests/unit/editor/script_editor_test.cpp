/// @file script_editor_test.cpp
/// @brief Unit tests for external Lua script editor launching and safety validation.

#include "editor/external_editor.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace eng;
using namespace eng::editor;

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << '\n'; \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)

namespace {

struct TempSandbox {
    std::filesystem::path dir;

    TempSandbox() {
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        dir =
            std::filesystem::temp_directory_path() / ("hikari_script_test_" + std::to_string(now));
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
    }

    ~TempSandbox() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

static std::string sLastCommand{};
static std::string sLastArgument{};
static bool sMockShouldSucceed = true;

bool mockCommandHook(std::string_view command, std::string_view argument) {
    sLastCommand = std::string(command);
    sLastArgument = std::string(argument);
    return sMockShouldSucceed;
}

} // namespace

static void testSupportedScriptExtensions() {
    TEST_CHECK(ExternalEditorLauncher::isSupportedScriptFile("script.lua"));
    TEST_CHECK(ExternalEditorLauncher::isSupportedScriptFile("SCRIPT.LUA"));
    TEST_CHECK(ExternalEditorLauncher::isSupportedScriptFile("path/to/player_controller.Lua"));
    TEST_CHECK(!ExternalEditorLauncher::isSupportedScriptFile("script.txt"));
    TEST_CHECK(!ExternalEditorLauncher::isSupportedScriptFile("script.cpp"));
    TEST_CHECK(!ExternalEditorLauncher::isSupportedScriptFile("script"));
    TEST_CHECK(!ExternalEditorLauncher::isSupportedScriptFile("lua"));
    TEST_CHECK(!ExternalEditorLauncher::isSupportedScriptFile("mesh.glb"));
    std::cout << "[PASS] testSupportedScriptExtensions\n";
}

static void testValidationFailures() {
    TempSandbox sandbox;

    // 1. Non-existent file
    auto nonExistent = sandbox.dir / "does_not_exist.lua";
    auto res1 = ExternalEditorLauncher::openScript(nonExistent);
    TEST_CHECK(!res1.hasValue());
    TEST_CHECK(res1.error().code == ErrorCode::InvalidArgument);

    // 2. Directory with .lua extension (must be regular file)
    auto dirAsLua = sandbox.dir / "fake_folder.lua";
    std::filesystem::create_directories(dirAsLua);
    auto res2 = ExternalEditorLauncher::openScript(dirAsLua);
    TEST_CHECK(!res2.hasValue());
    TEST_CHECK(res2.error().code == ErrorCode::InvalidArgument);

    // 3. Regular file but unsupported extension
    auto textFile = sandbox.dir / "readme.txt";
    {
        std::ofstream out(textFile);
        out << "hello";
    }
    auto res3 = ExternalEditorLauncher::openScript(textFile);
    TEST_CHECK(!res3.hasValue());
    TEST_CHECK(res3.error().code == ErrorCode::InvalidArgument);

    std::cout << "[PASS] testValidationFailures\n";
}

static void testLaunchExecutionAndHook() {
    TempSandbox sandbox;
    auto scriptPath = sandbox.dir / "game_logic.lua";
    {
        std::ofstream out(scriptPath);
        out << "print('test')\n";
    }

    // Set mock hook to intercept launch
    sLastCommand.clear();
    sLastArgument.clear();
    sMockShouldSucceed = true;
    ExternalEditorLauncher::setCommandHook(mockCommandHook);

    auto successRes = ExternalEditorLauncher::openScript(scriptPath);
    TEST_CHECK(successRes.hasValue());
    TEST_CHECK(!sLastCommand.empty());
    TEST_CHECK(sLastCommand == ExternalEditorLauncher::getDefaultCommand());
    TEST_CHECK(!sLastArgument.empty());
    TEST_CHECK(sLastArgument.find("game_logic.lua") != std::string::npos);

    // Test hook failure propagation
    sMockShouldSucceed = false;
    auto failRes = ExternalEditorLauncher::openScript(scriptPath);
    TEST_CHECK(!failRes.hasValue());
    TEST_CHECK(failRes.error().code == ErrorCode::PlatformInitFailed);

    ExternalEditorLauncher::clearCommandHook();
    std::cout << "[PASS] testLaunchExecutionAndHook\n";
}

static void testShellInjectionSafety() {
    TempSandbox sandbox;
    // Filename with characters that would be hazardous under shell evaluation
    auto hazardousPath = sandbox.dir / "test script with spaces;echo dangerous.lua";
    {
        std::ofstream out(hazardousPath);
        out << "-- safe script\n";
    }

    sLastCommand.clear();
    sLastArgument.clear();
    sMockShouldSucceed = true;
    ExternalEditorLauncher::setCommandHook(mockCommandHook);

    auto res = ExternalEditorLauncher::openScript(hazardousPath);
    TEST_CHECK(res.hasValue());
    // Verify path is passed as a pure argument string without shell escapes
    TEST_CHECK(sLastArgument.find("test script with spaces;echo dangerous.lua") !=
               std::string::npos);

    ExternalEditorLauncher::clearCommandHook();
    std::cout << "[PASS] testShellInjectionSafety\n";
}

int main() {
    std::cout << "Running External Lua Script Editor unit tests...\n";
    testSupportedScriptExtensions();
    testValidationFailures();
    testLaunchExecutionAndHook();
    testShellInjectionSafety();
    std::cout << "All External Lua Script Editor tests passed successfully!\n";
    return 0;
}
