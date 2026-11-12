/// @file project_settings_panel.cpp
/// @brief Centralized Graphics Settings & Scalability editor configuration panel implementation.

#include "panels/project_settings_panel.h"

#include "editor/editor_engine.h"
#include "editor/editor_theme.h"
#include "editor/ui_helpers.h"
#include "imgui_utils.h"
#include "render/graphics_settings_serializer.h"
#include "render/scene_renderer.h"
#include "rhi/rhi_device.h"

#include <algorithm>
#include <cctype>
#include <cstring>
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

} // namespace

ProjectSettingsPanel::ProjectSettingsPanel() {
    isOpen_ = false;
    pendingSettings_ = render::GraphicsSettings::createPreset(render::GraphicsQualityPreset::High);
    activeSettings_ = pendingSettings_;
}

bool ProjectSettingsPanel::matchesSearch(const char* label) const noexcept {
    if (searchBuffer_[0] == '\0') {
        return true;
    }
    return stringContainsCaseInsensitive(label, searchBuffer_);
}

void ProjectSettingsPanel::draw(EditorContext& ctx) {
    if (!isOpen_) {
        return;
    }

    if (!initialized_ && ctx.editorEngine != nullptr) {
        activeSettings_ = ctx.editorEngine->getGraphicsSettings();
        pendingSettings_ = activeSettings_;
        initialized_ = true;
    }

    ImGui::SetNextWindowSize(ImVec2(820.0F, 600.0F), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Project Settings", &isOpen_)) {
        ImGui::End();
        return;
    }

    const auto& theme = getActiveThemeColors();

    // 1. Top Search Header
    ImGui::BeginGroup();
    ImGui::PushItemWidth(320.0F);
    (void)drawSearchField("SettingsFilter", searchBuffer_, sizeof(searchBuffer_),
                          "Filter graphics settings...");
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (searchBuffer_[0] != '\0') {
        if (ImGui::SmallButton("Clear")) {
            searchBuffer_[0] = '\0';
        }
        ImGui::SameLine();
    }

    // Active Preset Badge
    render::GraphicsQualityPreset currentPreset = pendingSettings_.detectPreset();
    const char* presetName = presetToString(currentPreset);
    if (currentPreset == render::GraphicsQualityPreset::Custom) {
        drawBadge("Custom Fidelity", theme.statusWarning);
    } else {
        drawBadge(presetName, theme.accentPrimary);
    }

    bool hasPendingChanges = !(pendingSettings_ == activeSettings_);
    if (hasPendingChanges) {
        ImGui::SameLine();
        drawStatusPill("Unapplied Changes", theme.statusWarning);
    }
    ImGui::EndGroup();

    ImGui::Separator();

    // 2. Main Two-Column Navigation & Content Layout
    const float sidebarWidth = 200.0F;
    const float footerHeight = 44.0F;
    const float contentHeight = ImGui::GetContentRegionAvail().y - footerHeight;

    // Sidebar
    ImGui::BeginChild("SettingsSidebar", ImVec2(sidebarWidth, contentHeight), true);
    drawCategorySidebar();
    ImGui::EndChild();

    ImGui::SameLine();

    // Content Area
    ImGui::BeginChild("SettingsContent", ImVec2(0.0F, contentHeight), true);
    switch (activeCategory_) {
    case Category::Quality:
        drawQualityTab(ctx);
        break;
    case Category::Lighting:
        drawLightingTab(ctx);
        break;
    case Category::Shadows:
        drawShadowsTab(ctx);
        break;
    case Category::PostProcess:
        drawPostProcessTab(ctx);
        break;
    case Category::Performance:
        drawPerformanceTab(ctx);
        break;
    case Category::Debug:
        drawDebugTab(ctx);
        break;
    }
    ImGui::EndChild();

    // 3. Footer Control Bar
    drawFooter(ctx);

    ImGui::End();
}

void ProjectSettingsPanel::drawCategorySidebar() {
    auto drawNavItem = [&](Category cat, const char* label, const char* badge = nullptr) {
        bool selected = (activeCategory_ == cat);
        if (ImGui::Selectable(label, selected)) {
            activeCategory_ = cat;
        }
        if (badge != nullptr) {
            ImGui::SameLine(160.0F);
            ImGui::TextDisabled("%s", badge);
        }
    };

    drawNavItem(Category::Quality, "Graphics & Quality");
    drawNavItem(Category::Lighting, "Lighting");
    drawNavItem(Category::Shadows, "Shadows");
    drawNavItem(Category::PostProcess, "Post-Processing");
    drawNavItem(Category::Performance, "Performance");
    drawNavItem(Category::Debug, "Debug Rendering");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("HIKARI SCALABILITY");
    ImGui::TextDisabled("Version 1.0 (M11B)");
}

void ProjectSettingsPanel::drawQualityTab(EditorContext& ctx) {
    if (!matchesSearch("Quality") && !matchesSearch("Preset") && !matchesSearch("Scale") &&
        !matchesSearch("Anisotropic") && !matchesSearch("AntiAliasing")) {
        ImGui::TextDisabled("No settings match filter '%s'", searchBuffer_);
        return;
    }

    drawSectionHeader("Fidelity & Quality Presets");

    // Preset Selector
    const char* presets[] = {"Low", "Medium", "High", "Ultra", "Cinematic", "Custom"};
    int currentPresetIdx = static_cast<int>(pendingSettings_.detectPreset());
    if (ImGui::Combo("Quality Preset", &currentPresetIdx, presets, IM_ARRAYSIZE(presets))) {
        if (currentPresetIdx >= 0 && currentPresetIdx <= 4) {
            pendingSettings_.applyPreset(
                static_cast<render::GraphicsQualityPreset>(currentPresetIdx));
        }
    }
    helpMarker("Applies standardized baseline fidelity configurations balanced for diverse GPU "
               "tiers.\n- Low: Fast performance, SDR path, 512 shadows.\n- Medium: 1024 shadows, "
               "standard bloom, 4x Aniso.\n- High: 2048 shadows, 16x Aniso, full ACES HDR.\n- "
               "Ultra: 4096 shadows, max PCF filter.\n- Cinematic: 1.25x Supersampling, 4096 "
               "shadows.");

    ImGui::Spacing();
    drawSectionHeader("Geometry & Resolution Scalability");

    // Render Scale
    float scale = pendingSettings_.quality.renderScale;
    if (ImGui::SliderFloat("Render Scale", &scale, 0.50F, 2.00F, "%.2fx")) {
        pendingSettings_.quality.renderScale = scale;
    }
    helpMarker("Scales the internal offscreen 3D rendering resolution relative to viewport "
               "dimensions.\n<1.0x provides major performance savings on fill-rate bound GPUs.\n>1."
               "0x provides full-scene spatial supersampling.");

    // Texture Anisotropy
    const char* anisoModes[] = {"Off", "2x", "4x", "8x", "16x"};
    int anisoIdx = 0;
    switch (pendingSettings_.quality.anisotropicFiltering) {
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

    bool anisoSupported =
        (ctx.rhiDevice != nullptr && ctx.rhiDevice->getCaps().features.samplerAnisotropy);
    if (!anisoSupported) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Combo("Anisotropic Filtering", &anisoIdx, anisoModes, IM_ARRAYSIZE(anisoModes))) {
        switch (anisoIdx) {
        case 0:
            pendingSettings_.quality.anisotropicFiltering = render::AnisotropicFiltering::Off;
            break;
        case 1:
            pendingSettings_.quality.anisotropicFiltering = render::AnisotropicFiltering::X2;
            break;
        case 2:
            pendingSettings_.quality.anisotropicFiltering = render::AnisotropicFiltering::X4;
            break;
        case 3:
            pendingSettings_.quality.anisotropicFiltering = render::AnisotropicFiltering::X8;
            break;
        case 4:
            pendingSettings_.quality.anisotropicFiltering = render::AnisotropicFiltering::X16;
            break;
        }
    }
    helpMarker("Hardware anisotropic filtering level for oblique texture sampling clarity.\nHigh "
               "(16x) recommended on modern discrete GPUs (Pascal, Turing, RDNA).");
    if (!anisoSupported) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextColored(toImVec4(getActiveThemeColors().statusWarning),
                           "[Unsupported by Device]");
    }

    // Texture Quality
    const char* texQualities[] = {"Low", "Medium", "High", "Ultra"};
    int texIdx = static_cast<int>(pendingSettings_.quality.textureQuality);
    if (ImGui::Combo("Texture Quality", &texIdx, texQualities, IM_ARRAYSIZE(texQualities))) {
        pendingSettings_.quality.textureQuality = static_cast<render::TextureQuality>(texIdx);
    }
    helpMarker("Controls texture mipmap residency and texture memory footprint.");

    // Anti-Aliasing Mode
    const char* aaModes[] = {"None", "FXAA (Fast Approximate)", "TAA [Planned: M11C+]",
                             "MSAA 2x [Planned: M11C+]", "MSAA 4x [Planned: M11C+]"};
    int aaIdx = static_cast<int>(pendingSettings_.quality.antiAliasing);
    if (ImGui::Combo("Anti-Aliasing", &aaIdx, aaModes, IM_ARRAYSIZE(aaModes))) {
        if (aaIdx <= 1) {
            pendingSettings_.quality.antiAliasing = static_cast<render::AntiAliasingMode>(aaIdx);
        } else {
            // Revert planned modes
            pendingSettings_.quality.antiAliasing = render::AntiAliasingMode::None;
        }
    }
    helpMarker("Edge antialiasing technique.\nFXAA is single-pass post-process smoothing.\nTAA "
               "and MSAA are scheduled for Milestone 11C+.");
}

void ProjectSettingsPanel::drawLightingTab(EditorContext& /*ctx*/) {
    if (!matchesSearch("Lighting") && !matchesSearch("Sun") && !matchesSearch("Ambient") &&
        !matchesSearch("IBL") && !matchesSearch("Exposure") && !matchesSearch("SSAO")) {
        ImGui::TextDisabled("No settings match filter '%s'", searchBuffer_);
        return;
    }

    drawSectionHeader("Direct & Ambient Lighting");

    ImGui::Checkbox("Enable Direct Sunlight", &pendingSettings_.lighting.directLighting);
    helpMarker(
        "Toggles evaluation of directional sun and analytical light sources in the Cook-Torrance "
        "PBR shader.");

    float envIntensity = pendingSettings_.lighting.environmentIntensity;
    if (ImGui::SliderFloat("Ambient Intensity", &envIntensity, 0.0F, 5.0F, "%.2f")) {
        pendingSettings_.lighting.environmentIntensity = envIntensity;
    }
    helpMarker("Global baseline ambient illuminance applied across shadowed surface recesses.");

    float exp = pendingSettings_.lighting.exposure;
    if (ImGui::SliderFloat("Exposure Compensation", &exp, 0.0F, 5.0F, "%.2f")) {
        pendingSettings_.lighting.exposure = exp;
    }
    helpMarker("Linear photometric exposure scaling factor for the primary lighting pass.");

    float wb = pendingSettings_.lighting.whiteBalance;
    if (ImGui::SliderFloat("White Balance", &wb, -1.0F, 1.0F, "%.2f")) {
        pendingSettings_.lighting.whiteBalance = wb;
    }
    helpMarker("Adjusts correlated color temperature from cool (-1.0) to warm (+1.0).");

    ImGui::Spacing();
    drawSectionHeader("Advanced Global Illumination [Planned: M11C+]");

    ImGui::BeginDisabled();
    bool iblDummy = false;
    ImGui::Checkbox("Image-Based Lighting (IBL / HDRI)", &iblDummy);
    ImGui::SameLine();
    ImGui::TextDisabled("[Planned: Milestone 11C]");
    helpMarker("Split-sum specular environment map prefiltering and diffuse irradiance spherical "
               "harmonics.");

    bool ssaoDummy = false;
    ImGui::Checkbox("Screen-Space Ambient Occlusion (SSAO)", &ssaoDummy);
    ImGui::SameLine();
    ImGui::TextDisabled("[Planned: Milestone 11C]");
    helpMarker("High-frequency contact shadows and ambient light occlusion in geometric crevices.");

    bool ssrDummy = false;
    ImGui::Checkbox("Screen-Space Reflections (SSR)", &ssrDummy);
    ImGui::SameLine();
    ImGui::TextDisabled("[Planned: Milestone 11D]");
    helpMarker("Dynamic real-time ray-marched specular reflections of visible scene geometry.");
    ImGui::EndDisabled();
}

void ProjectSettingsPanel::drawShadowsTab(EditorContext& ctx) {
    if (!matchesSearch("Shadow") && !matchesSearch("Resolution") && !matchesSearch("Bias") &&
        !matchesSearch("Distance") && !matchesSearch("PCF")) {
        ImGui::TextDisabled("No settings match filter '%s'", searchBuffer_);
        return;
    }

    drawSectionHeader("Directional Shadow Mapping");

    ImGui::Checkbox("Enable Dynamic Shadows", &pendingSettings_.shadows.enabled);
    helpMarker("Master toggle for the directional shadow depth pass and PBR shadow receiver "
               "sampling.");

    if (!pendingSettings_.shadows.enabled) {
        ImGui::BeginDisabled();
    }

    const char* resolutions[] = {"512 x 512", "1024 x 1024", "2048 x 2048", "4096 x 4096"};
    int resIdx = 2;
    switch (pendingSettings_.shadows.resolution) {
    case 512:
        resIdx = 0;
        break;
    case 1024:
        resIdx = 1;
        break;
    case 2048:
        resIdx = 2;
        break;
    case 4096:
        resIdx = 3;
        break;
    default:
        resIdx = 2;
        break;
    }

    uint32_t maxTexDim =
        (ctx.rhiDevice != nullptr) ? ctx.rhiDevice->getCaps().limits.maxTextureDimension2D : 4096;
    if (ImGui::Combo("Shadow Map Resolution", &resIdx, resolutions, IM_ARRAYSIZE(resolutions))) {
        uint32_t chosen = 2048;
        if (resIdx == 0)
            chosen = 512;
        else if (resIdx == 1)
            chosen = 1024;
        else if (resIdx == 2)
            chosen = 2048;
        else if (resIdx == 3)
            chosen = 4096;

        if (chosen <= maxTexDim) {
            pendingSettings_.shadows.resolution = chosen;
        }
    }
    helpMarker("Width and height of the offscreen D32_FLOAT shadow depth map texture.\nHigher "
               "resolution eliminates geometric shadow aliasing.");

    float dist = pendingSettings_.shadows.distance;
    if (ImGui::SliderFloat("Shadow Distance", &dist, 10.0F, 200.0F, "%.0f m")) {
        pendingSettings_.shadows.distance = dist;
    }
    helpMarker("Maximum effective distance from origin/camera covered by the directional shadow "
               "projection volume.");

    float depthBias = pendingSettings_.shadows.depthBias;
    if (ImGui::SliderFloat("Constant Depth Bias", &depthBias, 0.0001F, 0.05F, "%.5f",
                           ImGuiSliderFlags_Logarithmic)) {
        pendingSettings_.shadows.depthBias = depthBias;
    }
    helpMarker("Offset added during depth comparison to eliminate shadow acne on surface planes.");

    float slopeBias = pendingSettings_.shadows.slopeScaledBias;
    if (ImGui::SliderFloat("Slope-Scaled Bias", &slopeBias, 0.0F, 2.0F, "%.2f")) {
        pendingSettings_.shadows.slopeScaledBias = slopeBias;
    }
    helpMarker("Additional depth bias scaled by surface polygon normal tilt relative to light "
               "direction.");

    ImGui::Checkbox("Soft Shadows (Filtering)", &pendingSettings_.shadows.softShadows);
    helpMarker("Enables multi-sample Percentage-Closer Filtering (PCF) for soft penumbra edges.");

    const char* pcfQualities[] = {"Hard (1 tap)", "Medium (3x3 - 9 taps)", "High (5x5 - 25 taps)"};
    int pcfIdx = static_cast<int>(pendingSettings_.shadows.pcfQuality);
    if (ImGui::Combo("PCF Kernel Quality", &pcfIdx, pcfQualities, IM_ARRAYSIZE(pcfQualities))) {
        pendingSettings_.shadows.pcfQuality = static_cast<render::ShadowPcfQuality>(pcfIdx);
    }
    helpMarker("Sampling density of the PCF kernel. High (5x5) provides ultra-smooth penumbra "
               "transitions.");

    if (!pendingSettings_.shadows.enabled) {
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    drawSectionHeader("Cascaded Shadow Maps (CSM) [Planned: M11C+]");

    ImGui::BeginDisabled();
    bool csmDummy = false;
    ImGui::Checkbox("Enable 4-Split Cascaded Shadow Maps", &csmDummy);
    ImGui::SameLine();
    ImGui::TextDisabled("[Planned: Milestone 11C]");
    helpMarker("Splits camera view frustum into multiple logarithmic cascades to maintain crisp "
               "shadow resolution near camera and distant horizon simultaneously.");
    ImGui::EndDisabled();
}

void ProjectSettingsPanel::drawPostProcessTab(EditorContext& /*ctx*/) {
    if (!matchesSearch("Post") && !matchesSearch("Tone") && !matchesSearch("Bloom") &&
        !matchesSearch("Exposure") && !matchesSearch("ACES")) {
        ImGui::TextDisabled("No settings match filter '%s'", searchBuffer_);
        return;
    }

    drawSectionHeader("Tone Mapping & Display Calibration");

    ImGui::Checkbox("Enable Post-Processing Pipeline", &pendingSettings_.postProcess.enabled);
    helpMarker("When disabled, scene renders directly into an SDR target without bloom or tone "
               "mapping.");

    if (!pendingSettings_.postProcess.enabled) {
        ImGui::BeginDisabled();
    }

    const char* toneMappers[] = {"ACES Fitted (Cinematic)", "Filmic Extended Reinhard",
                                 "Khronos PBR Neutral", "Linear Bypass"};
    int toneIdx = static_cast<int>(pendingSettings_.postProcess.toneMapOp);
    if (ImGui::Combo("Tone Map Operator", &toneIdx, toneMappers, IM_ARRAYSIZE(toneMappers))) {
        pendingSettings_.postProcess.toneMapOp = static_cast<render::ToneMapOperator>(toneIdx);
    }
    helpMarker("Selected tone reproduction curve mapping HDR scene radiance to display dynamic "
               "range.\n- ACES Fitted: Cinematic contrast and highlight roll-off.\n- Filmic "
               "Reinhard: Clean highlight compression with white point preservation.\n- Neutral: "
               "Industry-standard glTF PBR color fidelity.\n- Bypass: Direct linear clamping.");

    float exp = pendingSettings_.postProcess.exposure;
    if (ImGui::SliderFloat("Post-Exposure", &exp, 0.10F, 10.00F, "%.2f",
                           ImGuiSliderFlags_Logarithmic)) {
        pendingSettings_.postProcess.exposure = exp;
    }
    helpMarker("Photometric pre-exposure multiplier applied before tone mapping compression.");

    if (pendingSettings_.postProcess.toneMapOp == render::ToneMapOperator::FilmicReinhard) {
        float white = pendingSettings_.postProcess.reinhardWhitePoint;
        if (ImGui::SliderFloat("Reinhard White Point", &white, 1.0F, 50.0F, "%.1f")) {
            pendingSettings_.postProcess.reinhardWhitePoint = white;
        }
        helpMarker("Minimum scene luminance mapped to pure display white (1.0).");
    }

    ImGui::Spacing();
    drawSectionHeader("Multi-Scale Bloom Filter");

    ImGui::Checkbox("Enable Bloom Pass", &pendingSettings_.postProcess.bloomEnabled);
    helpMarker("Generates physical lens glow using an 8-level downsample/upsample progressive blur "
               "pyramid.");

    if (!pendingSettings_.postProcess.bloomEnabled) {
        ImGui::BeginDisabled();
    }

    float threshold = pendingSettings_.postProcess.bloomThreshold;
    if (ImGui::SliderFloat("Threshold", &threshold, 0.0F, 20.0F, "%.2f")) {
        pendingSettings_.postProcess.bloomThreshold = threshold;
    }
    helpMarker("Luminance threshold above which surface highlights begin blooming.");

    float knee = pendingSettings_.postProcess.bloomKnee;
    if (ImGui::SliderFloat("Soft Knee", &knee, 0.0F, 2.0F, "%.2f")) {
        pendingSettings_.postProcess.bloomKnee = knee;
    }
    helpMarker("Half-width transition range for smooth soft-knee luminance extraction.");

    float intensity = pendingSettings_.postProcess.bloomIntensity;
    if (ImGui::SliderFloat("Composite Intensity", &intensity, 0.0F, 0.50F, "%.3f")) {
        pendingSettings_.postProcess.bloomIntensity = intensity;
    }
    helpMarker(
        "Scale factor applied to the blurred bloom pyramid during final composite into color "
        "target.");

    float radius = pendingSettings_.postProcess.bloomFilterRadius;
    if (ImGui::SliderFloat("Filter Radius", &radius, 0.1F, 4.0F, "%.2f")) {
        pendingSettings_.postProcess.bloomFilterRadius = radius;
    }
    helpMarker("Spatial sampling spread radius for the 9-tap progressive upsampling filter.");

    if (!pendingSettings_.postProcess.bloomEnabled) {
        ImGui::EndDisabled();
    }

    if (!pendingSettings_.postProcess.enabled) {
        ImGui::EndDisabled();
    }
}

void ProjectSettingsPanel::drawPerformanceTab(EditorContext& ctx) {
    if (!matchesSearch("Performance") && !matchesSearch("VSync") && !matchesSearch("FPS") &&
        !matchesSearch("Profiling") && !matchesSearch("Stats")) {
        ImGui::TextDisabled("No settings match filter '%s'", searchBuffer_);
        return;
    }

    drawSectionHeader("Display & Synchronization");

    bool vsync = pendingSettings_.performance.vsync;
    if (ImGui::Checkbox("Vertical Synchronization (VSync)", &vsync)) {
        pendingSettings_.performance.vsync = vsync;
    }
    helpMarker(
        "Locks frame presentation to monitor refresh rate (VK_PRESENT_MODE_FIFO_KHR) to "
        "eliminate tearing.\nWhen disabled, uses VK_PRESENT_MODE_MAILBOX_KHR for lowest input "
        "latency.");

    const char* fpsLimits[] = {"Uncapped (0)", "30 FPS", "60 FPS", "120 FPS", "144 FPS", "240 FPS"};
    int fpsIdx = 0;
    switch (pendingSettings_.performance.frameRateLimit) {
    case 0:
        fpsIdx = 0;
        break;
    case 30:
        fpsIdx = 1;
        break;
    case 60:
        fpsIdx = 2;
        break;
    case 120:
        fpsIdx = 3;
        break;
    case 144:
        fpsIdx = 4;
        break;
    case 240:
        fpsIdx = 5;
        break;
    default:
        fpsIdx = 0;
        break;
    }
    if (ImGui::Combo("Frame Rate Limit", &fpsIdx, fpsLimits, IM_ARRAYSIZE(fpsLimits))) {
        uint32_t chosen = 0;
        if (fpsIdx == 1)
            chosen = 30;
        else if (fpsIdx == 2)
            chosen = 60;
        else if (fpsIdx == 3)
            chosen = 120;
        else if (fpsIdx == 4)
            chosen = 144;
        else if (fpsIdx == 5)
            chosen = 240;
        pendingSettings_.performance.frameRateLimit = chosen;
    }
    helpMarker("Caps the maximum application simulation and render loop frequency.");

    ImGui::Spacing();
    drawSectionHeader("Instrumentation & Diagnostics");

    bool timestampSupported =
        (ctx.rhiDevice != nullptr && ctx.rhiDevice->getCaps().features.timestampQueries);
    if (!timestampSupported) {
        ImGui::BeginDisabled();
    }
    ImGui::Checkbox("GPU Timestamp Pass Profiling", &pendingSettings_.performance.gpuProfiling);
    helpMarker(
        "Enables per-pass hardware Vulkan timestamp query collection for accurate millisecond "
        "timings.");
    if (!timestampSupported) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextColored(toImVec4(getActiveThemeColors().statusWarning),
                           "[Unsupported by Device]");
    }

    ImGui::Checkbox("CPU Execution Profiling", &pendingSettings_.performance.cpuProfiling);
    helpMarker("Tracks monotonic CPU frame time breakdown across simulation, recording, and "
               "presentation.");

    ImGui::Checkbox("Render Statistics Telemetry", &pendingSettings_.performance.renderStats);
    helpMarker("Counts primitives, draw calls, culled meshes, and submesh allocations.");
}

void ProjectSettingsPanel::drawDebugTab(EditorContext& ctx) {
    if (!matchesSearch("Debug") && !matchesSearch("Wireframe") && !matchesSearch("Normals") &&
        !matchesSearch("Albedo") && !matchesSearch("Timings")) {
        ImGui::TextDisabled("No settings match filter '%s'", searchBuffer_);
        return;
    }

    drawSectionHeader("Diagnostic Rasterization & View Modes");

    const char* viewModes[] = {
        "Lit (Full PBR + HDR + Tonemapping)",
        "Wireframe (VK_POLYGON_MODE_LINE)",
        "Normals [Planned: M11C]",
        "Albedo [Planned: M11C]",
        "Roughness [Planned: M11C]",
        "Metallic [Planned: M11C]",
        "Shadow Map Depth [Planned: M11C]",
        "Lighting Complexity [Planned: M11C]",
    };
    int viewIdx = static_cast<int>(pendingSettings_.debug.viewMode);
    bool fillModeSupported =
        (ctx.rhiDevice != nullptr && ctx.rhiDevice->getCaps().features.fillModeNonSolid);

    if (ImGui::Combo("View Mode", &viewIdx, viewModes, IM_ARRAYSIZE(viewModes))) {
        if (viewIdx == 1 && !fillModeSupported) {
            // Cannot enable wireframe if GPU lacks non-solid fill mode
            pendingSettings_.debug.viewMode = render::DebugViewMode::Lit;
        } else if (viewIdx <= 1) {
            pendingSettings_.debug.viewMode = static_cast<render::DebugViewMode>(viewIdx);
        } else {
            // Planned modes revert to Lit for now
            pendingSettings_.debug.viewMode = render::DebugViewMode::Lit;
        }
    }
    helpMarker("Diagnostic rendering mode.\n- Lit: Authoritative full Cook-Torrance shading.\n- "
               "Wireframe: Non-solid triangle edge rasterization.\nOther modes are planned for "
               "Milestone 11C.");
    if (!fillModeSupported) {
        ImGui::TextColored(toImVec4(getActiveThemeColors().statusWarning),
                           "Note: Wireframe requires fillModeNonSolid GPU capability.");
    }

    ImGui::Checkbox("Visualize Directional Shadow Map", &pendingSettings_.debug.visualizeShadowMap);
    helpMarker("Overlays the directional shadow depth map texture in the top corner of the "
               "viewport.");

    ImGui::Checkbox("Show Pass Timings Overlay", &pendingSettings_.debug.showPassTimings);
    helpMarker("Displays live millisecond execution costs for Shadow Pass, Main PBR Pass, and Post "
               "Processing.");
}

void ProjectSettingsPanel::drawFooter(EditorContext& ctx) {
    const auto& theme = getActiveThemeColors();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    bool hasPendingChanges = !(pendingSettings_ == activeSettings_);

    if (ImGui::Button("Reset to Defaults", ImVec2(130.0F, 28.0F))) {
        pendingSettings_ =
            render::GraphicsSettings::createPreset(render::GraphicsQualityPreset::High);
    }
    helpMarker("Resets all settings back to official High fidelity preset.");

    ImGui::SameLine();
    if (ImGui::Button("Revert", ImVec2(90.0F, 28.0F))) {
        pendingSettings_ = activeSettings_;
    }
    helpMarker("Discards unapplied changes and restores currently active settings.");

    ImGui::SameLine();
    if (ImGui::Button("Save to Config", ImVec2(110.0F, 28.0F))) {
        if (ctx.editorEngine != nullptr) {
            (void)ctx.editorEngine->applyGraphicsSettings(pendingSettings_);
            activeSettings_ = pendingSettings_;
            if (ctx.editorEngine->saveGraphicsSettings()) {
                statusMessage_ = "Settings saved to assets/config/graphics_settings.json";
                statusTimer_ = 3.0F;
            }
        }
    }
    helpMarker(
        "Applies settings and saves them permanently to assets/config/graphics_settings.json.");

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(10.0F, 0.0F));
    ImGui::SameLine();

    if (!hasPendingChanges) {
        ImGui::BeginDisabled();
    }
    ImGui::PushStyleColor(ImGuiCol_Button, toImVec4(theme.accentPrimary));
    if (ImGui::Button("Apply Changes", ImVec2(130.0F, 28.0F))) {
        if (ctx.editorEngine != nullptr) {
            (void)ctx.editorEngine->applyGraphicsSettings(pendingSettings_);
            activeSettings_ = pendingSettings_;
            statusMessage_ = "Graphics settings applied successfully.";
            statusTimer_ = 3.0F;
        }
    }
    ImGui::PopStyleColor();
    if (!hasPendingChanges) {
        ImGui::EndDisabled();
    }

    if (!statusMessage_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(toImVec4(theme.statusSuccess), "%s", statusMessage_.c_str());
    }
}

} // namespace eng::editor
