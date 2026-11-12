/// @file editor_preferences_test.cpp
/// @brief Unit tests for EditorPreferences, EditorPreferencesSerializer, and ExternalEditorService.

#include "editor/editor_preferences.h"
#include "editor/editor_preferences_serializer.h"
#include "editor/external_editor_service.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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
        dir = std::filesystem::temp_directory_path() / ("hikari_prefs_test_" + std::to_string(now));
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
    }

    ~TempSandbox() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

static std::string sLastExec{};
static std::vector<std::string> sLastArgs{};
static bool sMockShouldSucceed = true;

bool mockEditorServiceHook(std::string_view executable, const std::vector<std::string>& arguments) {
    sLastExec = std::string(executable);
    sLastArgs = arguments;
    return sMockShouldSucceed;
}

} // namespace

static void testPreferencesDefaultsAndClamping() {
    EditorPreferences prefs{};
    TEST_CHECK(prefs.general.autosaveIntervalSeconds == 300);
    TEST_CHECK(prefs.general.openLastProjectOnStartup == true);
    TEST_CHECK(prefs.general.undoHistoryLimit == 100);

    TEST_CHECK(prefs.appearance.themeName == "Dark Studio");
    TEST_CHECK(prefs.appearance.uiScaleFactor == 1.0F);

    TEST_CHECK(prefs.workspace.restoreLayoutOnStartup == true);
    TEST_CHECK(prefs.workspace.confirmOnExit == true);

    TEST_CHECK(prefs.viewport.defaultCameraSpeed == 5.0F);
    TEST_CHECK(prefs.viewport.mouseSensitivity == 0.15F);
    TEST_CHECK(prefs.viewport.fieldOfViewDegrees == 60.0F);

    TEST_CHECK(prefs.scripting.editorType == ExternalEditorType::SystemDefault);
    TEST_CHECK(prefs.scripting.argumentsTemplate == "\"{file}\"");

    TEST_CHECK(prefs.contentBrowser.viewModeGrid == true);
    TEST_CHECK(prefs.contentBrowser.thumbnailSize == 72.0F);

    TEST_CHECK(prefs.performance.maxBackgroundFps == 30);
    TEST_CHECK(prefs.performance.throttleWhenUnfocused == true);

    // Test clamping
    prefs.general.autosaveIntervalSeconds = 1; // minimum is 10
    prefs.appearance.uiScaleFactor = 10.0F;    // maximum is 3.0
    prefs.viewport.defaultCameraSpeed = -5.0F; // minimum is 0.1
    prefs.scripting.argumentsTemplate = "";    // resets to "{file}"
    prefs.validateAndClamp();

    TEST_CHECK(prefs.general.autosaveIntervalSeconds == 10);
    TEST_CHECK(prefs.appearance.uiScaleFactor == 3.0F);
    TEST_CHECK(prefs.viewport.defaultCameraSpeed == 0.1F);
    TEST_CHECK(!prefs.scripting.argumentsTemplate.empty());

    // Test resetToDefaults
    prefs.resetToDefaults();
    TEST_CHECK(prefs.general.autosaveIntervalSeconds == 300);

    std::cout << "[PASS] testPreferencesDefaultsAndClamping\n";
}

static void testSerializationToStringAndBack() {
    EditorPreferences original{};
    original.general.autosaveIntervalSeconds = 600;
    original.appearance.themeName = "Light Modern";
    original.appearance.uiScaleFactor = 1.25F;
    original.viewport.invertYAxis = true;
    original.scripting.editorType = ExternalEditorType::VisualStudioCode;
    original.scripting.argumentsTemplate = "-g \"{file}:{line}\"";
    original.contentBrowser.viewModeGrid = false;
    original.performance.maxBackgroundFps = 60;

    auto serializeRes = EditorPreferencesSerializer::serializeToString(original);
    TEST_CHECK(serializeRes.hasValue());
    const std::string& json = serializeRes.value();

    TEST_CHECK(json.find("\"autosaveIntervalSeconds\": 600") != std::string::npos);
    TEST_CHECK(json.find("\"themeName\": \"Light Modern\"") != std::string::npos);
    TEST_CHECK(json.find("\"Visual Studio Code\"") != std::string::npos);

    auto deserializeRes = EditorPreferencesSerializer::deserializeFromString(json);
    TEST_CHECK(deserializeRes.hasValue());
    const auto& deserialized = deserializeRes.value();

    TEST_CHECK(deserialized.general.autosaveIntervalSeconds == 600);
    TEST_CHECK(deserialized.appearance.themeName == "Light Modern");
    TEST_CHECK(deserialized.appearance.uiScaleFactor == 1.25F);
    TEST_CHECK(deserialized.viewport.invertYAxis == true);
    TEST_CHECK(deserialized.scripting.editorType == ExternalEditorType::VisualStudioCode);
    TEST_CHECK(deserialized.scripting.argumentsTemplate == "-g \"{file}:{line}\"");
    TEST_CHECK(deserialized.contentBrowser.viewModeGrid == false);
    TEST_CHECK(deserialized.performance.maxBackgroundFps == 60);

    std::cout << "[PASS] testSerializationToStringAndBack\n";
}

static void testFileAtomicPersistenceAndFallback() {
    TempSandbox sandbox;
    auto prefsPath = sandbox.dir / "sub" / "config" / "user_preferences.json";

    EditorPreferences prefs{};
    prefs.general.autosaveIntervalSeconds = 420;
    prefs.scripting.editorType = ExternalEditorType::Neovim;

    // Serialize to non-existent nested directory (should auto-create directory and write
    // atomically)
    auto writeRes = EditorPreferencesSerializer::serializeToFile(prefs, prefsPath);
    TEST_CHECK(writeRes.hasValue());
    TEST_CHECK(std::filesystem::exists(prefsPath));
    TEST_CHECK(!std::filesystem::exists(prefsPath.string() + ".tmp"));

    // Read back from file
    auto readRes = EditorPreferencesSerializer::deserializeFromFile(prefsPath);
    TEST_CHECK(readRes.hasValue());
    TEST_CHECK(readRes.value().general.autosaveIntervalSeconds == 420);
    TEST_CHECK(readRes.value().scripting.editorType == ExternalEditorType::Neovim);

    // Non-existent file should gracefully return defaults without failing
    auto nonExistent = sandbox.dir / "missing.json";
    auto readNonExistent = EditorPreferencesSerializer::deserializeFromFile(nonExistent);
    TEST_CHECK(readNonExistent.hasValue());
    TEST_CHECK(readNonExistent.value().general.autosaveIntervalSeconds == 300);

    // Corrupted file should log warning and return safe defaults without crashing
    auto corruptPath = sandbox.dir / "corrupted.json";
    {
        std::ofstream out(corruptPath);
        out << "{ \"general\": { \"autosaveIntervalSeconds\": [NOT_VALID_JSON ";
    }
    auto readCorrupt = EditorPreferencesSerializer::deserializeFromFile(corruptPath);
    TEST_CHECK(readCorrupt.hasValue());
    TEST_CHECK(readCorrupt.value().general.autosaveIntervalSeconds == 300);

    std::cout << "[PASS] testFileAtomicPersistenceAndFallback\n";
}

static void testExternalEditorDetectionAndArguments() {
    // 1. Detection
    auto detected = ExternalEditorService::detectAvailableEditors();
    TEST_CHECK(!detected.empty());
    // First editor is always System Default
    TEST_CHECK(detected[0].type == ExternalEditorType::SystemDefault);
    TEST_CHECK(detected[0].isAvailable);

    // 2. Argument formatting with spaces and quotes
    std::filesystem::path dummyScript = "/home/user/my project/game script.lua";
    auto args1 = ExternalEditorService::formatArguments("\"{file}\"", dummyScript, 1);
    TEST_CHECK(args1.size() == 1);
    TEST_CHECK(args1[0].find("game script.lua") != std::string::npos);

    auto args2 = ExternalEditorService::formatArguments("-g \"{file}:{line}\" --reuse-window",
                                                        dummyScript, 42);
    TEST_CHECK(args2.size() == 3);
    TEST_CHECK(args2[0] == "-g");
    TEST_CHECK(args2[1].find(":42") != std::string::npos);
    TEST_CHECK(args2[2] == "--reuse-window");

    std::cout << "[PASS] testExternalEditorDetectionAndArguments\n";
}

static void testExternalEditorServiceLaunchAndHook() {
    TempSandbox sandbox;
    auto scriptPath = sandbox.dir / "test_player.lua";
    {
        std::ofstream out(scriptPath);
        out << "print('test')\n";
    }

    ExternalEditorService::setCommandHook(mockEditorServiceHook);
    sMockShouldSucceed = true;

    ScriptingPreferences prefs{};
    prefs.editorType = ExternalEditorType::VisualStudioCode;
    prefs.argumentsTemplate = "-r \"{file}\"";

    auto res = ExternalEditorService::openFile(scriptPath, prefs, 1);
    TEST_CHECK(res.hasValue());
    TEST_CHECK(sLastExec.find("code") != std::string::npos);
    TEST_CHECK(sLastArgs.size() == 2);
    TEST_CHECK(sLastArgs[0] == "-r");
    TEST_CHECK(sLastArgs[1].find("test_player.lua") != std::string::npos);

    // Custom executable test
    auto fakeExe = sandbox.dir / "fake_editor.sh";
    {
        std::ofstream out(fakeExe);
        out << "#!/bin/sh\n";
    }
    prefs.editorType = ExternalEditorType::Custom;
    prefs.customExecutablePath = fakeExe.string();
    prefs.argumentsTemplate = "\"{file}\"";

    auto resCustom = ExternalEditorService::openFile(scriptPath, prefs, 1);
    TEST_CHECK(resCustom.hasValue());
    TEST_CHECK(sLastExec == fakeExe.string());

    // Non-existent custom executable
    prefs.customExecutablePath = (sandbox.dir / "does_not_exist_editor").string();
    auto resMissing = ExternalEditorService::openFile(scriptPath, prefs, 1);
    TEST_CHECK(!resMissing.hasValue());
    TEST_CHECK(resMissing.error().code == ErrorCode::InvalidArgument);

    // Test launch helper
    prefs.editorType = ExternalEditorType::VisualStudioCode;
    auto resTestLaunch = ExternalEditorService::testLaunch(prefs);
    TEST_CHECK(resTestLaunch.hasValue());

    ExternalEditorService::clearCommandHook();
    std::cout << "[PASS] testExternalEditorServiceLaunchAndHook\n";
}

int main() {
    testPreferencesDefaultsAndClamping();
    testSerializationToStringAndBack();
    testFileAtomicPersistenceAndFallback();
    testExternalEditorDetectionAndArguments();
    testExternalEditorServiceLaunchAndHook();
    std::cout << "All editor preferences unit tests passed successfully!\n";
    return 0;
}
