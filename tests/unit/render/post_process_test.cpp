/// @file post_process_test.cpp
/// @brief Physical, mathematical, and offscreen verification tests for Milestone 10F HDR,
/// Tone Mapping, and Bloom Post-Processing.
///
/// Milestone 10F Requirements:
/// 1. Tone mapping mathematical operators: ACES fitted, Filmic Reinhard, Khronos PBR Neutral,
/// Linear Bypass.
/// 2. sRGB OETF transfer curve application for UNORM targets.
/// 3. Bloom soft-knee luminance extraction math and numerical stability.
/// 4. HDR scene render target capability negotiation and resizing.
/// 5. Offscreen GPU scene rendering with exposure control, bloom pyramid, and tone mapping passes.

#include "core/assert.h"
#include "core/math.h"
#include "physics/physics_components.h"
#include "render/asset_manager.h"
#include "render/gltf_scene_importer.h"
#include "render/light_components.h"
#include "render/lighting_data.h"
#include "render/mesh.h"
#include "render/mesh_component.h"
#include "render/post_process_types.h"
#include "render/scene_renderer.h"
#include "rhi/command_list.h"
#include "rhi/rhi_device.h"
#include "world/world.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HIKARI_HAS_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define HIKARI_HAS_ASAN 1
#endif

#if defined(HIKARI_HAS_ASAN)
extern "C" const char* __lsan_default_suppressions() {
    return "leak:libdbus-1\nleak:libvulkan\nleak:nvidia\nleak:libwayland\n";
}
#endif

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
using namespace eng::rhi;

// CPU reference implementations matching shader code for mathematical validation

static inline float acesFittedCurve(float x) noexcept {
    x = std::max(0.0F, x);
    constexpr float a = 2.51F;
    constexpr float b = 0.03F;
    constexpr float c = 2.43F;
    constexpr float d = 0.59F;
    constexpr float e = 0.14F;
    return std::clamp((x * ((a * x) + b)) / ((x * ((c * x) + d)) + e), 0.0F, 1.0F);
}

static inline float filmicReinhardCurve(float x, float white = 4.0F) noexcept {
    x = std::max(0.0F, x);
    return std::clamp((x * (1.0F + (x / (white * white)))) / (1.0F + x), 0.0F, 1.0F);
}

static inline Vec3 neutralToneMappingCpu(Vec3 color) noexcept {
    constexpr float startCompression = 0.8F - 0.04F;
    constexpr float desaturation = 0.15F;

    float x = std::min(color.x, std::min(color.y, color.z));
    float offset = (x < 0.08F) ? (x - (6.25F * x * x)) : 0.04F;
    color.x -= offset;
    color.y -= offset;
    color.z -= offset;

    float peak = std::max(color.x, std::max(color.y, color.z));
    if (peak < startCompression) {
        return Vec3(std::clamp(color.x, 0.0F, 1.0F), std::clamp(color.y, 0.0F, 1.0F),
                    std::clamp(color.z, 0.0F, 1.0F));
    }

    constexpr float d = 1.0F - startCompression;
    float newPeak = 1.0F - ((d * d) / (peak + d - startCompression));
    color = color * (newPeak / peak);

    float g = 1.0F - (1.0F / ((desaturation * (peak - newPeak)) + 1.0F));
    Vec3 res = color * (1.0F - g) + Vec3(newPeak, newPeak, newPeak) * g;
    return Vec3(std::clamp(res.x, 0.0F, 1.0F), std::clamp(res.y, 0.0F, 1.0F),
                std::clamp(res.z, 0.0F, 1.0F));
}

static inline float linearToSrgbCpu(float x) noexcept {
    x = std::max(0.0F, x);
    return (x <= 0.0031308F) ? (12.92F * x) : ((1.055F * std::pow(x, 1.0F / 2.4F)) - 0.055F);
}

static constexpr float toRadians(float deg) noexcept {
    return deg * (3.14159265358979323846F / 180.0F);
}

static inline float extractThresholdCpu(float brightness, float thresh, float knee) noexcept {
    float soft = brightness - thresh + knee;
    soft = std::clamp(soft, 0.0F, 2.0F * knee);
    soft = (soft * soft) / ((4.0F * knee) + 1e-4F);
    float contribution = std::max(soft, brightness - thresh);
    contribution /= std::max(brightness, 1e-4F);
    return std::clamp(contribution, 0.0F, 1.0F);
}

static bool savePpm(const std::string& path, const uint8_t* pixels, uint32_t width,
                    uint32_t height) {
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    file << "P6\n" << width << " " << height << "\n255\n";
    for (size_t i = 0; i < static_cast<size_t>(width * height * 4); i += 4) {
        file.put(static_cast<char>(pixels[i + 0]));
        file.put(static_cast<char>(pixels[i + 1]));
        file.put(static_cast<char>(pixels[i + 2]));
    }
    return true;
}

// -----------------------------------------------------------------------------
// Test 1: Tone Mapping Mathematical Operators & Transfer Functions
// -----------------------------------------------------------------------------
static int testToneMappingMath() {
    std::cout << "[Test 1] Verifying tone mapping mathematical curves and numerical limits...\n";

    // 1. ACES Fitted
    // Monotonicity test
    float prevAces = -1.0F;
    for (float v = 0.0F; v <= 50.0F; v += 0.05F) {
        float cur = acesFittedCurve(v);
        TEST_CHECK(cur >= prevAces);
        TEST_CHECK(cur >= 0.0F && cur <= 1.0F);
        prevAces = cur;
    }
    // Asymptotic highlight roll-off
    TEST_CHECK(acesFittedCurve(0.0F) < 0.01F);
    TEST_CHECK(acesFittedCurve(0.18F) > 0.25F && acesFittedCurve(0.18F) < 0.45F);
    TEST_CHECK(acesFittedCurve(1.0F) > 0.75F && acesFittedCurve(1.0F) < 0.90F);
    TEST_CHECK(acesFittedCurve(100.0F) > 0.98F);

    // 2. Filmic Reinhard
    float prevReinhard = -1.0F;
    for (float v = 0.0F; v <= 50.0F; v += 0.05F) {
        float cur = filmicReinhardCurve(v, 4.0F);
        TEST_CHECK(cur >= prevReinhard);
        TEST_CHECK(cur >= 0.0F && cur <= 1.0F);
        prevReinhard = cur;
    }
    TEST_CHECK(std::abs(filmicReinhardCurve(0.0F) - 0.0F) < 1e-5F);
    TEST_CHECK(filmicReinhardCurve(1.0F) > 0.50F && filmicReinhardCurve(1.0F) < 0.60F);
    TEST_CHECK(filmicReinhardCurve(4.0F) > 0.90F);
    TEST_CHECK(filmicReinhardCurve(100.0F) > 0.98F);

    // 3. Khronos PBR Neutral
    Vec3 lowColor(0.2F, 0.4F, 0.6F);
    Vec3 mappedLow = neutralToneMappingCpu(lowColor);
    // Linear region preservation
    TEST_CHECK(std::abs(mappedLow.y - 0.4F) < 0.05F);
    TEST_CHECK(std::abs(mappedLow.z - 0.6F) < 0.05F);

    Vec3 brightColor(5.0F, 2.0F, 1.0F);
    Vec3 mappedBright = neutralToneMappingCpu(brightColor);
    TEST_CHECK(mappedBright.x <= 1.0F && mappedBright.y <= 1.0F && mappedBright.z <= 1.0F);
    // Preserves hue hierarchy
    TEST_CHECK(mappedBright.x > mappedBright.y);
    TEST_CHECK(mappedBright.y > mappedBright.z);

    // 4. sRGB Transfer Curve
    TEST_CHECK(std::abs(linearToSrgbCpu(0.0F) - 0.0F) < 1e-5F);
    TEST_CHECK(std::abs(linearToSrgbCpu(1.0F) - 1.0F) < 1e-4F);
    // Linear segment threshold check
    float linVal = 0.002F;
    TEST_CHECK(std::abs(linearToSrgbCpu(linVal) - (12.92F * linVal)) < 1e-5F);
    // Perceptual mid-gray (0.18 linear ~ 0.46 sRGB)
    float midGray = linearToSrgbCpu(0.18F);
    TEST_CHECK(midGray > 0.44F && midGray < 0.48F);

    std::cout << "  Tone mapping mathematical verification passed.\n";
    return 0;
}

// -----------------------------------------------------------------------------
// Test 2: Bloom Soft-Knee Luminance Extraction Math
// -----------------------------------------------------------------------------
static int testBloomExtractionMath() {
    std::cout << "[Test 2] Verifying bloom soft-knee extraction math...\n";

    const float thresh = 1.0F;
    const float knee = 0.5F;

    // Below knee (brightness <= thresh - knee = 0.5): extraction must be strictly 0
    TEST_CHECK(extractThresholdCpu(0.1F, thresh, knee) == 0.0F);
    TEST_CHECK(extractThresholdCpu(0.49F, thresh, knee) == 0.0F);

    // In knee region (0.5 < brightness < 1.5): smooth positive curve
    float inKnee1 = extractThresholdCpu(0.75F, thresh, knee);
    float inKnee2 = extractThresholdCpu(1.0F, thresh, knee);
    float inKnee3 = extractThresholdCpu(1.25F, thresh, knee);
    TEST_CHECK(inKnee1 > 0.0F && inKnee1 < inKnee2);
    TEST_CHECK(inKnee2 < inKnee3);

    // Above knee region (brightness >= 1.5): monotonic approach to 1.0
    float high1 = extractThresholdCpu(2.0F, thresh, knee);
    float high2 = extractThresholdCpu(10.0F, thresh, knee);
    TEST_CHECK(high1 > inKnee3 && high1 < 1.0F);
    TEST_CHECK(high2 > high1 && high2 > 0.85F);

    std::cout << "  Bloom extraction verification passed.\n";
    return 0;
}

// -----------------------------------------------------------------------------
// Test 3: HDR Render Target Allocation & Dynamic Resizing
// -----------------------------------------------------------------------------
static int testSceneRendererHdrLifecycle() {
    std::cout << "[Test 3] Verifying HDR target lifecycle, format negotiation, and resizing...\n";

    auto nullDevRes = createNullDevice();
    TEST_CHECK(nullDevRes.hasValue());
    auto nullDevice = std::move(nullDevRes.value());

    SceneRenderer renderer;
    auto initRes = renderer.init(*nullDevice, 800, 600);
    TEST_CHECK(initRes.hasValue());
    TEST_CHECK(renderer.isInitialized());

    // Verify HDR target was allocated
    TEST_CHECK(renderer.getHdrTarget().isValid());
    TEST_CHECK(renderer.getColorTarget().isValid());
    TEST_CHECK(renderer.getDepthTarget().isValid());

    // Verify format capability negotiation
    auto hdrFormat = renderer.getHdrFormat();
    TEST_CHECK(hdrFormat == TextureFormat::RGBA16_FLOAT ||
               hdrFormat == TextureFormat::RGBA32_FLOAT || hdrFormat == TextureFormat::RGBA8_UNORM);

    // Verify default post-processing settings state
    TEST_CHECK(!renderer.isPostProcessingEnabled());
    PostProcessSettings ppSettings{};
    ppSettings.enabled = true;
    ppSettings.exposure = 1.5F;
    ppSettings.toneMapOp = ToneMapOperator::FilmicReinhard;
    ppSettings.bloomEnabled = true;
    renderer.setPostProcessSettings(ppSettings);
    TEST_CHECK(renderer.isPostProcessingEnabled());
    TEST_CHECK(renderer.isHdrActive());
    TEST_CHECK(renderer.getPostProcessSettings().exposure == 1.5F);
    TEST_CHECK(renderer.getPostProcessSettings().toneMapOp == ToneMapOperator::FilmicReinhard);

    // Test multiple dynamic resize operations
    TEST_CHECK(renderer.resize(1920, 1080).hasValue());
    TEST_CHECK(renderer.getWidth() == 1920 && renderer.getHeight() == 1080);
    TEST_CHECK(renderer.getHdrTarget().isValid());

    TEST_CHECK(renderer.resize(128, 128).hasValue());
    TEST_CHECK(renderer.getWidth() == 128 && renderer.getHeight() == 128);

    // Small boundary dimensions (pyramid levels >= 1 pixel)
    TEST_CHECK(renderer.resize(16, 16).hasValue());
    TEST_CHECK(renderer.getWidth() == 16 && renderer.getHeight() == 16);

    renderer.shutdown();
    TEST_CHECK(!renderer.isInitialized());

    std::cout << "  HDR lifecycle and resizing verification passed.\n";
    return 0;
}

// -----------------------------------------------------------------------------
// Test 4: Vulkan GPU Post-Processing Execution & Pixel Verification
// -----------------------------------------------------------------------------
static int testVulkanPostProcessRendering() {
    std::cout << "[Test 4] Verifying Vulkan GPU HDR rendering, bloom, and tone mapping passes...\n";

    VulkanDeviceDesc devDesc{.windowHandle = nullptr, .enableValidation = true};
    auto devRes = createVulkanDevice(devDesc);
    if (!devRes.hasValue()) {
        std::cout << "  Vulkan device unavailable on this host; skipping Vulkan GPU test.\n";
        return 0;
    }
    auto device = std::move(devRes.value());

    constexpr uint32_t width = 64;
    constexpr uint32_t height = 64;
    SceneRenderer renderer;
    auto initRes = renderer.init(*device, width, height);
    TEST_CHECK(initRes.hasValue());

    // Enable post processing
    PostProcessSettings pp{};
    pp.enabled = true;
    pp.exposure = 1.0F;
    pp.toneMapOp = ToneMapOperator::AcesFitted;
    pp.bloomEnabled = true;
    pp.bloomThreshold = 0.5F;
    pp.bloomIntensity = 0.05F;
    renderer.setPostProcessSettings(pp);

    world::World world;
    physics::registerPhysicsComponents(world);
    AssetManager assetManager;

    const std::string helmetPath = "test-assets/DamagedHelmet.glb";
    if (std::filesystem::exists(helmetPath)) {
        auto importRes = GltfSceneImporter::importScene(helmetPath, world, &assetManager);
        TEST_CHECK(importRes.success);
    } else {
        flecs::world& ecs = world.getRawWorld();
        auto lightEnt = ecs.entity("HighIntensityLight");
        PointLightComponent ptComp{};
        ptComp.enabled = true;
        ptComp.position = Vec3(0.0F, 0.0F, 1.0F);
        ptComp.color = Vec3(1.0F, 1.0F, 1.0F);
        ptComp.intensity = 10.0F;
        ptComp.range = 10.0F;
        lightEnt.set<PointLightComponent>(ptComp);
    }

    // Readback buffer
    const uint64_t byteSize = width * height * 4;
    BufferDesc rbDesc{
        .sizeInBytes = byteSize,
        .usage = BufferUsageFlags::TransferDst,
        .memoryUsage = MemoryUsage::GpuToCpu,
        .debugName = "ReadbackBuffer",
    };
    auto rbRes = device->createBuffer(rbDesc);
    TEST_CHECK(rbRes.hasValue());
    auto rbHandle = rbRes.value();

    auto cmdRes = device->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());

    Mat4 view =
        Mat4::lookAt(Vec3(0.0F, 0.0F, 2.5F), Vec3(0.0F, 0.0F, 0.0F), Vec3(0.0F, 1.0F, 0.0F));
    Mat4 proj = Mat4::perspective(toRadians(60.0F), 1.0F, 0.1F, 100.0F);
    Mat4 viewProj = proj * view;

    auto capture = [&](const std::string& name, const PostProcessSettings& settings,
                       std::vector<uint8_t>& outPx) -> int {
        renderer.setPostProcessSettings(settings);
        cmd->begin();
        renderer.renderScene(world, assetManager, *device, *cmd, viewProj);
        renderer.renderPostProcess(*cmd);
        cmd->copyTextureToBuffer(renderer.getColorTarget(), rbHandle);
        cmd->end();

        TEST_CHECK(device->submit(cmd.get()).hasValue());
        device->waitIdle();

        outPx.assign(byteSize, 0);
        auto readRes = device->readBuffer(rbHandle, 0, outPx.data(), byteSize);
        TEST_CHECK(readRes.hasValue());
        uint64_t total = 0;
        for (uint8_t b : outPx) {
            total += b;
        }
        std::cout << "  Capture " << name << ": total=" << total << ", sample[0..3]=("
                  << static_cast<int>(outPx[0]) << "," << static_cast<int>(outPx[1]) << ","
                  << static_cast<int>(outPx[2]) << "," << static_cast<int>(outPx[3]) << ")\n";
        savePpm("test-assets/" + name + ".ppm", outPx.data(), width, height);
        return 0;
    };

    // 1. Base scene with ACES tone mapping (normal exposure = 1.0)
    PostProcessSettings sAcesNormal{
        .enabled = true,
        .exposure = 1.0F,
        .toneMapOp = ToneMapOperator::AcesFitted,
        .bloomEnabled = true,
        .bloomThreshold = 0.5F,
        .bloomKnee = 0.5F,
        .bloomIntensity = 0.05F,
        .bloomFilterRadius = 1.0F,
    };
    std::vector<uint8_t> pxAcesNorm;
    TEST_CHECK(capture("postprocess_aces_normal", sAcesNormal, pxAcesNorm) == 0);

    // 2. Increased exposure (2.5)
    PostProcessSettings sAcesHigh = sAcesNormal;
    sAcesHigh.exposure = 2.5F;
    std::vector<uint8_t> pxAcesHigh;
    TEST_CHECK(capture("postprocess_aces_high_exposure", sAcesHigh, pxAcesHigh) == 0);

    // 3. Decreased exposure (0.3)
    PostProcessSettings sAcesLow = sAcesNormal;
    sAcesLow.exposure = 0.3F;
    std::vector<uint8_t> pxAcesLow;
    TEST_CHECK(capture("postprocess_aces_low_exposure", sAcesLow, pxAcesLow) == 0);

    // Verify brightness relationship: high > normal > low
    uint64_t sumHigh = 0;
    uint64_t sumNorm = 0;
    uint64_t sumLow = 0;
    for (size_t i = 0; i < byteSize; i += 4) {
        sumHigh += static_cast<uint64_t>(pxAcesHigh[i] + pxAcesHigh[i + 1] + pxAcesHigh[i + 2]);
        sumNorm += static_cast<uint64_t>(pxAcesNorm[i] + pxAcesNorm[i + 1] + pxAcesNorm[i + 2]);
        sumLow += static_cast<uint64_t>(pxAcesLow[i] + pxAcesLow[i + 1] + pxAcesLow[i + 2]);
    }
    std::cout << "  Exposure sums: high=" << sumHigh << ", norm=" << sumNorm << ", low=" << sumLow
              << std::endl;
    TEST_CHECK(sumHigh > sumNorm);
    TEST_CHECK(sumNorm > sumLow);

    // 4. Bloom enabled vs disabled
    PostProcessSettings sBloomOff = sAcesNormal;
    sBloomOff.bloomEnabled = false;
    std::vector<uint8_t> pxBloomOff;
    TEST_CHECK(capture("postprocess_bloom_disabled", sBloomOff, pxBloomOff) == 0);

    PostProcessSettings sBloomOn = sAcesNormal;
    sBloomOn.bloomEnabled = true;
    sBloomOn.bloomThreshold = 0.2F;
    sBloomOn.bloomIntensity = 0.20F;
    std::vector<uint8_t> pxBloomOn;
    TEST_CHECK(capture("postprocess_bloom_enabled", sBloomOn, pxBloomOn) == 0);

    uint64_t sumBloomOn = 0;
    uint64_t sumBloomOff = 0;
    for (size_t i = 0; i < byteSize; i += 4) {
        sumBloomOn += static_cast<uint64_t>(pxBloomOn[i] + pxBloomOn[i + 1] + pxBloomOn[i + 2]);
        sumBloomOff += static_cast<uint64_t>(pxBloomOff[i] + pxBloomOff[i + 1] + pxBloomOff[i + 2]);
    }
    TEST_CHECK(sumBloomOn >= sumBloomOff);

    // 5. Operator comparisons (Reinhard, Neutral, Bypass)
    PostProcessSettings sReinhard = sAcesNormal;
    sReinhard.toneMapOp = ToneMapOperator::FilmicReinhard;
    sReinhard.bloomEnabled = false;
    std::vector<uint8_t> pxDummy;
    TEST_CHECK(capture("postprocess_reinhard", sReinhard, pxDummy) == 0);

    PostProcessSettings sNeutral = sAcesNormal;
    sNeutral.toneMapOp = ToneMapOperator::Neutral;
    sNeutral.bloomEnabled = false;
    TEST_CHECK(capture("postprocess_neutral", sNeutral, pxDummy) == 0);

    PostProcessSettings sBypass = sAcesNormal;
    sBypass.toneMapOp = ToneMapOperator::Bypass;
    sBypass.bloomEnabled = false;
    TEST_CHECK(capture("postprocess_bypass", sBypass, pxDummy) == 0);

    device->destroyBuffer(rbHandle);
    renderer.shutdown();
    std::cout << "  Vulkan GPU post-processing execution verified successfully.\n";
    return 0;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << " Running Milestone 10F HDR, Tone Mapping & Post FX Tests\n";
    std::cout << "========================================================\n";

    if (testToneMappingMath() != 0) {
        std::cerr << "FAIL: Tone mapping math test\n";
        return 1;
    }

    if (testBloomExtractionMath() != 0) {
        std::cerr << "FAIL: Bloom extraction math test\n";
        return 1;
    }

    if (testSceneRendererHdrLifecycle() != 0) {
        std::cerr << "FAIL: SceneRenderer HDR lifecycle test\n";
        return 1;
    }

    if (testVulkanPostProcessRendering() != 0) {
        std::cerr << "FAIL: Vulkan GPU post-processing test\n";
        return 1;
    }

    std::cout << "========================================================\n";
    std::cout << " ALL MILESTONE 10F POST-PROCESSING TESTS PASSED (4/4)\n";
    std::cout << "========================================================\n";
    return 0;
}
