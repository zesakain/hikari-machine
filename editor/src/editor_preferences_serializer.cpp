/// @file editor_preferences_serializer.cpp
/// @brief Implementation of transactional JSON serializer for EditorPreferences.

#include "editor/editor_preferences_serializer.h"

#include "core/json.h"
#include "core/log.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace eng::editor {

namespace {

using namespace core;

} // namespace

std::filesystem::path EditorPreferencesSerializer::getDefaultPreferencesPath() {
#if defined(_WIN32)
    const char* appData = std::getenv("APPDATA");
    if (appData != nullptr && *appData != '\0') {
        return std::filesystem::path(appData) / "Hikari" / "editor_preferences.json";
    }
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile != nullptr && *userProfile != '\0') {
        return std::filesystem::path(userProfile) / "AppData" / "Roaming" / "Hikari" /
               "editor_preferences.json";
    }
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / "Library" / "Application Support" / "Hikari" /
               "editor_preferences.json";
    }
#else
    // Linux and BSD: XDG_CONFIG_HOME or ~/.config
    const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
    if (xdgConfig != nullptr && *xdgConfig != '\0') {
        return std::filesystem::path(xdgConfig) / "hikari" / "editor_preferences.json";
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".config" / "hikari" / "editor_preferences.json";
    }
#endif
    return std::filesystem::current_path() / "editor_preferences.json";
}

Result<std::string> EditorPreferencesSerializer::serializeToString(const EditorPreferences& prefs,
                                                                   bool prettyPrint,
                                                                   int indentSpaces) {
    JsonValue root = JsonValue::makeObject();
    root.set("version", JsonValue(static_cast<int>(kCurrentVersion)));

    // General
    JsonValue generalObj = JsonValue::makeObject();
    generalObj.set("autosaveIntervalSeconds",
                   JsonValue(static_cast<int>(prefs.general.autosaveIntervalSeconds)));
    generalObj.set("openLastProjectOnStartup", JsonValue(prefs.general.openLastProjectOnStartup));
    generalObj.set("showWelcomeDialog", JsonValue(prefs.general.showWelcomeDialog));
    generalObj.set("undoHistoryLimit", JsonValue(static_cast<int>(prefs.general.undoHistoryLimit)));
    root.set("general", std::move(generalObj));

    // Appearance
    JsonValue appObj = JsonValue::makeObject();
    appObj.set("themeName", JsonValue(prefs.appearance.themeName));
    appObj.set("uiScaleFactor", JsonValue(static_cast<double>(prefs.appearance.uiScaleFactor)));
    appObj.set("useMonospaceFontForScripts",
               JsonValue(prefs.appearance.useMonospaceFontForScripts));
    root.set("appearance", std::move(appObj));

    // Workspace
    JsonValue wsObj = JsonValue::makeObject();
    wsObj.set("restoreLayoutOnStartup", JsonValue(prefs.workspace.restoreLayoutOnStartup));
    wsObj.set("confirmOnExit", JsonValue(prefs.workspace.confirmOnExit));
    wsObj.set("confirmSceneRevert", JsonValue(prefs.workspace.confirmSceneRevert));
    root.set("workspace", std::move(wsObj));

    // Viewport
    JsonValue vpObj = JsonValue::makeObject();
    vpObj.set("defaultCameraSpeed",
              JsonValue(static_cast<double>(prefs.viewport.defaultCameraSpeed)));
    vpObj.set("mouseSensitivity", JsonValue(static_cast<double>(prefs.viewport.mouseSensitivity)));
    vpObj.set("invertYAxis", JsonValue(prefs.viewport.invertYAxis));
    vpObj.set("showGridByDefault", JsonValue(prefs.viewport.showGridByDefault));
    vpObj.set("fieldOfViewDegrees",
              JsonValue(static_cast<double>(prefs.viewport.fieldOfViewDegrees)));
    root.set("viewport", std::move(vpObj));

    // Scripting
    JsonValue scriptObj = JsonValue::makeObject();
    scriptObj.set("editorType", JsonValue(externalEditorTypeToString(prefs.scripting.editorType)));
    scriptObj.set("customExecutablePath", JsonValue(prefs.scripting.customExecutablePath));
    scriptObj.set("argumentsTemplate", JsonValue(prefs.scripting.argumentsTemplate));
    scriptObj.set("autoSaveBeforeLaunch", JsonValue(prefs.scripting.autoSaveBeforeLaunch));
    root.set("scripting", std::move(scriptObj));

    // Content Browser
    JsonValue cbObj = JsonValue::makeObject();
    cbObj.set("viewModeGrid", JsonValue(prefs.contentBrowser.viewModeGrid));
    cbObj.set("thumbnailSize", JsonValue(static_cast<double>(prefs.contentBrowser.thumbnailSize)));
    cbObj.set("showFileExtensions", JsonValue(prefs.contentBrowser.showFileExtensions));
    cbObj.set("showHiddenFiles", JsonValue(prefs.contentBrowser.showHiddenFiles));
    root.set("contentBrowser", std::move(cbObj));

    // Keyboard
    JsonValue kbObj = JsonValue::makeObject();
    kbObj.set("playPauseShortcut", JsonValue(prefs.keyboard.playPauseShortcut));
    kbObj.set("stepShortcut", JsonValue(prefs.keyboard.stepShortcut));
    kbObj.set("stopShortcut", JsonValue(prefs.keyboard.stopShortcut));
    kbObj.set("saveSceneShortcut", JsonValue(prefs.keyboard.saveSceneShortcut));
    kbObj.set("duplicateShortcut", JsonValue(prefs.keyboard.duplicateShortcut));
    kbObj.set("deleteShortcut", JsonValue(prefs.keyboard.deleteShortcut));
    root.set("keyboard", std::move(kbObj));

    // Performance
    JsonValue perfObj = JsonValue::makeObject();
    perfObj.set("maxBackgroundFps",
                JsonValue(static_cast<int>(prefs.performance.maxBackgroundFps)));
    perfObj.set("throttleWhenUnfocused", JsonValue(prefs.performance.throttleWhenUnfocused));
    perfObj.set("showTelemetryOverlay", JsonValue(prefs.performance.showTelemetryOverlay));
    root.set("performance", std::move(perfObj));

    return Result<std::string>::success(root.serialize(prettyPrint ? indentSpaces : 0));
}

Result<void> EditorPreferencesSerializer::serializeToFile(const EditorPreferences& prefs,
                                                          const std::filesystem::path& path,
                                                          bool prettyPrint, int indentSpaces) {
    auto strRes = serializeToString(prefs, prettyPrint, indentSpaces);
    if (!strRes.hasValue()) {
        return strRes.error();
    }

    std::filesystem::path parentDir = path.parent_path();
    if (!parentDir.empty() && !std::filesystem::exists(parentDir)) {
        std::error_code ec;
        std::filesystem::create_directories(parentDir, ec);
        if (ec) {
            return Error{.code = ErrorCode::InvalidArgument,
                         .message = "Failed to create preferences directory: " + ec.message()};
        }
    }

    std::filesystem::path tmpPath = path;
    tmpPath += ".tmp";

    {
        std::ofstream ofs(tmpPath, std::ios::out | std::ios::trunc);
        if (!ofs.is_open()) {
            return Error{.code = ErrorCode::DeviceCreationFailed,
                         .message = "Failed to open temporary preferences file for writing"};
        }
        ofs << strRes.value();
        if (ofs.fail()) {
            return Error{.code = ErrorCode::DeviceCreationFailed,
                         .message = "Failed writing serialized preferences stream"};
        }
    }

    std::error_code renameEc;
    std::filesystem::rename(tmpPath, path, renameEc);
    if (renameEc) {
        return Error{.code = ErrorCode::DeviceCreationFailed,
                     .message =
                         "Atomic rename of editor preferences failed: " + renameEc.message()};
    }

    return Result<void>::success();
}

Result<EditorPreferences>
EditorPreferencesSerializer::deserializeFromString(std::string_view jsonText) {
    auto parseRes = JsonValue::parse(jsonText);
    if (!parseRes.hasValue()) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Failed to parse JSON for EditorPreferences: " +
                                std::string(parseRes.error().message)};
    }

    const JsonValue& root = parseRes.value();
    if (!root.isObject()) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Root JSON token for EditorPreferences must be an object"};
    }

    EditorPreferences prefs{};

    // General
    if (const JsonValue* gen = root.find("general"); gen != nullptr && gen->isObject()) {
        if (const JsonValue* v = gen->find("autosaveIntervalSeconds");
            v != nullptr && v->isNumber()) {
            prefs.general.autosaveIntervalSeconds = static_cast<uint32_t>(v->asInt());
        }
        if (const JsonValue* v = gen->find("openLastProjectOnStartup");
            v != nullptr && v->isBool()) {
            prefs.general.openLastProjectOnStartup = v->asBool();
        }
        if (const JsonValue* v = gen->find("showWelcomeDialog"); v != nullptr && v->isBool()) {
            prefs.general.showWelcomeDialog = v->asBool();
        }
        if (const JsonValue* v = gen->find("undoHistoryLimit"); v != nullptr && v->isNumber()) {
            prefs.general.undoHistoryLimit = static_cast<uint32_t>(v->asInt());
        }
    }

    // Appearance
    if (const JsonValue* app = root.find("appearance"); app != nullptr && app->isObject()) {
        if (const JsonValue* v = app->find("themeName"); v != nullptr && v->isString()) {
            prefs.appearance.themeName = std::string(v->asString());
        }
        if (const JsonValue* v = app->find("uiScaleFactor"); v != nullptr && v->isNumber()) {
            prefs.appearance.uiScaleFactor = static_cast<float>(v->asDouble());
        }
        if (const JsonValue* v = app->find("useMonospaceFontForScripts");
            v != nullptr && v->isBool()) {
            prefs.appearance.useMonospaceFontForScripts = v->asBool();
        }
    }

    // Workspace
    if (const JsonValue* ws = root.find("workspace"); ws != nullptr && ws->isObject()) {
        if (const JsonValue* v = ws->find("restoreLayoutOnStartup"); v != nullptr && v->isBool()) {
            prefs.workspace.restoreLayoutOnStartup = v->asBool();
        }
        if (const JsonValue* v = ws->find("confirmOnExit"); v != nullptr && v->isBool()) {
            prefs.workspace.confirmOnExit = v->asBool();
        }
        if (const JsonValue* v = ws->find("confirmSceneRevert"); v != nullptr && v->isBool()) {
            prefs.workspace.confirmSceneRevert = v->asBool();
        }
    }

    // Viewport
    if (const JsonValue* vp = root.find("viewport"); vp != nullptr && vp->isObject()) {
        if (const JsonValue* v = vp->find("defaultCameraSpeed"); v != nullptr && v->isNumber()) {
            prefs.viewport.defaultCameraSpeed = static_cast<float>(v->asDouble());
        }
        if (const JsonValue* v = vp->find("mouseSensitivity"); v != nullptr && v->isNumber()) {
            prefs.viewport.mouseSensitivity = static_cast<float>(v->asDouble());
        }
        if (const JsonValue* v = vp->find("invertYAxis"); v != nullptr && v->isBool()) {
            prefs.viewport.invertYAxis = v->asBool();
        }
        if (const JsonValue* v = vp->find("showGridByDefault"); v != nullptr && v->isBool()) {
            prefs.viewport.showGridByDefault = v->asBool();
        }
        if (const JsonValue* v = vp->find("fieldOfViewDegrees"); v != nullptr && v->isNumber()) {
            prefs.viewport.fieldOfViewDegrees = static_cast<float>(v->asDouble());
        }
    }

    // Scripting
    if (const JsonValue* sc = root.find("scripting"); sc != nullptr && sc->isObject()) {
        if (const JsonValue* v = sc->find("editorType"); v != nullptr && v->isString()) {
            prefs.scripting.editorType =
                stringToExternalEditorType(std::string(v->asString()).c_str());
        }
        if (const JsonValue* v = sc->find("customExecutablePath"); v != nullptr && v->isString()) {
            prefs.scripting.customExecutablePath = std::string(v->asString());
        }
        if (const JsonValue* v = sc->find("argumentsTemplate"); v != nullptr && v->isString()) {
            prefs.scripting.argumentsTemplate = std::string(v->asString());
        }
        if (const JsonValue* v = sc->find("autoSaveBeforeLaunch"); v != nullptr && v->isBool()) {
            prefs.scripting.autoSaveBeforeLaunch = v->asBool();
        }
    }

    // Content Browser
    if (const JsonValue* cb = root.find("contentBrowser"); cb != nullptr && cb->isObject()) {
        if (const JsonValue* v = cb->find("viewModeGrid"); v != nullptr && v->isBool()) {
            prefs.contentBrowser.viewModeGrid = v->asBool();
        }
        if (const JsonValue* v = cb->find("thumbnailSize"); v != nullptr && v->isNumber()) {
            prefs.contentBrowser.thumbnailSize = static_cast<float>(v->asDouble());
        }
        if (const JsonValue* v = cb->find("showFileExtensions"); v != nullptr && v->isBool()) {
            prefs.contentBrowser.showFileExtensions = v->asBool();
        }
        if (const JsonValue* v = cb->find("showHiddenFiles"); v != nullptr && v->isBool()) {
            prefs.contentBrowser.showHiddenFiles = v->asBool();
        }
    }

    // Keyboard
    if (const JsonValue* kb = root.find("keyboard"); kb != nullptr && kb->isObject()) {
        if (const JsonValue* v = kb->find("playPauseShortcut"); v != nullptr && v->isString()) {
            prefs.keyboard.playPauseShortcut = std::string(v->asString());
        }
        if (const JsonValue* v = kb->find("stepShortcut"); v != nullptr && v->isString()) {
            prefs.keyboard.stepShortcut = std::string(v->asString());
        }
        if (const JsonValue* v = kb->find("stopShortcut"); v != nullptr && v->isString()) {
            prefs.keyboard.stopShortcut = std::string(v->asString());
        }
        if (const JsonValue* v = kb->find("saveSceneShortcut"); v != nullptr && v->isString()) {
            prefs.keyboard.saveSceneShortcut = std::string(v->asString());
        }
        if (const JsonValue* v = kb->find("duplicateShortcut"); v != nullptr && v->isString()) {
            prefs.keyboard.duplicateShortcut = std::string(v->asString());
        }
        if (const JsonValue* v = kb->find("deleteShortcut"); v != nullptr && v->isString()) {
            prefs.keyboard.deleteShortcut = std::string(v->asString());
        }
    }

    // Performance
    if (const JsonValue* pf = root.find("performance"); pf != nullptr && pf->isObject()) {
        if (const JsonValue* v = pf->find("maxBackgroundFps"); v != nullptr && v->isNumber()) {
            prefs.performance.maxBackgroundFps = static_cast<uint32_t>(v->asInt());
        }
        if (const JsonValue* v = pf->find("throttleWhenUnfocused"); v != nullptr && v->isBool()) {
            prefs.performance.throttleWhenUnfocused = v->asBool();
        }
        if (const JsonValue* v = pf->find("showTelemetryOverlay"); v != nullptr && v->isBool()) {
            prefs.performance.showTelemetryOverlay = v->asBool();
        }
    }

    prefs.validateAndClamp();
    return Result<EditorPreferences>::success(std::move(prefs));
}

Result<EditorPreferences>
EditorPreferencesSerializer::deserializeFromFile(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        // Safe default: return default preferences when file does not exist
        EditorPreferences defaults{};
        return Result<EditorPreferences>::success(defaults);
    }

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        ENG_LOG_WARN("Editor",
                     "Could not open preferences file at " + path.string() + "; using defaults");
        EditorPreferences defaults{};
        return Result<EditorPreferences>::success(defaults);
    }

    std::stringstream buffer;
    buffer << ifs.rdbuf();

    auto result = deserializeFromString(buffer.str());
    if (!result.hasValue()) {
        ENG_LOG_WARN("Editor", "Corrupted preferences file at " + path.string() + ": " +
                                   std::string(result.error().message) +
                                   "; falling back to defaults");
        EditorPreferences defaults{};
        return Result<EditorPreferences>::success(defaults);
    }

    return result;
}

} // namespace eng::editor
