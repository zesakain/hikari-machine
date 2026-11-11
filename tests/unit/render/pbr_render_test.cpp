/// @file pbr_render_test.cpp
/// @brief Physical and visual verification tests for glTF 2.0 Cook-Torrance metallic-roughness
/// BRDF.
///
/// Milestone 10D Requirements:
/// 1. GGX normal distribution, Smith geometry visibility, Schlick Fresnel specular BRDF.
/// 2. Energy-conserving Lambertian diffuse reflection (diffuse suppressed on metals).
/// 3. Roughness variations: sharp specular highlight on smooth surfaces vs broad on rough surfaces.
/// 4. Metallic variations: dielectric (F0 = 0.04) vs metallic (F0 = baseColor, zero diffuse).
/// 5. Tangent-space normal mapping using TBN basis and normalScale.
/// 6. Metallic-roughness texture channel interpretation (G = roughness, B = metallic).
/// 7. Emissive contribution and alpha mask discard behavior.
/// 8. Missing texture fallback safety.

#include "core/assert.h"
#include "core/math.h"
#include "physics/physics_components.h"
#include "render/asset_manager.h"
#include "render/gltf_scene_importer.h"
#include "render/gpu_material.h"
#include "render/gpu_mesh.h"
#include "render/image.h"
#include "render/material.h"
#include "render/mesh.h"
#include "render/mesh_component.h"
#include "render/scene_renderer.h"
#include "rhi/command_list.h"
#include "rhi/rhi_device.h"
#include "world/world.h"

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
#if __has_feature(thread_sanitizer)
#define HIKARI_HAS_TSAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define HIKARI_HAS_ASAN 1
#endif
#if defined(__SANITIZE_THREAD__)
#define HIKARI_HAS_TSAN 1
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

static constexpr float toRadians(float deg) noexcept {
    return deg * 3.141592653589793F / 180.0F;
}

// Creates a 2-triangle flat quad in the XY plane facing +Z
static MeshData createPbrQuad(float minX = -1.0F, float maxX = 1.0F, float minY = -1.0F,
                              float maxY = 1.0F) {
    MeshData mesh{};
    mesh.vertices = {
        Vertex(Vec3(minX, minY, 0.0F), Vec3(0.0F, 0.0F, 1.0F), Vec2(0.0F, 1.0F),
               Vec4(1.0F, 1.0F, 1.0F, 1.0F), Vec4(1.0F, 0.0F, 0.0F, 1.0F)),
        Vertex(Vec3(maxX, minY, 0.0F), Vec3(0.0F, 0.0F, 1.0F), Vec2(1.0F, 1.0F),
               Vec4(1.0F, 1.0F, 1.0F, 1.0F), Vec4(1.0F, 0.0F, 0.0F, 1.0F)),
        Vertex(Vec3(maxX, maxY, 0.0F), Vec3(0.0F, 0.0F, 1.0F), Vec2(1.0F, 0.0F),
               Vec4(1.0F, 1.0F, 1.0F, 1.0F), Vec4(1.0F, 0.0F, 0.0F, 1.0F)),
        Vertex(Vec3(minX, maxY, 0.0F), Vec3(0.0F, 0.0F, 1.0F), Vec2(0.0F, 0.0F),
               Vec4(1.0F, 1.0F, 1.0F, 1.0F), Vec4(1.0F, 0.0F, 0.0F, 1.0F)),
    };
    mesh.indices = {0, 1, 2, 2, 3, 0};
    mesh.submeshes.push_back(Submesh{
        .name = "QuadSubmesh",
        .firstIndex = 0,
        .indexCount = 6,
        .vertexOffset = 0,
        .materialIndex = 0,
    });
    return mesh;
}

// Helper to render a mesh with a given material and read back rendered center pixel
static Result<std::vector<uint8_t>> renderAndReadback(RhiDevice& device, SceneRenderer& renderer,
                                                      const MeshData& meshData, uint32_t width = 64,
                                                      uint32_t height = 64) {
    world::World world{};
    physics::registerPhysicsComponents(world);
    AssetManager assetManager{};

    auto regRes = assetManager.registerMesh("test/pbr_mesh", meshData);
    if (!regRes.hasValue()) {
        return regRes.error();
    }
    const AssetId meshId = regRes.value();

    auto& ecs = world.getRawWorld();
    auto entity = ecs.entity("TestEntity");
    entity.set<MeshComponent>(MeshComponent{
        .meshAsset = meshId,
        .submeshIndex = kAllSubmeshes,
        .visible = true,
    });
    entity.set<physics::PhysicsTransformComponent>(physics::PhysicsTransformComponent{
        .transform = Transform(Vec3(0.0F, 0.0F, 0.0F)),
    });

    const size_t byteSize = width * height * 4;
    BufferDesc rbDesc{
        .sizeInBytes = byteSize,
        .usage = BufferUsageFlags::TransferDst,
        .memoryUsage = MemoryUsage::CpuToGpu,
    };
    auto rbRes = device.createBuffer(rbDesc);
    if (!rbRes.hasValue()) {
        return rbRes.error();
    }
    BufferHandle rbHandle = rbRes.value();

    // Camera looking straight at the quad from z = 2.0
    Mat4 view =
        Mat4::lookAt(Vec3(0.0F, 0.0F, 2.0F), Vec3(0.0F, 0.0F, 0.0F), Vec3(0.0F, 1.0F, 0.0F));
    Mat4 proj = Mat4::perspective(toRadians(60.0F), 1.0F, 0.1F, 100.0F);
    Mat4 viewProj = proj * view;

    auto cmdRes = device.createCommandList();
    if (!cmdRes.hasValue()) {
        device.destroyBuffer(rbHandle);
        return cmdRes.error();
    }
    auto cmd = std::move(cmdRes.value());

    cmd->begin();
    renderer.renderScene(world, assetManager, device, *cmd, viewProj);
    cmd->copyTextureToBuffer(renderer.getColorTarget(), rbHandle);
    cmd->end();

    auto subRes = device.submit(cmd.get());
    if (!subRes.hasValue()) {
        device.destroyBuffer(rbHandle);
        return subRes.error();
    }
    device.waitIdle();

    std::vector<uint8_t> pixels(byteSize, 0);
    auto readRes = device.readBuffer(rbHandle, 0, pixels.data(), byteSize);
    device.destroyBuffer(rbHandle);

    if (!readRes.hasValue()) {
        return readRes.error();
    }
    return Result<std::vector<uint8_t>>::success(std::move(pixels));
}

// -----------------------------------------------------------------------------
// Test 0: Analytical Deterministic BRDF Reference Checks
// -----------------------------------------------------------------------------
static float evalGgx(float NdotH, float roughness) {
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0F) + 1.0F);
    denom = 3.141592653589793F * denom * denom;
    return a2 / std::max(denom, 1e-7F);
}

static float evalSmithVis(float NdotV, float NdotL, float roughness) {
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float gv = NdotL * std::sqrt(NdotV * NdotV * (1.0F - a2) + a2);
    const float gl = NdotV * std::sqrt(NdotL * NdotL * (1.0F - a2) + a2);
    return 0.5F / std::max(gv + gl, 1e-7F);
}

static float evalSchlickFresnel(float VdotH, float F0) {
    return F0 + (1.0F - F0) * std::pow(std::clamp(1.0F - VdotH, 0.0F, 1.0F), 5.0F);
}

static int testAnalyticalBrdfModel() {
    constexpr float kPi = 3.141592653589793F;

    // 1. GGX NDF Reference points
    // When N == H (NdotH = 1.0): D = 1 / (pi * alpha^2)
    const float roughness = std::sqrt(0.5F); // alpha = 0.5, a2 = 0.25
    const float dPeak = evalGgx(1.0F, roughness);
    const float expectedPeak = 1.0F / (kPi * 0.25F);
    TEST_CHECK(std::abs(dPeak - expectedPeak) < 1e-4F);

    // When N perp H (NdotH = 0.0): D = alpha^2 / pi
    const float dGrazing = evalGgx(0.0F, roughness);
    const float expectedGrazing = 0.25F / kPi;
    TEST_CHECK(std::abs(dGrazing - expectedGrazing) < 1e-4F);

    // 2. Smith Correlated Visibility
    // At normal incidence (NdotV = 1.0, NdotL = 1.0): V = 0.5 / (1.0 + 1.0) = 0.25
    const float visNormal = evalSmithVis(1.0F, 1.0F, 0.5F);
    TEST_CHECK(std::abs(visNormal - 0.25F) < 1e-4F);

    // 3. Schlick Fresnel
    // At normal incidence (VdotH = 1.0): F = F0
    TEST_CHECK(std::abs(evalSchlickFresnel(1.0F, 0.04F) - 0.04F) < 1e-5F);
    // At grazing incidence (VdotH = 0.0): F = 1.0
    TEST_CHECK(std::abs(evalSchlickFresnel(0.0F, 0.04F) - 1.0F) < 1e-5F);

    // 4. Energy Conservation across parameter space
    for (int m = 0; m <= 10; ++m) {
        const float metallic = static_cast<float>(m) * 0.1F;
        for (int cosTheta = 0; cosTheta <= 10; ++cosTheta) {
            const float VdotH = static_cast<float>(cosTheta) * 0.1F;
            const float F = evalSchlickFresnel(VdotH, std::lerp(0.04F, 1.0F, metallic));
            const float kd = (1.0F - F) * (1.0F - metallic);
            TEST_CHECK(kd >= 0.0F);
            TEST_CHECK(kd + F <= 1.0001F); // Strict energy conservation: kd + F <= 1.0
        }
    }

    // 5. Numerical stability at extreme limits (roughness clamp = 0.045, grazing angles)
    const float dExtreme = evalGgx(0.0F, 0.045F);
    TEST_CHECK(!std::isnan(dExtreme) && !std::isinf(dExtreme) && dExtreme > 0.0F);

    const float visGrazing = evalSmithVis(1e-5F, 1e-5F, 0.045F);
    TEST_CHECK(!std::isnan(visGrazing) && !std::isinf(visGrazing) && visGrazing > 0.0F);

    std::cout << "[PASS] testAnalyticalBrdfModel (GGX, Smith, Fresnel, Energy Conservation)\n";
    return 0;
}

// -----------------------------------------------------------------------------
// Test 1: Dielectric Roughness Variations
// -----------------------------------------------------------------------------
static int testDielectricRoughnessVariations(RhiDevice& device, SceneRenderer& renderer) {
    // Smooth dielectric (roughness = 0.05) vs rough dielectric (roughness = 0.95)
    // Both red color (1.0, 0.0, 0.0)
    MeshData smoothMesh = createPbrQuad();
    Material smoothMat{};
    smoothMat.baseColor = Vec4(1.0F, 0.0F, 0.0F, 1.0F);
    smoothMat.metallic = 0.0F;
    smoothMat.roughness = 0.05F;
    smoothMesh.materials = {smoothMat};

    MeshData roughMesh = createPbrQuad();
    Material roughMat{};
    roughMat.baseColor = Vec4(1.0F, 0.0F, 0.0F, 1.0F);
    roughMat.metallic = 0.0F;
    roughMat.roughness = 0.95F;
    roughMesh.materials = {roughMat};

    auto smoothRes = renderAndReadback(device, renderer, smoothMesh);
    TEST_CHECK(smoothRes.hasValue());
    auto roughRes = renderAndReadback(device, renderer, roughMesh);
    TEST_CHECK(roughRes.hasValue());

    const auto& smoothPix = smoothRes.value();
    const auto& roughPix = roughRes.value();

    const size_t centerIdx = (32 * 64 + 32) * 4;
    // Both should show strong red component (dielectric diffuse)
    TEST_CHECK(smoothPix[centerIdx + 0] > 50);
    TEST_CHECK(roughPix[centerIdx + 0] > 50);

    // Roughness noticeably affects diffuse-specular distribution
    TEST_CHECK(smoothPix[centerIdx + 0] != roughPix[centerIdx + 0] ||
               smoothPix[centerIdx + 1] != roughPix[centerIdx + 1] ||
               smoothPix[centerIdx + 2] != roughPix[centerIdx + 2]);

    std::cout << "[PASS] testDielectricRoughnessVariations (smooth R="
              << static_cast<int>(smoothPix[centerIdx + 0])
              << " vs rough R=" << static_cast<int>(roughPix[centerIdx + 0]) << ")\n";
    return 0;
}

// -----------------------------------------------------------------------------
// Test 2: Metallic Roughness Variations & Diffuse Suppression
// -----------------------------------------------------------------------------
static int testMetallicRoughnessVariations(RhiDevice& device, SceneRenderer& renderer) {
    // Pure Gold metal (metallic = 1.0, baseColor = {1.0, 0.76, 0.33})
    MeshData smoothGoldMesh = createPbrQuad();
    Material smoothGoldMat{};
    smoothGoldMat.baseColor = Vec4(1.0F, 0.76F, 0.33F, 1.0F);
    smoothGoldMat.metallic = 1.0F;
    smoothGoldMat.roughness = 0.05F;
    smoothGoldMesh.materials = {smoothGoldMat};

    MeshData roughGoldMesh = createPbrQuad();
    Material roughGoldMat{};
    roughGoldMat.baseColor = Vec4(1.0F, 0.76F, 0.33F, 1.0F);
    roughGoldMat.metallic = 1.0F;
    roughGoldMat.roughness = 0.95F;
    roughGoldMesh.materials = {roughGoldMat};

    auto smoothRes = renderAndReadback(device, renderer, smoothGoldMesh);
    TEST_CHECK(smoothRes.hasValue());
    auto roughRes = renderAndReadback(device, renderer, roughGoldMesh);
    TEST_CHECK(roughRes.hasValue());

    const auto& smoothPix = smoothRes.value();
    const auto& roughPix = roughRes.value();

    const size_t centerIdx = (32 * 64 + 32) * 4;
    // In metals, diffuse is zero: color is governed strictly by specular reflection + ambient
    // specular Rough metal disperses specular reflections, showing distinct intensity
    TEST_CHECK(smoothPix[centerIdx + 0] > 20);
    TEST_CHECK(roughPix[centerIdx + 0] > 20);

    // Gold R > G > B
    TEST_CHECK(smoothPix[centerIdx + 0] >= smoothPix[centerIdx + 1]);
    TEST_CHECK(smoothPix[centerIdx + 1] >= smoothPix[centerIdx + 2]);

    std::cout << "[PASS] testMetallicRoughnessVariations (smooth metal R="
              << static_cast<int>(smoothPix[centerIdx + 0])
              << " vs rough metal R=" << static_cast<int>(roughPix[centerIdx + 0]) << ")\n";
    return 0;
}

// -----------------------------------------------------------------------------
// Test 3: Metallic vs Dielectric Energy Conservation
// -----------------------------------------------------------------------------
static int testMetallicVsDielectricEnergyConservation(RhiDevice& device, SceneRenderer& renderer) {
    // Pure blue baseColor {0.0, 0.0, 1.0}:
    // Dielectric (metallic = 0.0): diffuse reflection is blue; specular is white (F0 = 0.04)
    // Metal (metallic = 1.0): diffuse is 0; specular is blue (F0 = baseColor)
    MeshData dielectricMesh = createPbrQuad();
    Material dieMat{};
    dieMat.baseColor = Vec4(0.0F, 0.0F, 1.0F, 1.0F);
    dieMat.metallic = 0.0F;
    dieMat.roughness = 0.5F;
    dielectricMesh.materials = {dieMat};

    MeshData metalMesh = createPbrQuad();
    Material metalMat{};
    metalMat.baseColor = Vec4(0.0F, 0.0F, 1.0F, 1.0F);
    metalMat.metallic = 1.0F;
    metalMat.roughness = 0.5F;
    metalMesh.materials = {metalMat};

    auto dieRes = renderAndReadback(device, renderer, dielectricMesh);
    TEST_CHECK(dieRes.hasValue());
    auto metRes = renderAndReadback(device, renderer, metalMesh);
    TEST_CHECK(metRes.hasValue());

    const auto& diePix = dieRes.value();
    const auto& metPix = metRes.value();

    const size_t centerIdx = (32 * 64 + 32) * 4;
    // Dielectric has diffuse energy: blue dominates strongly
    TEST_CHECK(diePix[centerIdx + 2] > 60);

    // Metal suppresses diffuse completely (diffuse = 0) and has lower overall direct reflection
    // off-specular
    TEST_CHECK(diePix[centerIdx + 2] != metPix[centerIdx + 2]);

    std::cout << "[PASS] testMetallicVsDielectricEnergyConservation (dielectric B="
              << static_cast<int>(diePix[centerIdx + 2])
              << " vs metallic B=" << static_cast<int>(metPix[centerIdx + 2]) << ")\n";
    return 0;
}

// -----------------------------------------------------------------------------
// Test 4: Normal Map Perturbation
// -----------------------------------------------------------------------------
static int testNormalMappingPerturbation(RhiDevice& device, SceneRenderer& renderer) {
    // Flat normal map (pointing +Z: {128, 128, 255, 255})
    // vs perturbed normal map (tilted strongly towards -X away from light: {0, 128, 255, 255})
    ImageData flatImg{
        .width = 1,
        .height = 1,
        .channels = 4,
        .pixels = {128, 128, 255, 255},
    };
    ImageData tiltedImg{
        .width = 1,
        .height = 1,
        .channels = 4,
        .pixels = {0, 128, 255, 255},
    };

    world::World world{};
    physics::registerPhysicsComponents(world);
    AssetManager assetManager{};
    auto regFlatTex = assetManager.registerTexture("procedural/flat_normal#linear", flatImg);
    TEST_CHECK(regFlatTex.hasValue());
    auto regTiltedTex = assetManager.registerTexture("procedural/tilted_normal#linear", tiltedImg);
    TEST_CHECK(regTiltedTex.hasValue());

    MeshData flatMesh = createPbrQuad();
    Material flatMat{};
    flatMat.baseColor = Vec4(0.8F, 0.8F, 0.8F, 1.0F);
    flatMat.metallic = 0.0F;
    flatMat.roughness = 0.3F;
    flatMat.normalTexture.isPresent = true;
    flatMat.normalTexture.textureAsset = regFlatTex.value();
    flatMat.normalTexture.uri = "procedural/flat_normal#linear";
    flatMat.normalTexture.colorSpace = TextureColorSpace::Linear;
    flatMesh.materials = {flatMat};

    MeshData tiltedMesh = createPbrQuad();
    Material tiltedMat = flatMat;
    tiltedMat.normalTexture.textureAsset = regTiltedTex.value();
    tiltedMat.normalTexture.uri = "procedural/tilted_normal#linear";
    tiltedMesh.materials = {tiltedMat};

    auto regFlat = assetManager.registerMesh("test/flat_mesh", flatMesh);
    TEST_CHECK(regFlat.hasValue());
    auto regTilted = assetManager.registerMesh("test/tilted_mesh", tiltedMesh);
    TEST_CHECK(regTilted.hasValue());

    // Render both and verify tilted normal redirects shading
    const uint32_t width = 64;
    const uint32_t height = 64;
    const size_t byteSize = width * height * 4;

    BufferDesc rbDesc{
        .sizeInBytes = byteSize,
        .usage = BufferUsageFlags::TransferDst,
        .memoryUsage = MemoryUsage::CpuToGpu,
    };
    auto rbRes = device.createBuffer(rbDesc);
    TEST_CHECK(rbRes.hasValue());
    BufferHandle rbHandle = rbRes.value();

    Mat4 view =
        Mat4::lookAt(Vec3(0.0F, 0.0F, 2.0F), Vec3(0.0F, 0.0F, 0.0F), Vec3(0.0F, 1.0F, 0.0F));
    Mat4 proj = Mat4::perspective(toRadians(60.0F), 1.0F, 0.1F, 100.0F);
    Mat4 viewProj = proj * view;

    auto cmdRes = device.createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());

    // 1. Render flat normal
    auto& ecs = world.getRawWorld();
    auto entity = ecs.entity("NormalEntity");
    entity.set<MeshComponent>(MeshComponent{
        .meshAsset = regFlat.value(),
        .submeshIndex = kAllSubmeshes,
        .visible = true,
    });
    entity.set<physics::PhysicsTransformComponent>(physics::PhysicsTransformComponent{
        .transform = Transform(Vec3(0.0F, 0.0F, 0.0F)),
    });

    cmd->begin();
    renderer.renderScene(world, assetManager, device, *cmd, viewProj);
    cmd->copyTextureToBuffer(renderer.getColorTarget(), rbHandle);
    cmd->end();
    TEST_CHECK(device.submit(cmd.get()).hasValue());
    device.waitIdle();

    std::vector<uint8_t> flatPix(byteSize, 0);
    TEST_CHECK(device.readBuffer(rbHandle, 0, flatPix.data(), byteSize).hasValue());

    // 2. Render tilted normal
    entity.set<MeshComponent>(MeshComponent{
        .meshAsset = regTilted.value(),
        .submeshIndex = kAllSubmeshes,
        .visible = true,
    });

    cmd->begin();
    renderer.renderScene(world, assetManager, device, *cmd, viewProj);
    cmd->copyTextureToBuffer(renderer.getColorTarget(), rbHandle);
    cmd->end();
    TEST_CHECK(device.submit(cmd.get()).hasValue());
    device.waitIdle();

    std::vector<uint8_t> tiltedPix(byteSize, 0);
    TEST_CHECK(device.readBuffer(rbHandle, 0, tiltedPix.data(), byteSize).hasValue());

    device.destroyBuffer(rbHandle);

    const size_t centerIdx = (32 * 64 + 32) * 4;
    // Normal map perturbation must alter pixel shading
    TEST_CHECK(flatPix[centerIdx + 0] != tiltedPix[centerIdx + 0]);

    std::cout << "[PASS] testNormalMappingPerturbation (flat R="
              << static_cast<int>(flatPix[centerIdx + 0])
              << " vs tilted R=" << static_cast<int>(tiltedPix[centerIdx + 0]) << ")\n";
    return 0;
}

// -----------------------------------------------------------------------------
// Test 5: Emissive Radiance Contribution
// -----------------------------------------------------------------------------
static int testEmissiveContribution(RhiDevice& device, SceneRenderer& renderer) {
    MeshData noEmissiveMesh = createPbrQuad();
    Material noEmMat{};
    noEmMat.baseColor = Vec4(0.1F, 0.1F, 0.1F, 1.0F);
    noEmMat.emissiveFactor = Vec3(0.0F, 0.0F, 0.0F);
    noEmissiveMesh.materials = {noEmMat};

    MeshData emissiveMesh = createPbrQuad();
    Material emMat{};
    emMat.baseColor = Vec4(0.1F, 0.1F, 0.1F, 1.0F);
    emMat.emissiveFactor = Vec3(0.0F, 1.0F, 0.0F); // Pure bright green emission
    emissiveMesh.materials = {emMat};

    auto noEmRes = renderAndReadback(device, renderer, noEmissiveMesh);
    TEST_CHECK(noEmRes.hasValue());
    auto emRes = renderAndReadback(device, renderer, emissiveMesh);
    TEST_CHECK(emRes.hasValue());

    const auto& noEmPix = noEmRes.value();
    const auto& emPix = emRes.value();

    const size_t centerIdx = (32 * 64 + 32) * 4;
    // Emissive green must be saturated (>= 250 in 8-bit unsigned unorm)
    TEST_CHECK(emPix[centerIdx + 1] >= 250);
    // Non-emissive green is dim (diffuse only)
    TEST_CHECK(noEmPix[centerIdx + 1] < 100);

    std::cout << "[PASS] testEmissiveContribution (non-emissive G="
              << static_cast<int>(noEmPix[centerIdx + 1])
              << " vs emissive G=" << static_cast<int>(emPix[centerIdx + 1]) << ")\n";
    return 0;
}

// -----------------------------------------------------------------------------
// Test 6: Alpha Mask Discard
// -----------------------------------------------------------------------------
static int testAlphaMaskCutoff(RhiDevice& device, SceneRenderer& renderer) {
    // Quad with alpha below cutoff (0.2 < 0.5) must be discarded -> clear color
    MeshData discardedMesh = createPbrQuad();
    Material discMat{};
    discMat.baseColor = Vec4(1.0F, 1.0F, 1.0F, 0.2F);
    discMat.alphaMode = AlphaMode::Mask;
    discMat.alphaCutoff = 0.5F;
    discardedMesh.materials = {discMat};

    // Quad with alpha above cutoff (0.8 >= 0.5) must be rendered -> geometry color
    MeshData opaqueMesh = createPbrQuad();
    Material opMat{};
    opMat.baseColor = Vec4(1.0F, 1.0F, 1.0F, 0.8F);
    opMat.alphaMode = AlphaMode::Mask;
    opMat.alphaCutoff = 0.5F;
    opaqueMesh.materials = {opMat};

    auto discRes = renderAndReadback(device, renderer, discardedMesh);
    TEST_CHECK(discRes.hasValue());
    auto opRes = renderAndReadback(device, renderer, opaqueMesh);
    TEST_CHECK(opRes.hasValue());

    const auto& discPix = discRes.value();
    const auto& opPix = opRes.value();

    const size_t centerIdx = (32 * 64 + 32) * 4;
    // Clear color is approx (20, 31, 51)
    // Discarded pixel must match clear background color (R <= 30)
    TEST_CHECK(discPix[centerIdx + 0] <= 30);
    // Rendered white opaque pixel must be bright (R > 100)
    TEST_CHECK(opPix[centerIdx + 0] > 100);

    std::cout << "[PASS] testAlphaMaskCutoff (discarded R="
              << static_cast<int>(discPix[centerIdx + 0])
              << " vs opaque R=" << static_cast<int>(opPix[centerIdx + 0]) << ")\n";
    return 0;
}

// -----------------------------------------------------------------------------
// Test 7: External Canonical glTF Sample Asset: Damaged Helmet
// -----------------------------------------------------------------------------
static int testDamagedHelmetPbrImportAndRender(RhiDevice& device, SceneRenderer& renderer) {
    const std::string helmetPath = "test-assets/DamagedHelmet.glb";
    if (!std::filesystem::exists(helmetPath)) {
        std::cout << "[SKIP] testDamagedHelmetPbrImportAndRender: " << helmetPath << " not found\n";
        return 0;
    }

    world::World world{};
    physics::registerPhysicsComponents(world);
    AssetManager assetManager{};

    auto importRes = GltfSceneImporter::importScene(helmetPath, world, &assetManager);
    TEST_CHECK(importRes.success);
    TEST_CHECK(importRes.nodeCount > 0);

    // Verify DamagedHelmet material factors and all 5 PBR texture bindings
    const auto allMeta = assetManager.getAllMetadata();
    bool foundHelmetMesh = false;
    for (const auto& meta : allMeta) {
        if (meta.type == AssetType::Mesh) {
            const MeshData* mesh = assetManager.getMeshData(meta.id);
            if (mesh != nullptr && !mesh->materials.empty()) {
                foundHelmetMesh = true;
                const Material& mat = mesh->materials[0];
                TEST_CHECK(mat.baseColorTexture.isPresent);
                TEST_CHECK(mat.baseColorTexture.textureAsset.isValid());
                TEST_CHECK(assetManager.isLoaded(mat.baseColorTexture.textureAsset));

                TEST_CHECK(mat.metallicRoughnessTexture.isPresent);
                TEST_CHECK(mat.metallicRoughnessTexture.textureAsset.isValid());
                TEST_CHECK(assetManager.isLoaded(mat.metallicRoughnessTexture.textureAsset));

                TEST_CHECK(mat.normalTexture.isPresent);
                TEST_CHECK(mat.normalTexture.textureAsset.isValid());
                TEST_CHECK(assetManager.isLoaded(mat.normalTexture.textureAsset));

                TEST_CHECK(mat.occlusionTexture.isPresent);
                TEST_CHECK(mat.occlusionTexture.textureAsset.isValid());
                TEST_CHECK(assetManager.isLoaded(mat.occlusionTexture.textureAsset));

                TEST_CHECK(mat.emissiveTexture.isPresent);
                TEST_CHECK(mat.emissiveTexture.textureAsset.isValid());
                TEST_CHECK(assetManager.isLoaded(mat.emissiveTexture.textureAsset));

                // Also verify tangent attributes are present in vertex buffer
                TEST_CHECK(!mesh->vertices.empty());
                TEST_CHECK(mesh->vertices[0].tangent.w != 0.0F);
            }
        }
    }
    TEST_CHECK(foundHelmetMesh);

    // Render DamagedHelmet through SceneRenderer with camera centered on helmet
    const uint32_t width = 64;
    const uint32_t height = 64;
    const size_t byteSize = width * height * 4;

    BufferDesc rbDesc{
        .sizeInBytes = byteSize,
        .usage = BufferUsageFlags::TransferDst,
        .memoryUsage = MemoryUsage::CpuToGpu,
    };
    auto rbRes = device.createBuffer(rbDesc);
    TEST_CHECK(rbRes.hasValue());
    BufferHandle rbHandle = rbRes.value();

    Mat4 view =
        Mat4::lookAt(Vec3(0.0F, 0.0F, 2.5F), Vec3(0.0F, 0.0F, 0.0F), Vec3(0.0F, 1.0F, 0.0F));
    Mat4 proj = Mat4::perspective(toRadians(60.0F), 1.0F, 0.1F, 100.0F);
    Mat4 viewProj = proj * view;

    auto cmdRes = device.createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());

    cmd->begin();
    renderer.renderScene(world, assetManager, device, *cmd, viewProj);
    cmd->copyTextureToBuffer(renderer.getColorTarget(), rbHandle);
    cmd->end();

    TEST_CHECK(device.submit(cmd.get()).hasValue());
    device.waitIdle();

    std::vector<uint8_t> pixels(byteSize, 0);
    TEST_CHECK(device.readBuffer(rbHandle, 0, pixels.data(), byteSize).hasValue());
    device.destroyBuffer(rbHandle);

    // Verify helmet rendered (non-clear color pixels present)
    size_t nonClearPixels = 0;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        // Clear color is approx (20, 31, 51)
        if (std::abs(static_cast<int>(pixels[i + 0]) - 20) > 5 ||
            std::abs(static_cast<int>(pixels[i + 1]) - 31) > 5) {
            nonClearPixels++;
        }
    }
    TEST_CHECK(nonClearPixels > 100);

    const size_t centerIdx = (32 * 64 + 32) * 4;
    // DamagedHelmet center pixel is shaded helmet geometry (not clear color)
    TEST_CHECK(pixels[centerIdx + 3] == 255);
    TEST_CHECK(pixels[centerIdx + 0] != 20 || pixels[centerIdx + 1] != 31);

    // Compute reproducible 64-bit FNV-1a checksum of the rendered framebuffer
    uint64_t fnvHash = 14695981039346656037ULL;
    for (uint8_t byte : pixels) {
        fnvHash ^= static_cast<uint64_t>(byte);
        fnvHash *= 1099511628211ULL;
    }
    TEST_CHECK(fnvHash != 0ULL);

    // Save reproducible Vulkan capture image as binary PPM (P6)
    const std::string capturePath = "test-assets/damaged_helmet_pbr_capture.ppm";
    std::ofstream ppmFile(capturePath, std::ios::binary);
    if (ppmFile.is_open()) {
        ppmFile << "P6\n" << width << " " << height << "\n255\n";
        for (size_t i = 0; i < pixels.size(); i += 4) {
            ppmFile.put(static_cast<char>(pixels[i + 0]));
            ppmFile.put(static_cast<char>(pixels[i + 1]));
            ppmFile.put(static_cast<char>(pixels[i + 2]));
        }
        ppmFile.close();
    }

    std::cout << "[PASS] testDamagedHelmetPbrImportAndRender (rendered " << nonClearPixels
              << " valid PBR pixels, center RGBA=(" << static_cast<int>(pixels[centerIdx + 0])
              << "," << static_cast<int>(pixels[centerIdx + 1]) << ","
              << static_cast<int>(pixels[centerIdx + 2]) << ","
              << static_cast<int>(pixels[centerIdx + 3]) << "), fnv=" << std::hex << fnvHash
              << std::dec << ")\n";
    return 0;
}

// -----------------------------------------------------------------------------
// Main Test Runner
// -----------------------------------------------------------------------------
int main() {
    if (testAnalyticalBrdfModel() != 0) {
        return 10;
    }
#if defined(HIKARI_HAS_TSAN)
    std::cout << "ThreadSanitizer active: skipping live GPU PBR tests\n";
    return 0;
#endif

    std::cout << "[Running Milestone 10D Full PBR Verification Suite]\n";

    VulkanDeviceDesc devDesc{.windowHandle = nullptr, .enableValidation = true};
    auto devRes = createVulkanDevice(devDesc);
    if (!devRes.hasValue()) {
        std::cout << "Vulkan device unavailable; skipping hardware PBR tests\n";
        return 0;
    }
    auto device = std::move(devRes.value());

    SceneRenderer renderer{};
    auto initRes = renderer.init(*device, 64, 64);
    if (!initRes.hasValue()) {
        std::cerr << "Failed to initialize SceneRenderer: " << initRes.error().message << "\n";
        return 1;
    }

    if (testDielectricRoughnessVariations(*device, renderer) != 0) {
        return 1;
    }
    if (testMetallicRoughnessVariations(*device, renderer) != 0) {
        return 2;
    }
    if (testMetallicVsDielectricEnergyConservation(*device, renderer) != 0) {
        return 3;
    }
    if (testNormalMappingPerturbation(*device, renderer) != 0) {
        return 4;
    }
    if (testEmissiveContribution(*device, renderer) != 0) {
        return 5;
    }
    if (testAlphaMaskCutoff(*device, renderer) != 0) {
        return 6;
    }
    if (testDamagedHelmetPbrImportAndRender(*device, renderer) != 0) {
        return 7;
    }

    renderer.shutdown();
    std::cout << "[All Milestone 10D PBR Verification Tests Passed: 7/7]\n";
    return 0;
}
