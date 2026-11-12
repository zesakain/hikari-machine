/// @file graphics_settings_serializer.cpp
/// @brief Implementation of transactional JSON serialization for GraphicsSettings.

#include "render/graphics_settings_serializer.h"

#include "core/json.h"
#include "core/log.h"

#include <fstream>
#include <sstream>

namespace eng::render {

namespace {

using namespace core;

std::string_view presetToString(GraphicsQualityPreset p) noexcept {
    switch (p) {
    case GraphicsQualityPreset::Low:
        return "Low";
    case GraphicsQualityPreset::Medium:
        return "Medium";
    case GraphicsQualityPreset::High:
        return "High";
    case GraphicsQualityPreset::Ultra:
        return "Ultra";
    case GraphicsQualityPreset::Cinematic:
        return "Cinematic";
    case GraphicsQualityPreset::Custom:
        return "Custom";
    }
    return "High";
}

GraphicsQualityPreset stringToPreset(std::string_view s) noexcept {
    if (s == "Low")
        return GraphicsQualityPreset::Low;
    if (s == "Medium")
        return GraphicsQualityPreset::Medium;
    if (s == "High")
        return GraphicsQualityPreset::High;
    if (s == "Ultra")
        return GraphicsQualityPreset::Ultra;
    if (s == "Cinematic")
        return GraphicsQualityPreset::Cinematic;
    if (s == "Custom")
        return GraphicsQualityPreset::Custom;
    return GraphicsQualityPreset::High;
}

std::string_view textureQualityToString(TextureQuality q) noexcept {
    switch (q) {
    case TextureQuality::Low:
        return "Low";
    case TextureQuality::Medium:
        return "Medium";
    case TextureQuality::High:
        return "High";
    case TextureQuality::Ultra:
        return "Ultra";
    }
    return "High";
}

TextureQuality stringToTextureQuality(std::string_view s) noexcept {
    if (s == "Low")
        return TextureQuality::Low;
    if (s == "Medium")
        return TextureQuality::Medium;
    if (s == "High")
        return TextureQuality::High;
    if (s == "Ultra")
        return TextureQuality::Ultra;
    return TextureQuality::High;
}

std::string_view aaToString(AntiAliasingMode m) noexcept {
    switch (m) {
    case AntiAliasingMode::None:
        return "None";
    case AntiAliasingMode::FXAA:
        return "FXAA";
    case AntiAliasingMode::TAA:
        return "TAA";
    case AntiAliasingMode::MSAA2x:
        return "MSAA2x";
    case AntiAliasingMode::MSAA4x:
        return "MSAA4x";
    }
    return "None";
}

AntiAliasingMode stringToAntiAliasing(std::string_view s) noexcept {
    if (s == "FXAA")
        return AntiAliasingMode::FXAA;
    if (s == "TAA")
        return AntiAliasingMode::TAA;
    if (s == "MSAA2x")
        return AntiAliasingMode::MSAA2x;
    if (s == "MSAA4x")
        return AntiAliasingMode::MSAA4x;
    return AntiAliasingMode::None;
}

std::string_view pcfToString(ShadowPcfQuality q) noexcept {
    switch (q) {
    case ShadowPcfQuality::Hard:
        return "Hard";
    case ShadowPcfQuality::Medium:
        return "Medium";
    case ShadowPcfQuality::High:
        return "High";
    }
    return "Medium";
}

ShadowPcfQuality stringToPcf(std::string_view s) noexcept {
    if (s == "Hard")
        return ShadowPcfQuality::Hard;
    if (s == "High")
        return ShadowPcfQuality::High;
    return ShadowPcfQuality::Medium;
}

std::string_view toneMapToString(ToneMapOperator op) noexcept {
    switch (op) {
    case ToneMapOperator::AcesFitted:
        return "AcesFitted";
    case ToneMapOperator::FilmicReinhard:
        return "FilmicReinhard";
    case ToneMapOperator::Neutral:
        return "Neutral";
    case ToneMapOperator::Bypass:
        return "Bypass";
    }
    return "AcesFitted";
}

ToneMapOperator stringToToneMap(std::string_view s) noexcept {
    if (s == "FilmicReinhard")
        return ToneMapOperator::FilmicReinhard;
    if (s == "Neutral")
        return ToneMapOperator::Neutral;
    if (s == "Bypass")
        return ToneMapOperator::Bypass;
    return ToneMapOperator::AcesFitted;
}

std::string_view debugViewToString(DebugViewMode m) noexcept {
    switch (m) {
    case DebugViewMode::Lit:
        return "Lit";
    case DebugViewMode::Wireframe:
        return "Wireframe";
    case DebugViewMode::Normals:
        return "Normals";
    case DebugViewMode::Albedo:
        return "Albedo";
    case DebugViewMode::Roughness:
        return "Roughness";
    case DebugViewMode::Metallic:
        return "Metallic";
    case DebugViewMode::ShadowMap:
        return "ShadowMap";
    case DebugViewMode::LightingComplexity:
        return "LightingComplexity";
    }
    return "Lit";
}

DebugViewMode stringToDebugView(std::string_view s) noexcept {
    if (s == "Wireframe")
        return DebugViewMode::Wireframe;
    if (s == "Normals")
        return DebugViewMode::Normals;
    if (s == "Albedo")
        return DebugViewMode::Albedo;
    if (s == "Roughness")
        return DebugViewMode::Roughness;
    if (s == "Metallic")
        return DebugViewMode::Metallic;
    if (s == "ShadowMap")
        return DebugViewMode::ShadowMap;
    if (s == "LightingComplexity")
        return DebugViewMode::LightingComplexity;
    return DebugViewMode::Lit;
}

} // namespace

Result<std::string> GraphicsSettingsSerializer::serializeToString(const GraphicsSettings& settings,
                                                                  bool prettyPrint,
                                                                  int indentSpaces) {
    JsonValue root = JsonValue::makeObject();
    root.set("formatVersion", JsonValue(static_cast<int>(kCurrentVersion)));

    // Quality
    JsonValue qualityObj = JsonValue::makeObject();
    qualityObj.set("preset", JsonValue(presetToString(settings.quality.preset)));
    qualityObj.set("renderScale", JsonValue(static_cast<double>(settings.quality.renderScale)));
    qualityObj.set("anisotropicFiltering",
                   JsonValue(static_cast<int>(settings.quality.anisotropicFiltering)));
    qualityObj.set("antiAliasing", JsonValue(aaToString(settings.quality.antiAliasing)));
    qualityObj.set("textureQuality",
                   JsonValue(textureQualityToString(settings.quality.textureQuality)));
    root.set("quality", std::move(qualityObj));

    // Lighting
    JsonValue lightingObj = JsonValue::makeObject();
    lightingObj.set("directLighting", JsonValue(settings.lighting.directLighting));
    lightingObj.set("iblEnabled", JsonValue(settings.lighting.iblEnabled));
    lightingObj.set("environmentIntensity",
                    JsonValue(static_cast<double>(settings.lighting.environmentIntensity)));
    lightingObj.set("indirectLighting", JsonValue(settings.lighting.indirectLighting));
    lightingObj.set("exposure", JsonValue(static_cast<double>(settings.lighting.exposure)));
    lightingObj.set("whiteBalance", JsonValue(static_cast<double>(settings.lighting.whiteBalance)));
    root.set("lighting", std::move(lightingObj));

    // Shadows
    JsonValue shadowObj = JsonValue::makeObject();
    shadowObj.set("enabled", JsonValue(settings.shadows.enabled));
    shadowObj.set("resolution", JsonValue(static_cast<int>(settings.shadows.resolution)));
    shadowObj.set("distance", JsonValue(static_cast<double>(settings.shadows.distance)));
    shadowObj.set("depthBias", JsonValue(static_cast<double>(settings.shadows.depthBias)));
    shadowObj.set("slopeScaledBias",
                  JsonValue(static_cast<double>(settings.shadows.slopeScaledBias)));
    shadowObj.set("pcfQuality", JsonValue(pcfToString(settings.shadows.pcfQuality)));
    shadowObj.set("softShadows", JsonValue(settings.shadows.softShadows));
    root.set("shadows", std::move(shadowObj));

    // Post Processing
    JsonValue postObj = JsonValue::makeObject();
    postObj.set("enabled", JsonValue(settings.postProcess.enabled));
    postObj.set("toneMapOp", JsonValue(toneMapToString(settings.postProcess.toneMapOp)));
    postObj.set("exposure", JsonValue(static_cast<double>(settings.postProcess.exposure)));
    postObj.set("reinhardWhitePoint",
                JsonValue(static_cast<double>(settings.postProcess.reinhardWhitePoint)));
    postObj.set("bloomEnabled", JsonValue(settings.postProcess.bloomEnabled));
    postObj.set("bloomThreshold",
                JsonValue(static_cast<double>(settings.postProcess.bloomThreshold)));
    postObj.set("bloomKnee", JsonValue(static_cast<double>(settings.postProcess.bloomKnee)));
    postObj.set("bloomIntensity",
                JsonValue(static_cast<double>(settings.postProcess.bloomIntensity)));
    postObj.set("bloomFilterRadius",
                JsonValue(static_cast<double>(settings.postProcess.bloomFilterRadius)));
    root.set("postProcess", std::move(postObj));

    // Performance
    JsonValue perfObj = JsonValue::makeObject();
    perfObj.set("vsync", JsonValue(settings.performance.vsync));
    perfObj.set("frameRateLimit", JsonValue(static_cast<int>(settings.performance.frameRateLimit)));
    perfObj.set("dynamicResolution", JsonValue(settings.performance.dynamicResolution));
    perfObj.set("gpuProfiling", JsonValue(settings.performance.gpuProfiling));
    perfObj.set("cpuProfiling", JsonValue(settings.performance.cpuProfiling));
    perfObj.set("renderStats", JsonValue(settings.performance.renderStats));
    root.set("performance", std::move(perfObj));

    // Debug
    JsonValue debugObj = JsonValue::makeObject();
    debugObj.set("viewMode", JsonValue(debugViewToString(settings.debug.viewMode)));
    debugObj.set("visualizeShadowMap", JsonValue(settings.debug.visualizeShadowMap));
    debugObj.set("showPassTimings", JsonValue(settings.debug.showPassTimings));
    root.set("debug", std::move(debugObj));

    return Result<std::string>::success(root.serialize(prettyPrint ? indentSpaces : 0));
}

Result<void> GraphicsSettingsSerializer::serializeToFile(const GraphicsSettings& settings,
                                                         const std::filesystem::path& path,
                                                         bool prettyPrint, int indentSpaces) {
    auto strRes = serializeToString(settings, prettyPrint, indentSpaces);
    if (!strRes.hasValue()) {
        return strRes.error();
    }

    std::filesystem::path parentDir = path.parent_path();
    if (!parentDir.empty() && !std::filesystem::exists(parentDir)) {
        std::error_code ec;
        std::filesystem::create_directories(parentDir, ec);
        if (ec) {
            return Error{.code = ErrorCode::InvalidArgument,
                         .message = "Failed to create target directories for settings file"};
        }
    }

    std::filesystem::path tmpPath = path;
    tmpPath += ".tmp";

    {
        std::ofstream ofs(tmpPath, std::ios::out | std::ios::trunc);
        if (!ofs.is_open()) {
            return Error{.code = ErrorCode::DeviceCreationFailed,
                         .message = "Failed to open temporary settings file for writing"};
        }
        ofs << strRes.value();
        if (ofs.fail()) {
            return Error{.code = ErrorCode::DeviceCreationFailed,
                         .message = "Failed writing serialized settings stream"};
        }
    }

    std::error_code renameEc;
    std::filesystem::rename(tmpPath, path, renameEc);
    if (renameEc) {
        return Error{.code = ErrorCode::DeviceCreationFailed,
                     .message = "Atomic rename of graphics settings failed: " + renameEc.message()};
    }

    return Result<void>::success();
}

Result<GraphicsSettings>
GraphicsSettingsSerializer::deserializeFromString(std::string_view jsonText) {
    auto parseRes = JsonValue::parse(jsonText);
    if (!parseRes.hasValue()) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = std::string("JSON syntax error parsing graphics settings: ") +
                                std::string(parseRes.error().message)};
    }

    const auto& root = parseRes.value();
    if (!root.isObject()) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Root of graphics settings JSON must be an object"};
    }

    // Baseline default settings (High tier)
    GraphicsSettings settings = GraphicsSettings::createPreset(GraphicsQualityPreset::High);

    if (root.contains("formatVersion")) {
        settings.version = static_cast<uint32_t>(root.getInt("formatVersion", 1));
    }

    // 1. Quality
    if (const auto* q = root.find("quality"); q != nullptr && q->isObject()) {
        if (q->contains("preset")) {
            settings.quality.preset = stringToPreset(q->getString("preset", "High"));
        }
        settings.quality.renderScale = q->getFloat("renderScale", settings.quality.renderScale);
        if (q->contains("anisotropicFiltering")) {
            int aniso = q->getInt("anisotropicFiltering", 16);
            if (aniso == 0)
                settings.quality.anisotropicFiltering = AnisotropicFiltering::Off;
            else if (aniso <= 2)
                settings.quality.anisotropicFiltering = AnisotropicFiltering::X2;
            else if (aniso <= 4)
                settings.quality.anisotropicFiltering = AnisotropicFiltering::X4;
            else if (aniso <= 8)
                settings.quality.anisotropicFiltering = AnisotropicFiltering::X8;
            else
                settings.quality.anisotropicFiltering = AnisotropicFiltering::X16;
        }
        if (q->contains("antiAliasing")) {
            settings.quality.antiAliasing =
                stringToAntiAliasing(q->getString("antiAliasing", "None"));
        }
        if (q->contains("textureQuality")) {
            settings.quality.textureQuality =
                stringToTextureQuality(q->getString("textureQuality", "High"));
        }
    }

    // 2. Lighting
    if (const auto* l = root.find("lighting"); l != nullptr && l->isObject()) {
        settings.lighting.directLighting =
            l->getBool("directLighting", settings.lighting.directLighting);
        settings.lighting.iblEnabled = l->getBool("iblEnabled", settings.lighting.iblEnabled);
        settings.lighting.environmentIntensity =
            l->getFloat("environmentIntensity", settings.lighting.environmentIntensity);
        settings.lighting.indirectLighting =
            l->getBool("indirectLighting", settings.lighting.indirectLighting);
        settings.lighting.exposure = l->getFloat("exposure", settings.lighting.exposure);
        settings.lighting.whiteBalance =
            l->getFloat("whiteBalance", settings.lighting.whiteBalance);
    }

    // 3. Shadows
    if (const auto* s = root.find("shadows"); s != nullptr && s->isObject()) {
        settings.shadows.enabled = s->getBool("enabled", settings.shadows.enabled);
        settings.shadows.resolution = static_cast<uint32_t>(
            s->getInt("resolution", static_cast<int>(settings.shadows.resolution)));
        settings.shadows.distance = s->getFloat("distance", settings.shadows.distance);
        settings.shadows.depthBias = s->getFloat("depthBias", settings.shadows.depthBias);
        settings.shadows.slopeScaledBias =
            s->getFloat("slopeScaledBias", settings.shadows.slopeScaledBias);
        if (s->contains("pcfQuality")) {
            settings.shadows.pcfQuality = stringToPcf(s->getString("pcfQuality", "Medium"));
        }
        settings.shadows.softShadows = s->getBool("softShadows", settings.shadows.softShadows);
    }

    // 4. Post Processing
    if (const auto* pp = root.find("postProcess"); pp != nullptr && pp->isObject()) {
        settings.postProcess.enabled = pp->getBool("enabled", settings.postProcess.enabled);
        if (pp->contains("toneMapOp")) {
            settings.postProcess.toneMapOp =
                stringToToneMap(pp->getString("toneMapOp", "AcesFitted"));
        }
        settings.postProcess.exposure = pp->getFloat("exposure", settings.postProcess.exposure);
        settings.postProcess.reinhardWhitePoint =
            pp->getFloat("reinhardWhitePoint", settings.postProcess.reinhardWhitePoint);
        settings.postProcess.bloomEnabled =
            pp->getBool("bloomEnabled", settings.postProcess.bloomEnabled);
        settings.postProcess.bloomThreshold =
            pp->getFloat("bloomThreshold", settings.postProcess.bloomThreshold);
        settings.postProcess.bloomKnee = pp->getFloat("bloomKnee", settings.postProcess.bloomKnee);
        settings.postProcess.bloomIntensity =
            pp->getFloat("bloomIntensity", settings.postProcess.bloomIntensity);
        settings.postProcess.bloomFilterRadius =
            pp->getFloat("bloomFilterRadius", settings.postProcess.bloomFilterRadius);
    }

    // 5. Performance
    if (const auto* perf = root.find("performance"); perf != nullptr && perf->isObject()) {
        settings.performance.vsync = perf->getBool("vsync", settings.performance.vsync);
        settings.performance.frameRateLimit = static_cast<uint32_t>(
            perf->getInt("frameRateLimit", static_cast<int>(settings.performance.frameRateLimit)));
        settings.performance.dynamicResolution =
            perf->getBool("dynamicResolution", settings.performance.dynamicResolution);
        settings.performance.gpuProfiling =
            perf->getBool("gpuProfiling", settings.performance.gpuProfiling);
        settings.performance.cpuProfiling =
            perf->getBool("cpuProfiling", settings.performance.cpuProfiling);
        settings.performance.renderStats =
            perf->getBool("renderStats", settings.performance.renderStats);
    }

    // 6. Debug
    if (const auto* d = root.find("debug"); d != nullptr && d->isObject()) {
        if (d->contains("viewMode")) {
            settings.debug.viewMode = stringToDebugView(d->getString("viewMode", "Lit"));
        }
        settings.debug.visualizeShadowMap =
            d->getBool("visualizeShadowMap", settings.debug.visualizeShadowMap);
        settings.debug.showPassTimings =
            d->getBool("showPassTimings", settings.debug.showPassTimings);
    }

    // Update preset detector to reflect parsed parameters
    settings.quality.preset = settings.detectPreset();
    return Result<GraphicsSettings>::success(settings);
}

Result<GraphicsSettings>
GraphicsSettingsSerializer::deserializeFromFile(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Graphics settings file not found: " + path.string()};
    }

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return Error{.code = ErrorCode::DeviceCreationFailed,
                     .message = "Failed to open graphics settings file: " + path.string()};
    }

    std::stringstream ss;
    ss << ifs.rdbuf();
    return deserializeFromString(ss.str());
}

} // namespace eng::render
