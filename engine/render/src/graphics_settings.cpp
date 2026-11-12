/// @file graphics_settings.cpp
/// @brief Implementation of GraphicsSettings presets, detection, and hardware capability
/// validation.

#include "render/graphics_settings.h"

#include <algorithm>
#include <cmath>

namespace eng::render {

std::string_view GraphicsSettings::getPresetName(GraphicsQualityPreset preset) noexcept {
    switch (preset) {
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
    return "Unknown";
}

GraphicsSettings GraphicsSettings::createPreset(GraphicsQualityPreset preset) noexcept {
    GraphicsSettings s{};
    s.quality.preset = preset;

    switch (preset) {
    case GraphicsQualityPreset::Low:
        s.quality.renderScale = 0.75F;
        s.quality.anisotropicFiltering = AnisotropicFiltering::Off;
        s.quality.antiAliasing = AntiAliasingMode::None;
        s.quality.textureQuality = TextureQuality::Low;

        s.lighting.directLighting = true;
        s.lighting.environmentIntensity = 0.8F;
        s.lighting.exposure = 1.0F;

        s.shadows.enabled = false;
        s.shadows.resolution = 512;
        s.shadows.distance = 25.0F;
        s.shadows.depthBias = 0.008F;
        s.shadows.pcfQuality = ShadowPcfQuality::Hard;
        s.shadows.softShadows = false;

        s.postProcess.enabled = false; // SDR direct path for max performance
        s.postProcess.bloomEnabled = false;
        break;

    case GraphicsQualityPreset::Medium:
        s.quality.renderScale = 1.0F;
        s.quality.anisotropicFiltering = AnisotropicFiltering::X4;
        s.quality.antiAliasing = AntiAliasingMode::None;
        s.quality.textureQuality = TextureQuality::Medium;

        s.lighting.directLighting = true;
        s.lighting.environmentIntensity = 1.0F;
        s.lighting.exposure = 1.0F;

        s.shadows.enabled = true;
        s.shadows.resolution = 1024;
        s.shadows.distance = 35.0F;
        s.shadows.depthBias = 0.005F;
        s.shadows.pcfQuality = ShadowPcfQuality::Medium;
        s.shadows.softShadows = true;

        s.postProcess.enabled = true;
        s.postProcess.bloomEnabled = true;
        s.postProcess.bloomIntensity = 0.03F;
        s.postProcess.bloomThreshold = 1.0F;
        s.postProcess.toneMapOp = ToneMapOperator::AcesFitted;
        break;

    case GraphicsQualityPreset::High:
        s.quality.renderScale = 1.0F;
        s.quality.anisotropicFiltering = AnisotropicFiltering::X16;
        s.quality.antiAliasing = AntiAliasingMode::None;
        s.quality.textureQuality = TextureQuality::High;

        s.lighting.directLighting = true;
        s.lighting.environmentIntensity = 1.0F;
        s.lighting.exposure = 1.0F;

        s.shadows.enabled = true;
        s.shadows.resolution = 2048;
        s.shadows.distance = 50.0F;
        s.shadows.depthBias = 0.005F;
        s.shadows.pcfQuality = ShadowPcfQuality::Medium;
        s.shadows.softShadows = true;

        s.postProcess.enabled = true;
        s.postProcess.bloomEnabled = true;
        s.postProcess.bloomIntensity = 0.04F;
        s.postProcess.bloomThreshold = 1.0F;
        s.postProcess.toneMapOp = ToneMapOperator::AcesFitted;
        break;

    case GraphicsQualityPreset::Ultra:
        s.quality.renderScale = 1.0F;
        s.quality.anisotropicFiltering = AnisotropicFiltering::X16;
        s.quality.antiAliasing = AntiAliasingMode::None;
        s.quality.textureQuality = TextureQuality::Ultra;

        s.lighting.directLighting = true;
        s.lighting.environmentIntensity = 1.0F;
        s.lighting.exposure = 1.0F;

        s.shadows.enabled = true;
        s.shadows.resolution = 4096;
        s.shadows.distance = 75.0F;
        s.shadows.depthBias = 0.0025F;
        s.shadows.pcfQuality = ShadowPcfQuality::High;
        s.shadows.softShadows = true;

        s.postProcess.enabled = true;
        s.postProcess.bloomEnabled = true;
        s.postProcess.bloomIntensity = 0.04F;
        s.postProcess.bloomThreshold = 1.0F;
        s.postProcess.toneMapOp = ToneMapOperator::AcesFitted;
        break;

    case GraphicsQualityPreset::Cinematic:
        s.quality.renderScale = 1.25F; // 25% Supersampling
        s.quality.anisotropicFiltering = AnisotropicFiltering::X16;
        s.quality.antiAliasing = AntiAliasingMode::None;
        s.quality.textureQuality = TextureQuality::Ultra;

        s.lighting.directLighting = true;
        s.lighting.environmentIntensity = 1.0F;
        s.lighting.exposure = 1.0F;

        s.shadows.enabled = true;
        s.shadows.resolution = 4096;
        s.shadows.distance = 100.0F;
        s.shadows.depthBias = 0.002F;
        s.shadows.pcfQuality = ShadowPcfQuality::High;
        s.shadows.softShadows = true;

        s.postProcess.enabled = true;
        s.postProcess.bloomEnabled = true;
        s.postProcess.bloomIntensity = 0.05F;
        s.postProcess.bloomThreshold = 0.9F;
        s.postProcess.toneMapOp = ToneMapOperator::AcesFitted;
        break;

    case GraphicsQualityPreset::Custom:
        // Default to High as baseline when initialized as Custom
        s = createPreset(GraphicsQualityPreset::High);
        s.quality.preset = GraphicsQualityPreset::Custom;
        break;
    }

    return s;
}

GraphicsQualityPreset GraphicsSettings::detectPreset() const noexcept {
    static constexpr GraphicsQualityPreset kStandardPresets[] = {
        GraphicsQualityPreset::Low, GraphicsQualityPreset::Medium, GraphicsQualityPreset::High,
        GraphicsQualityPreset::Ultra, GraphicsQualityPreset::Cinematic};

    for (auto candidate : kStandardPresets) {
        GraphicsSettings ref = createPreset(candidate);

        const bool matchQuality =
            (std::abs(quality.renderScale - ref.quality.renderScale) < 0.01F) &&
            (quality.anisotropicFiltering == ref.quality.anisotropicFiltering) &&
            (quality.textureQuality == ref.quality.textureQuality);

        const bool matchShadows = (shadows.enabled == ref.shadows.enabled) &&
                                  (shadows.resolution == ref.shadows.resolution) &&
                                  (std::abs(shadows.distance - ref.shadows.distance) < 0.1F) &&
                                  (shadows.pcfQuality == ref.shadows.pcfQuality);

        const bool matchPost = (postProcess.enabled == ref.postProcess.enabled) &&
                               (postProcess.bloomEnabled == ref.postProcess.bloomEnabled);

        if (matchQuality && matchShadows && matchPost) {
            return candidate;
        }
    }

    return GraphicsQualityPreset::Custom;
}

void GraphicsSettings::applyPreset(GraphicsQualityPreset preset) noexcept {
    if (preset == GraphicsQualityPreset::Custom) {
        quality.preset = GraphicsQualityPreset::Custom;
        return;
    }

    // Preserve non-preset specific user preferences (vsync, debug modes, framerate limit)
    auto savedPerf = performance;
    auto savedDebug = debug;

    *this = createPreset(preset);
    performance = savedPerf;
    debug = savedDebug;
}

GraphicsSettings GraphicsSettings::validateAgainstCaps(const rhi::GpuCaps& caps) const noexcept {
    GraphicsSettings validated = *this;

    // 1. Anisotropic Filtering Clamping
    if (!caps.features.samplerAnisotropy) {
        validated.quality.anisotropicFiltering = AnisotropicFiltering::Off;
    } else {
        const float maxAniso = caps.limits.maxSamplerAnisotropy;
        const uint8_t reqAniso = static_cast<uint8_t>(validated.quality.anisotropicFiltering);
        if (static_cast<float>(reqAniso) > maxAniso) {
            if (maxAniso >= 8.0F) {
                validated.quality.anisotropicFiltering = AnisotropicFiltering::X8;
            } else if (maxAniso >= 4.0F) {
                validated.quality.anisotropicFiltering = AnisotropicFiltering::X4;
            } else if (maxAniso >= 2.0F) {
                validated.quality.anisotropicFiltering = AnisotropicFiltering::X2;
            } else {
                validated.quality.anisotropicFiltering = AnisotropicFiltering::Off;
            }
        }
    }

    // 2. Shadow Map Resolution Clamping
    const uint32_t maxTexDim = caps.limits.maxTextureDimension2D;
    if (validated.shadows.resolution > maxTexDim) {
        validated.shadows.resolution = maxTexDim;
    }

    // 3. Render Scale Boundary Guard
    validated.quality.renderScale = std::clamp(validated.quality.renderScale, 0.5F, 2.0F);

    // 4. GPU Timestamp Profiling Guard
    if (!caps.features.timestampQueries) {
        validated.performance.gpuProfiling = false;
        validated.debug.showPassTimings = false;
    }

    // 5. Wireframe Mode Guard
    if (validated.debug.viewMode == DebugViewMode::Wireframe && !caps.features.fillModeNonSolid) {
        validated.debug.viewMode = DebugViewMode::Lit;
    }

    // Update preset state after validation
    validated.quality.preset = validated.detectPreset();
    return validated;
}

SettingChangeSeverity
GraphicsSettings::computeChangeSeverity(const GraphicsSettings& other) const noexcept {
    if (*this == other) {
        return SettingChangeSeverity::None;
    }

    // Swapchain recreation required if VSync changed
    if (performance.vsync != other.performance.vsync) {
        return SettingChangeSeverity::SwapchainRecreation;
    }

    // Pipeline rebuild required if debug rasterization mode changed
    if (debug.viewMode != other.debug.viewMode) {
        return SettingChangeSeverity::PipelineRecompilation;
    }

    // Resource allocation required if shadow resolution, render scale, or HDR target changed
    if (shadows.resolution != other.shadows.resolution ||
        std::abs(quality.renderScale - other.quality.renderScale) > 1e-4F ||
        postProcess.enabled != other.postProcess.enabled) {
        return SettingChangeSeverity::RenderResourceRecreation;
    }

    // All remaining parameters update immediately in dynamic state / uniform buffers
    return SettingChangeSeverity::Immediate;
}

bool GraphicsSettings::operator==(const GraphicsSettings& other) const noexcept {
    return quality == other.quality && lighting == other.lighting && shadows == other.shadows &&
           postProcess.enabled == other.postProcess.enabled &&
           postProcess.exposure == other.postProcess.exposure &&
           postProcess.toneMapOp == other.postProcess.toneMapOp &&
           postProcess.bloomEnabled == other.postProcess.bloomEnabled &&
           postProcess.bloomThreshold == other.postProcess.bloomThreshold &&
           postProcess.bloomKnee == other.postProcess.bloomKnee &&
           postProcess.reinhardWhitePoint == other.postProcess.reinhardWhitePoint &&
           postProcess.bloomIntensity == other.postProcess.bloomIntensity &&
           postProcess.bloomFilterRadius == other.postProcess.bloomFilterRadius &&
           performance == other.performance && debug == other.debug;
}

} // namespace eng::render
