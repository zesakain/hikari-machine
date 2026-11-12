/// @file settings_panel.cpp
/// @brief Unified Editor Settings and Preferences dockable panel implementation.

#include "panels/settings_panel.h"

#include "editor/editor_engine.h"
#include "editor/editor_preferences_serializer.h"
#include "editor/editor_theme.h"
#include "editor/ui_helpers.h"
#include "imgui_utils.h"
#include "render/graphics_settings_serializer.h"
#include "render/scene_renderer.h"
#include "rhi/rhi_device.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <imgui.h>

namespace eng::editor {

namespace {

bool stringContainsCaseInsensitive(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty()) {
        return true;
    }
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                          [](char ch1, char ch2) {
                              return std::tolower(static_cast<unsigned char>(ch1)) ==
                                     std::tolower(static_cast<unsigned char>(ch2));
                          });
    return it != haystack.end();
}

void helpMarker(const char* desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0F);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

const char* presetToString(render::GraphicsQualityPreset preset) noexcept {
    switch (preset) {
    case render::GraphicsQualityPreset::Low:
        return "Low";
    case render::GraphicsQualityPreset::Medium:
        return "Medium";
    case render::GraphicsQualityPreset::High:
        return "High";
    case render::GraphicsQualityPreset::Ultra:
        return "Ultra";
    case render::GraphicsQualityPreset::Cinematic:
        return "Cinematic";
    case render::GraphicsQualityPreset::Custom:
        return "Custom";
    }
    return "Custom";
}

bool isPreferencesCategory(SettingsCategory cat) noexcept {
    return cat == SettingsCategory::General || cat == SettingsCategory::Appearance ||
           cat == SettingsCategory::Workspace || cat == SettingsCategory::Viewport ||
           cat == SettingsCategory::Scripting || cat == SettingsCategory::ContentBrowser ||
           cat == SettingsCategory::Keyboard || cat == SettingsCategory::Performance;
}

} // namespace

SettingsPanel::SettingsPanel() {
    isOpen_ = false;
    pendingGraphics_ = render::GraphicsSettings::createPreset(render::GraphicsQualityPreset::High);
    activeGraphics_ = pendingGraphics_;
    pendingPrefs_.resetToDefaults();
    activePrefs_ = pendingPrefs_;
}

void SettingsPanel::openToCategory(SettingsCategory category) noexcept {
    activeCategory_ = category;
    setOpen(true);
}

bool SettingsPanel::matchesSearch(const char* label) const noexcept {
    if (searchBuffer_[0] == '\0') {
        return true;
    }
    return stringContainsCaseInsensitive(label, searchBuffer_);
}

void SettingsPanel::draw(EditorContext& ctx) {
    if (!isOpen_) {
        return;
    }

    if (!prefsInitialized_ && ctx.editorEngine != nullptr) {
        activePrefs_ = ctx.editorEngine->getEditorPreferences();
        pendingPrefs_ = activePrefs_;
        std::snprintf(customExecBuffer_, sizeof(customExecBuffer_), "%s",
                      pendingPrefs_.scripting.customExecutablePath.c_str());
        std::snprintf(argsTemplateBuffer_, sizeof(argsTemplateBuffer_), "%s",
                      pendingPrefs_.scripting.argumentsTemplate.c_str());
        prefsInitialized_ = true;
    }

    if (!graphicsInitialized_ && ctx.editorEngine != nullptr) {
        activeGraphics_ = ctx.editorEngine->getGraphicsSettings();
        pendingGraphics_ = activeGraphics_;
        graphicsInitialized_ = true;
    }

    if (!editorsDetected_) {
        detectedEditors_ = ExternalEditorService::detectAvailableEditors();
        editorsDetected_ = true;
    }

    ImGui::SetNextWindowSize(ImVec2(860.0F, 640.0F), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Settings", &isOpen_)) {
        ImGui::End();
        return;
    }

    const auto& theme = getActiveThemeColors();

    // 1. Top Search and Header Bar
    ImGui::BeginGroup();
    ImGui::PushItemWidth(320.0F);
    (void)drawSearchField("SettingsFilter", searchBuffer_, sizeof(searchBuffer_),
                          "Search settings...");
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (searchBuffer_[0] != '\0') {
        if (ImGui::SmallButton("Clear")) {
            searchBuffer_[0] = '\0';
        }
        ImGui::SameLine();
    }

    // Active Category Pill
    if (isPreferencesCategory(activeCategory_)) {
        drawBadge("User Preferences", theme.accentPrimary);
    } else {
        render::GraphicsQualityPreset currentPreset = pendingGraphics_.detectPreset();
        drawBadge("Project Settings", theme.statusWarning);
        ImGui::SameLine();
        drawBadge(presetToString(currentPreset), theme.accentPrimary);
    }

    ImGui::EndGroup();
    ImGui::Separator();

    // 2. Main Two-Column Navigation & Content Layout
    const float sidebarWidth = 210.0F;
    const float footerHeight = 44.0F;
    const float contentHeight = ImGui::GetContentRegionAvail().y - footerHeight;

    // Left Category Sidebar
    ImGui::BeginChild("SettingsSidebar", ImVec2(sidebarWidth, contentHeight), true);
    drawCategorySidebar();
    ImGui::EndChild();

    ImGui::SameLine();

    // Right Content Area
    ImGui::BeginChild("SettingsContent", ImVec2(0.0F, contentHeight), true);
    switch (activeCategory_) {
    case SettingsCategory::General:
        drawGeneralTab(ctx);
        break;
    case SettingsCategory::Appearance:
        drawAppearanceTab(ctx);
        break;
    case SettingsCategory::Workspace:
        drawWorkspaceTab(ctx);
        break;
    case SettingsCategory::Viewport:
        drawViewportTab(ctx);
        break;
    case SettingsCategory::Scripting:
        drawScriptingTab(ctx);
        break;
    case SettingsCategory::ContentBrowser:
        drawContentBrowserTab(ctx);
        break;
    case SettingsCategory::Keyboard:
        drawKeyboardTab(ctx);
        break;
    case SettingsCategory::Performance:
        drawPerformanceTab(ctx);
        break;
    case SettingsCategory::GraphicsQuality:
        drawQualityTab(ctx);
        break;
    case SettingsCategory::GraphicsLighting:
        drawLightingTab(ctx);
        break;
    case SettingsCategory::GraphicsShadows:
        drawShadowsTab(ctx);
        break;
    case SettingsCategory::GraphicsPostProcess:
        drawPostProcessTab(ctx);
        break;
    case SettingsCategory::GraphicsDisplay:
        drawDisplayTab(ctx);
        break;
    case SettingsCategory::GraphicsDebug:
        drawDebugTab(ctx);
        break;
    }
    ImGui::EndChild();

    // 3. Bottom Action Footer
    drawFooter(ctx);

    ImGui::End();
}

void SettingsPanel::drawCategorySidebar() {
    const auto& theme = getActiveThemeColors();

    auto drawNavButton = [this, &theme](const char* label, SettingsCategory cat) {
        bool selected = (activeCategory_ == cat);
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, toImVec4(theme.accentPrimary));
            ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(theme.backgroundApp));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(theme.textPrimary));
        }
        if (ImGui::Button(label, ImVec2(-1.0F, 26.0F))) {
            activeCategory_ = cat;
        }
        ImGui::PopStyleColor(2);
    };

    // SECTION 1: PREFERENCES
    ImGui::TextColored(toImVec4(theme.textMuted), "USER PREFERENCES");
    ImGui::Separator();

    if (matchesSearch("General")) {
        drawNavButton("General", SettingsCategory::General);
    }
    if (matchesSearch("Appearance Theme")) {
        drawNavButton("Appearance", SettingsCategory::Appearance);
    }
    if (matchesSearch("Workspace Layout")) {
        drawNavButton("Workspace", SettingsCategory::Workspace);
    }
    if (matchesSearch("Viewport Camera Grid")) {
        drawNavButton("Viewport", SettingsCategory::Viewport);
    }
    if (matchesSearch("Scripting External Editor VSCode Neovim")) {
        drawNavButton("Scripting", SettingsCategory::Scripting);
    }
    if (matchesSearch("Content Browser Thumbnails")) {
        drawNavButton("Content Browser", SettingsCategory::ContentBrowser);
    }
    if (matchesSearch("Keyboard Hotkeys Shortcuts")) {
        drawNavButton("Keyboard", SettingsCategory::Keyboard);
    }
    if (matchesSearch("Performance FPS Background")) {
        drawNavButton("Performance", SettingsCategory::Performance);
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // SECTION 2: PROJECT SETTINGS
    ImGui::TextColored(toImVec4(theme.textMuted), "PROJECT SETTINGS");
    ImGui::Separator();

    if (matchesSearch("Quality Fidelity Presets Anti-Aliasing")) {
        drawNavButton("Graphics Quality", SettingsCategory::GraphicsQuality);
    }
    if (matchesSearch("Lighting Ambient Directional")) {
        drawNavButton("Lighting", SettingsCategory::GraphicsLighting);
    }
    if (matchesSearch("Shadows Resolution Bias PCF")) {
        drawNavButton("Shadows", SettingsCategory::GraphicsShadows);
    }
    if (matchesSearch("Post-Processing Bloom Tone Map Exposure")) {
        drawNavButton("Post-Processing", SettingsCategory::GraphicsPostProcess);
    }
    if (matchesSearch("Display VSync Frame Rate")) {
        drawNavButton("Display & Sync", SettingsCategory::GraphicsDisplay);
    }
    if (matchesSearch("Debug Wireframe Timings")) {
        drawNavButton("Debug Views", SettingsCategory::GraphicsDebug);
    }
}

// -----------------------------------------------------------------------------
// USER PREFERENCES TABS
// -----------------------------------------------------------------------------

void SettingsPanel::drawGeneralTab(EditorContext& /*ctx*/) {
    ImGui::TextUnformatted("General Preferences");
    ImGui::TextDisabled("Cross-project settings stored in user configuration directory.");
    ImGui::Separator();
    ImGui::Spacing();

    int autosaveSec = static_cast<int>(pendingPrefs_.general.autosaveIntervalSeconds);
    if (ImGui::SliderInt("Autosave Interval (seconds)", &autosaveSec, 30, 1800, "%d s")) {
        pendingPrefs_.general.autosaveIntervalSeconds = static_cast<uint32_t>(autosaveSec);
    }
    helpMarker("Frequency of automated scene backups.");

    ImGui::Checkbox("Open Last Project on Startup",
                    &pendingPrefs_.general.openLastProjectOnStartup);
    ImGui::Checkbox("Show Welcome Dialog on Launch", &pendingPrefs_.general.showWelcomeDialog);

    int undoLimit = static_cast<int>(pendingPrefs_.general.undoHistoryLimit);
    if (ImGui::SliderInt("Undo History Limit", &undoLimit, 10, 500)) {
        pendingPrefs_.general.undoHistoryLimit = static_cast<uint32_t>(undoLimit);
    }
    helpMarker("Maximum number of undoable scene operations stored in memory.");
}

void SettingsPanel::drawAppearanceTab(EditorContext& /*ctx*/) {
    ImGui::TextUnformatted("Appearance & Themes");
    ImGui::TextDisabled("Visual theme, typography, and scaling preferences.");
    ImGui::Separator();
    ImGui::Spacing();

    const char* themes[] = {"Dark Studio", "Charcoal Slate", "High Contrast Dark"};
    int currentTheme = 0;
    if (pendingPrefs_.appearance.themeName == "Charcoal Slate")
        currentTheme = 1;
    else if (pendingPrefs_.appearance.themeName == "High Contrast Dark")
        currentTheme = 2;

    if (ImGui::Combo("Editor Theme", &currentTheme, themes, 3)) {
        pendingPrefs_.appearance.themeName = themes[currentTheme];
    }

    ImGui::SliderFloat("UI Scale Factor", &pendingPrefs_.appearance.uiScaleFactor, 0.75F, 2.0F,
                       "%.2fx");
    helpMarker("Global ImGui display scaling.");

    ImGui::Checkbox("Use Monospace Font for Code / Script Panels",
                    &pendingPrefs_.appearance.useMonospaceFontForScripts);
}

void SettingsPanel::drawWorkspaceTab(EditorContext& /*ctx*/) {
    ImGui::TextUnformatted("Workspace & Safety Dialogs");
    ImGui::TextDisabled("Window docking behavior and confirmation prompts.");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Checkbox("Restore Window Layout on Startup",
                    &pendingPrefs_.workspace.restoreLayoutOnStartup);
    ImGui::Checkbox("Confirm Before Application Exit", &pendingPrefs_.workspace.confirmOnExit);
    ImGui::Checkbox("Confirm Before Scene Revert / Deletion",
                    &pendingPrefs_.workspace.confirmSceneRevert);
}

void SettingsPanel::drawViewportTab(EditorContext& /*ctx*/) {
    ImGui::TextUnformatted("Viewport & Navigation");
    ImGui::TextDisabled("Default camera controls, speed, and 3D grid visibility.");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SliderFloat("Default Camera Fly Speed", &pendingPrefs_.viewport.defaultCameraSpeed, 0.5F,
                       50.0F, "%.1f m/s");
    ImGui::SliderFloat("Camera Mouse Sensitivity", &pendingPrefs_.viewport.mouseSensitivity, 0.02F,
                       1.0F, "%.2f");
    ImGui::Checkbox("Invert Camera Y Axis", &pendingPrefs_.viewport.invertYAxis);
    ImGui::Checkbox("Show Viewport Grid by Default", &pendingPrefs_.viewport.showGridByDefault);
    ImGui::SliderFloat("Field of View (Degrees)", &pendingPrefs_.viewport.fieldOfViewDegrees, 30.0F,
                       110.0F, "%.0f deg");
}

void SettingsPanel::drawScriptingTab(EditorContext& /*ctx*/) {
    const auto& theme = getActiveThemeColors();

    ImGui::TextUnformatted("Scripting & External Editor");
    ImGui::TextDisabled("Configure host IDE used when editing Lua scripts from Content Browser.");
    ImGui::Separator();
    ImGui::Spacing();

    // Editor Type Selection
    const char* editorTypes[] = {
        "System Default (OS Association)", "Visual Studio Code", "Neovim",
        "Visual Studio (Windows)",         "Custom Executable",
    };
    int currentType = static_cast<int>(pendingPrefs_.scripting.editorType);
    if (ImGui::Combo("External Script Editor", &currentType, editorTypes, 5)) {
        pendingPrefs_.scripting.editorType = static_cast<ExternalEditorType>(currentType);
    }
    helpMarker("Application launched when double-clicking .lua scripts or using 'Open With'.");

    // Detected Editors Status
    ImGui::Spacing();
    ImGui::TextColored(toImVec4(theme.textMuted), "HOST SYSTEM DISCOVERY");
    ImGui::BeginChild("DetectedEditorsBox", ImVec2(0.0F, 90.0F), true);
    for (const auto& ed : detectedEditors_) {
        ImGui::Bullet();
        ImGui::Text("%s: ", ed.name.c_str());
        ImGui::SameLine();
        if (ed.isAvailable) {
            drawStatusPill("Found", theme.statusSuccess);
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", ed.executablePath.c_str());
        } else {
            drawStatusPill("Not Found", theme.textMuted);
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();

    // Custom executable path
    if (pendingPrefs_.scripting.editorType == ExternalEditorType::Custom) {
        ImGui::TextUnformatted("Custom Executable Path:");
        if (ImGui::InputText("##CustomExec", customExecBuffer_, sizeof(customExecBuffer_))) {
            pendingPrefs_.scripting.customExecutablePath = customExecBuffer_;
        }
        std::error_code ec;
        if (!pendingPrefs_.scripting.customExecutablePath.empty()) {
            if (std::filesystem::exists(pendingPrefs_.scripting.customExecutablePath, ec)) {
                ImGui::SameLine();
                drawStatusPill("Valid Executable", theme.statusSuccess);
            } else {
                ImGui::SameLine();
                drawStatusPill("File Not Found", theme.statusError);
            }
        }
    }

    // Arguments Template
    ImGui::TextUnformatted("Launch Arguments Template:");
    if (ImGui::InputText("##ArgsTemplate", argsTemplateBuffer_, sizeof(argsTemplateBuffer_))) {
        pendingPrefs_.scripting.argumentsTemplate = argsTemplateBuffer_;
    }
    helpMarker("Supported tokens: {file} = absolute script path, {line} = target line number.");
    ImGui::TextDisabled("Examples: \"{file}\" or -g \"{file}:{line}\"");

    ImGui::Spacing();
    ImGui::Checkbox("Auto-save scene before launching external editor",
                    &pendingPrefs_.scripting.autoSaveBeforeLaunch);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Test Launch Button
    if (ImGui::Button("Test Launch Editor", ImVec2(180.0F, 30.0F))) {
        pendingPrefs_.scripting.customExecutablePath = customExecBuffer_;
        pendingPrefs_.scripting.argumentsTemplate = argsTemplateBuffer_;
        auto res = ExternalEditorService::testLaunch(pendingPrefs_.scripting);
        if (res.hasValue()) {
            statusMessage_ = "External script editor launched successfully!";
            statusTimer_ = 4.0F;
        } else {
            statusMessage_ = "Launch failed: " + std::string(res.error().message);
            statusTimer_ = 5.0F;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Spawns external process safely without shell invocation.");
}

void SettingsPanel::drawContentBrowserTab(EditorContext& /*ctx*/) {
    ImGui::TextUnformatted("Content Browser Preferences");
    ImGui::TextDisabled("Asset display density and file visibility.");
    ImGui::Separator();
    ImGui::Spacing();

    int viewMode = pendingPrefs_.contentBrowser.viewModeGrid ? 0 : 1;
    const char* viewModes[] = {"Grid View (Cards)", "List View (Details)"};
    if (ImGui::Combo("Default Presentation Mode", &viewMode, viewModes, 2)) {
        pendingPrefs_.contentBrowser.viewModeGrid = (viewMode == 0);
    }

    ImGui::SliderFloat("Card Thumbnail Size", &pendingPrefs_.contentBrowser.thumbnailSize, 48.0F,
                       128.0F, "%.0f px");
    ImGui::Checkbox("Show File Extensions in Asset Names",
                    &pendingPrefs_.contentBrowser.showFileExtensions);
    ImGui::Checkbox("Show Hidden Files (dotfiles)", &pendingPrefs_.contentBrowser.showHiddenFiles);
}

void SettingsPanel::drawKeyboardTab(EditorContext& /*ctx*/) {
    const auto& theme = getActiveThemeColors();

    ImGui::TextUnformatted("Keyboard Hotkeys & Shortcuts");
    ImGui::TextDisabled("Standard keybindings reference for editor operations.");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::BeginTable("HotkeysTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 220.0F);
        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        auto addRow = [&theme](const char* action, const char* shortcut) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(action);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(toImVec4(theme.accentPrimary), "%s", shortcut);
        };

        addRow("Start / Resume Simulation", "F5 / Ctrl+P");
        addRow("Pause Simulation", "Ctrl+P");
        addRow("Advance Single Frame Step", "F10");
        addRow("Stop Simulation & Restore State", "Shift+F5 / Ctrl+X");
        addRow("Save Current Scene", "Ctrl+S");
        addRow("Duplicate Selected Entity", "Ctrl+D");
        addRow("Delete Selected Entity", "Delete");
        addRow("Focus Viewport on Selection", "F");
        addRow("Open Settings & Preferences", "Ctrl+Shift+P");

        ImGui::EndTable();
    }
}

void SettingsPanel::drawPerformanceTab(EditorContext& /*ctx*/) {
    ImGui::TextUnformatted("Editor Performance & Diagnostics");
    ImGui::TextDisabled("Background throttling and real-time telemetry options.");
    ImGui::Separator();
    ImGui::Spacing();

    int bgFps = static_cast<int>(pendingPrefs_.performance.maxBackgroundFps);
    if (ImGui::SliderInt("Background Target FPS", &bgFps, 10, 120, "%d fps")) {
        pendingPrefs_.performance.maxBackgroundFps = static_cast<uint32_t>(bgFps);
    }
    helpMarker("Frame rate limit applied when editor window loses focus.");

    ImGui::Checkbox("Throttle Rendering When Unfocused",
                    &pendingPrefs_.performance.throttleWhenUnfocused);
    ImGui::Checkbox("Show Viewport Telemetry Overlay",
                    &pendingPrefs_.performance.showTelemetryOverlay);
}

// -----------------------------------------------------------------------------
// PROJECT GRAPHICS SETTINGS TABS (Preserving existing Milestone 10/11A systems)
// -----------------------------------------------------------------------------

void SettingsPanel::drawQualityTab(EditorContext& /*ctx*/) {
    ImGui::TextUnformatted("Global Graphics Quality & Fidelity Presets");
    ImGui::TextDisabled("Scalability settings stored in assets/config/graphics_settings.json.");
    ImGui::Separator();
    ImGui::Spacing();

    // Preset Selector
    render::GraphicsQualityPreset currentPreset = pendingGraphics_.detectPreset();
    const char* presets[] = {"Low", "Medium", "High", "Ultra", "Cinematic", "Custom"};
    int presetIdx = (currentPreset == render::GraphicsQualityPreset::Custom)
                        ? 5
                        : static_cast<int>(currentPreset);

    if (ImGui::Combo("Quality Preset", &presetIdx, presets, 6)) {
        if (presetIdx < 5) {
            pendingGraphics_ = render::GraphicsSettings::createPreset(
                static_cast<render::GraphicsQualityPreset>(presetIdx));
        }
    }
    helpMarker("One-click scalability baseline across all rendering subsystems.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Render Scale
    ImGui::SliderFloat("Viewport Render Scale", &pendingGraphics_.quality.renderScale, 0.5F, 2.0F,
                       "%.2fx");

    // Texture Quality
    const char* texQualities[] = {"Low", "Medium", "High", "Ultra"};
    int texIdx = static_cast<int>(pendingGraphics_.quality.textureQuality);
    if (ImGui::Combo("Texture Quality", &texIdx, texQualities, 4)) {
        pendingGraphics_.quality.textureQuality = static_cast<render::TextureQuality>(texIdx);
    }

    // Anisotropic Filtering
    const char* anisoOptions[] = {"Off", "2x", "4x", "8x", "16x"};
    int anisoIdx = 4;
    switch (pendingGraphics_.quality.anisotropicFiltering) {
    case render::AnisotropicFiltering::Off:
        anisoIdx = 0;
        break;
    case render::AnisotropicFiltering::X2:
        anisoIdx = 1;
        break;
    case render::AnisotropicFiltering::X4:
        anisoIdx = 2;
        break;
    case render::AnisotropicFiltering::X8:
        anisoIdx = 3;
        break;
    case render::AnisotropicFiltering::X16:
        anisoIdx = 4;
        break;
    }
    if (ImGui::Combo("Anisotropic Filtering", &anisoIdx, anisoOptions, 5)) {
        render::AnisotropicFiltering anisoValues[] = {
            render::AnisotropicFiltering::Off, render::AnisotropicFiltering::X2,
            render::AnisotropicFiltering::X4, render::AnisotropicFiltering::X8,
            render::AnisotropicFiltering::X16};
        pendingGraphics_.quality.anisotropicFiltering = anisoValues[anisoIdx];
    }

    // Anti-Aliasing
    const char* aaModes[] = {"None", "FXAA", "TAA", "MSAA 2x", "MSAA 4x"};
    int aaIdx = static_cast<int>(pendingGraphics_.quality.antiAliasing);
    if (ImGui::Combo("Anti-Aliasing", &aaIdx, aaModes, 5)) {
        pendingGraphics_.quality.antiAliasing = static_cast<render::AntiAliasingMode>(aaIdx);
    }
}

void SettingsPanel::drawLightingTab(EditorContext& /*ctx*/) {
    ImGui::TextUnformatted("Lighting & Environment Scalability");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Checkbox("Enable Direct Sun Lighting", &pendingGraphics_.lighting.directLighting);
    ImGui::SliderFloat("Environment Light Intensity",
                       &pendingGraphics_.lighting.environmentIntensity, 0.0F, 10.0F, "%.2f");
    ImGui::SliderFloat("Camera Exposure", &pendingGraphics_.lighting.exposure, 0.01F, 10.0F,
                       "%.2f");
}

void SettingsPanel::drawShadowsTab(EditorContext& /*ctx*/) {
    ImGui::TextUnformatted("Shadow Map Resolution & Filtering");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Checkbox("Enable Directional Shadows", &pendingGraphics_.shadows.enabled);

    if (pendingGraphics_.shadows.enabled) {
        const char* resOptions[] = {"512 x 512", "1024 x 1024", "2048 x 2048", "4096 x 4096"};
        int resIdx = 1;
        if (pendingGraphics_.shadows.resolution == 512)
            resIdx = 0;
        else if (pendingGraphics_.shadows.resolution == 1024)
            resIdx = 1;
        else if (pendingGraphics_.shadows.resolution == 2048)
            resIdx = 2;
        else if (pendingGraphics_.shadows.resolution == 4096)
            resIdx = 3;

        if (ImGui::Combo("Shadow Map Resolution", &resIdx, resOptions, 4)) {
            uint32_t resolutions[] = {512, 1024, 2048, 4096};
            pendingGraphics_.shadows.resolution = resolutions[resIdx];
        }

        const char* pcfQualities[] = {"Hard (1-tap)", "Medium (PCF 3x3)", "High (PCF 5x5)"};
        int pcfIdx = static_cast<int>(pendingGraphics_.shadows.pcfQuality);
        if (ImGui::Combo("Shadow Filtering", &pcfIdx, pcfQualities, 3)) {
            pendingGraphics_.shadows.pcfQuality = static_cast<render::ShadowPcfQuality>(pcfIdx);
        }

        ImGui::SliderFloat("Shadow Projection Distance", &pendingGraphics_.shadows.distance, 10.0F,
                           200.0F, "%.1f m");
        ImGui::SliderFloat("Shadow Depth Bias", &pendingGraphics_.shadows.depthBias, 0.0001F, 0.05F,
                           "%.4f");
        ImGui::SliderFloat("Slope Scaled Bias", &pendingGraphics_.shadows.slopeScaledBias, 0.0F,
                           5.0F, "%.2f");
    }
}

void SettingsPanel::drawPostProcessTab(EditorContext& /*ctx*/) {
    ImGui::TextUnformatted("HDR Tone Mapping & Post-Processing");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Checkbox("Enable Post-Processing Pipeline", &pendingGraphics_.postProcess.enabled);

    if (pendingGraphics_.postProcess.enabled) {
        const char* toneOps[] = {"ACES Fitted", "Filmic Reinhard", "Neutral", "Bypass (Raw HDR)"};
        int toneIdx = static_cast<int>(pendingGraphics_.postProcess.toneMapOp);
        if (ImGui::Combo("Tone Mapping Operator", &toneIdx, toneOps, 4)) {
            pendingGraphics_.postProcess.toneMapOp = static_cast<render::ToneMapOperator>(toneIdx);
        }

        ImGui::SliderFloat("Exposure Compensation", &pendingGraphics_.postProcess.exposure, 0.1F,
                           10.0F, "%.2f");
        if (pendingGraphics_.postProcess.toneMapOp == render::ToneMapOperator::FilmicReinhard) {
            ImGui::SliderFloat("Reinhard White Point",
                               &pendingGraphics_.postProcess.reinhardWhitePoint, 1.0F, 20.0F,
                               "%.1f");
        }

        ImGui::Spacing();
        ImGui::Checkbox("Enable HDR Bloom", &pendingGraphics_.postProcess.bloomEnabled);
        if (pendingGraphics_.postProcess.bloomEnabled) {
            ImGui::SliderFloat("Bloom Threshold", &pendingGraphics_.postProcess.bloomThreshold,
                               0.1F, 5.0F, "%.2f");
            ImGui::SliderFloat("Bloom Knee", &pendingGraphics_.postProcess.bloomKnee, 0.0F, 1.0F,
                               "%.2f");
            ImGui::SliderFloat("Bloom Intensity", &pendingGraphics_.postProcess.bloomIntensity,
                               0.01F, 0.5F, "%.3f");
            ImGui::SliderFloat("Bloom Filter Radius",
                               &pendingGraphics_.postProcess.bloomFilterRadius, 0.5F, 4.0F,
                               "%.1f px");
        }
    }
}

void SettingsPanel::drawDisplayTab(EditorContext& /*ctx*/) {
    ImGui::TextUnformatted("Display & Frame Synchronization");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Checkbox("Vertical Sync (VSync)", &pendingGraphics_.performance.vsync);
    int limit = static_cast<int>(pendingGraphics_.performance.frameRateLimit);
    if (ImGui::SliderInt("Frame Rate Limit (0 = Uncapped)", &limit, 0, 240, "%d fps")) {
        pendingGraphics_.performance.frameRateLimit = static_cast<uint32_t>(limit);
    }
    ImGui::Checkbox("Dynamic Resolution Scaling", &pendingGraphics_.performance.dynamicResolution);
    ImGui::Checkbox("Enable GPU Pass Profiling", &pendingGraphics_.performance.gpuProfiling);
    ImGui::Checkbox("Enable CPU Profiling", &pendingGraphics_.performance.cpuProfiling);
}

void SettingsPanel::drawDebugTab(EditorContext& /*ctx*/) {
    ImGui::TextUnformatted("Diagnostic & Visualization Modes");
    ImGui::Separator();
    ImGui::Spacing();

    const char* debugViews[] = {"Default Shaded (PBR)", "Wireframe Mesh", "World Normals",
                                "Albedo Only",          "Roughness Only", "Metallic Only",
                                "Shadow Cascade Map"};
    int viewIdx = static_cast<int>(pendingGraphics_.debug.viewMode);
    if (ImGui::Combo("Render Debug View", &viewIdx, debugViews, 7)) {
        pendingGraphics_.debug.viewMode = static_cast<render::DebugViewMode>(viewIdx);
    }

    ImGui::Checkbox("Visualize Shadow Map Depth", &pendingGraphics_.debug.visualizeShadowMap);
    ImGui::Checkbox("Show Pass Timings Overlay", &pendingGraphics_.debug.showPassTimings);
}

void SettingsPanel::drawFooter(EditorContext& ctx) {
    const auto& theme = getActiveThemeColors();

    ImGui::Separator();
    ImGui::Spacing();

    if (isPreferencesCategory(activeCategory_)) {
        // PREFERENCES FOOTER
        if (ImGui::Button("Save Preferences", ImVec2(140.0F, 30.0F))) {
            pendingPrefs_.scripting.customExecutablePath = customExecBuffer_;
            pendingPrefs_.scripting.argumentsTemplate = argsTemplateBuffer_;
            if (ctx.editorEngine != nullptr) {
                if (ctx.editorEngine->applyEditorPreferences(pendingPrefs_) &&
                    ctx.editorEngine->saveEditorPreferences()) {
                    activePrefs_ = pendingPrefs_;
                    statusMessage_ = "Preferences saved to disk!";
                    statusTimer_ = 3.0F;
                } else {
                    statusMessage_ = "Failed saving preferences.";
                    statusTimer_ = 4.0F;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Defaults", ImVec2(130.0F, 30.0F))) {
            pendingPrefs_.resetToDefaults();
            std::snprintf(customExecBuffer_, sizeof(customExecBuffer_), "%s",
                          pendingPrefs_.scripting.customExecutablePath.c_str());
            std::snprintf(argsTemplateBuffer_, sizeof(argsTemplateBuffer_), "%s",
                          pendingPrefs_.scripting.argumentsTemplate.c_str());
            statusMessage_ = "Preferences reset to defaults (click Save to persist).";
            statusTimer_ = 3.0F;
        }
    } else {
        // PROJECT GRAPHICS SETTINGS FOOTER
        bool hasChanges = !(pendingGraphics_ == activeGraphics_);

        if (ImGui::Button("Apply", ImVec2(90.0F, 30.0F))) {
            if (ctx.editorEngine != nullptr) {
                if (ctx.editorEngine->applyGraphicsSettings(pendingGraphics_)) {
                    activeGraphics_ = pendingGraphics_;
                    statusMessage_ = "Graphics settings applied!";
                    statusTimer_ = 3.0F;
                } else {
                    statusMessage_ = "Failed applying graphics settings.";
                    statusTimer_ = 4.0F;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save to Project", ImVec2(130.0F, 30.0F))) {
            if (ctx.editorEngine != nullptr) {
                if (ctx.editorEngine->applyGraphicsSettings(pendingGraphics_) &&
                    ctx.editorEngine->saveGraphicsSettings()) {
                    activeGraphics_ = pendingGraphics_;
                    statusMessage_ = "Saved to assets/config/graphics_settings.json!";
                    statusTimer_ = 3.0F;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert", ImVec2(90.0F, 30.0F))) {
            pendingGraphics_ = activeGraphics_;
            statusMessage_ = "Reverted changes to active settings.";
            statusTimer_ = 3.0F;
        }

        if (hasChanges) {
            ImGui::SameLine();
            drawStatusPill("Unapplied Changes", theme.statusWarning);
        }
    }

    // Status Message
    if (!statusMessage_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(toImVec4(theme.accentPrimary), "%s", statusMessage_.c_str());
        statusTimer_ -= ImGui::GetIO().DeltaTime;
        if (statusTimer_ <= 0.0F) {
            statusMessage_.clear();
        }
    }
}

} // namespace eng::editor
