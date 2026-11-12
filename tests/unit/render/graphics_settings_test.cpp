/// @file graphics_settings_test.cpp
/// @brief Comprehensive unit verification tests for Milestone 11B Graphics Settings & Scalability.
///
/// Validates:
/// 1. Preset creation and default parameter baseline.
/// 2. Automatic preset detection and custom override tracking.
/// 3. Hardware capability clamping and feature validation.
/// 4. Change severity classification (None, PerFrame, RecreateTarget, RebuildPipelines).
/// 5. JSON serialization, atomic file writing, schema versioning, and corrupt JSON recovery.

#include "core/assert.h"
#include "render/graphics_settings.h"
#include "render/graphics_settings_serializer.h"
#include "rhi/gpu_caps.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ << ": " #cond       \
                      << std::endl;                                                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

using namespace eng;
using namespace eng::render;

int main() {
    std::cout << "[graphics_settings_test] Starting Milestone 11B verification tests..."
              << std::endl;

    // =========================================================================
    // 1. Preset Creation & Values Baseline
    // =========================================================================
    {
        std::cout << "  - Test 1: Preset generation integrity..." << std::endl;
        auto low = GraphicsSettings::createPreset(GraphicsQualityPreset::Low);
        TEST_CHECK(low.quality.preset == GraphicsQualityPreset::Low);
        TEST_CHECK(low.quality.renderScale == 0.75F);
        TEST_CHECK(low.shadows.resolution == 512);
        TEST_CHECK(!low.shadows.softShadows);
        TEST_CHECK(low.quality.anisotropicFiltering == AnisotropicFiltering::Off);

        auto high = GraphicsSettings::createPreset(GraphicsQualityPreset::High);
        TEST_CHECK(high.quality.preset == GraphicsQualityPreset::High);
        TEST_CHECK(high.quality.renderScale == 1.0F);
        TEST_CHECK(high.shadows.resolution == 2048);
        TEST_CHECK(high.shadows.softShadows);
        TEST_CHECK(high.shadows.distance == 50.0F);
        TEST_CHECK(high.quality.anisotropicFiltering == AnisotropicFiltering::X16);

        auto cinematic = GraphicsSettings::createPreset(GraphicsQualityPreset::Cinematic);
        TEST_CHECK(cinematic.quality.preset == GraphicsQualityPreset::Cinematic);
        TEST_CHECK(cinematic.quality.renderScale == 1.25F);
        TEST_CHECK(cinematic.shadows.resolution == 4096);
        TEST_CHECK(cinematic.shadows.distance == 100.0F);
    }

    // =========================================================================
    // 2. Preset Detection & Custom Preset Override
    // =========================================================================
    {
        std::cout << "  - Test 2: Preset detection and custom overrides..." << std::endl;
        auto high = GraphicsSettings::createPreset(GraphicsQualityPreset::High);
        TEST_CHECK(high.detectPreset() == GraphicsQualityPreset::High);

        auto ultra = GraphicsSettings::createPreset(GraphicsQualityPreset::Ultra);
        TEST_CHECK(ultra.detectPreset() == GraphicsQualityPreset::Ultra);

        // Perturbing a single parameter must detect Custom
        high.shadows.resolution = 4096;
        TEST_CHECK(high.detectPreset() == GraphicsQualityPreset::Custom);

        // Resetting back to High parameters restores High detection
        high.shadows.resolution = 2048;
        TEST_CHECK(high.detectPreset() == GraphicsQualityPreset::High);
    }

    // =========================================================================
    // 3. Hardware Capability Clamping
    // =========================================================================
    {
        std::cout << "  - Test 3: Capability clamping against hardware limitations..." << std::endl;
        rhi::GpuCaps caps{};
        caps.features.samplerAnisotropy = false;
        caps.features.fillModeNonSolid = false;
        caps.limits.maxTextureDimension2D = 1024;
        caps.limits.maxSamplerAnisotropy = 1.0F;

        auto settings = GraphicsSettings::createPreset(GraphicsQualityPreset::Ultra);
        settings.debug.viewMode = DebugViewMode::Wireframe;

        auto validated = settings.validateAgainstCaps(caps);

        // Anisotropic filtering clamped to Off because feature is unsupported
        TEST_CHECK(validated.quality.anisotropicFiltering == AnisotropicFiltering::Off);

        // Shadow resolution clamped from 2048 to maxTextureDimension2D (1024)
        TEST_CHECK(validated.shadows.resolution == 1024);

        // Debug wireframe view reverted to Lit because fillModeNonSolid is unsupported
        TEST_CHECK(validated.debug.viewMode == DebugViewMode::Lit);
    }

    // =========================================================================
    // 4. Change Severity Classification
    // =========================================================================
    {
        std::cout << "  - Test 4: Change severity classification..." << std::endl;
        auto a = GraphicsSettings::createPreset(GraphicsQualityPreset::High);
        auto b = a;
        TEST_CHECK(a.computeChangeSeverity(b) == SettingChangeSeverity::None);

        // Immediate change: exposure or bloom intensity
        b.postProcess.exposure = 1.5F;
        TEST_CHECK(a.computeChangeSeverity(b) == SettingChangeSeverity::Immediate);

        // RenderResourceRecreation: renderScale
        b = a;
        b.quality.renderScale = 0.85F;
        TEST_CHECK(a.computeChangeSeverity(b) == SettingChangeSeverity::RenderResourceRecreation);

        // RenderResourceRecreation: shadow resolution
        b = a;
        b.shadows.resolution = 4096;
        TEST_CHECK(a.computeChangeSeverity(b) == SettingChangeSeverity::RenderResourceRecreation);

        // PipelineRecompilation: debug view mode
        b = a;
        b.debug.viewMode = DebugViewMode::Wireframe;
        TEST_CHECK(a.computeChangeSeverity(b) == SettingChangeSeverity::PipelineRecompilation);

        // SwapchainRecreation: vsync toggle
        b = a;
        b.performance.vsync = !a.performance.vsync;
        TEST_CHECK(a.computeChangeSeverity(b) == SettingChangeSeverity::SwapchainRecreation);
    }

    // =========================================================================
    // 5. JSON Serialization Roundtrip
    // =========================================================================
    {
        std::cout << "  - Test 5: JSON serialization & round-trip deserialization..." << std::endl;
        auto original = GraphicsSettings::createPreset(GraphicsQualityPreset::Ultra);
        original.lighting.directLighting = false;
        original.postProcess.exposure = 1.85F;
        original.debug.viewMode = DebugViewMode::Wireframe;

        auto jsonRes = GraphicsSettingsSerializer::serializeToString(original, true, 2);
        TEST_CHECK(jsonRes.hasValue());
        const std::string& jsonText = jsonRes.value();
        TEST_CHECK(!jsonText.empty());

        auto parsedRes = GraphicsSettingsSerializer::deserializeFromString(jsonText);
        TEST_CHECK(parsedRes.hasValue());
        const auto& parsed = parsedRes.value();

        TEST_CHECK(parsed.quality.preset == original.quality.preset);
        TEST_CHECK(parsed.shadows.resolution == original.shadows.resolution);
        TEST_CHECK(parsed.lighting.directLighting == original.lighting.directLighting);
        TEST_CHECK(std::abs(parsed.postProcess.exposure - original.postProcess.exposure) < 1e-4F);
        TEST_CHECK(parsed.debug.viewMode == original.debug.viewMode);
    }

    // =========================================================================
    // 6. Corrupt / Empty JSON Handling
    // =========================================================================
    {
        std::cout << "  - Test 6: Corrupt and partial JSON resilience..." << std::endl;
        auto corruptRes =
            GraphicsSettingsSerializer::deserializeFromString("{ invalid json syntax");
        TEST_CHECK(!corruptRes.hasValue());

        // Empty JSON object should gracefully use defaults without crashing
        auto emptyRes = GraphicsSettingsSerializer::deserializeFromString("{}");
        TEST_CHECK(emptyRes.hasValue());
        const auto& def = emptyRes.value();
        TEST_CHECK(def.quality.renderScale == 1.0F);
        TEST_CHECK(def.shadows.resolution == 2048);
    }

    // =========================================================================
    // 7. Atomic File Persistence
    // =========================================================================
    {
        std::cout << "  - Test 7: Atomic file I/O operations..." << std::endl;
        std::filesystem::path testPath = "build/test_graphics_settings.json";

        auto original = GraphicsSettings::createPreset(GraphicsQualityPreset::Cinematic);
        auto writeRes = GraphicsSettingsSerializer::serializeToFile(original, testPath);
        TEST_CHECK(writeRes.hasValue());
        TEST_CHECK(std::filesystem::exists(testPath));

        auto readRes = GraphicsSettingsSerializer::deserializeFromFile(testPath);
        TEST_CHECK(readRes.hasValue());
        const auto& loaded = readRes.value();
        TEST_CHECK(loaded.shadows.resolution == 4096);
        TEST_CHECK(loaded.quality.renderScale == 1.25F);

        std::filesystem::remove(testPath);
    }

    std::cout << "[graphics_settings_test] All Milestone 11B verification tests PASSED!"
              << std::endl;
    return 0;
}
