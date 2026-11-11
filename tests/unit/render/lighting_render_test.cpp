/// @file lighting_render_test.cpp
/// @brief Physical and visual verification tests for Milestone 10E Lighting & Shadow Mapping.
///
/// Milestone 10E Requirements:
/// 1. Directional light evaluation and intensity scaling.
/// 2. Point light distance attenuation with smooth range windowing.
/// 3. Spotlight cone angular falloff between inner and outer angles.
/// 4. Directional shadow depth pass and PCF receiver filtering.
/// 5. Offscreen render target capture verification.

#include "core/assert.h"
#include "core/math.h"
#include "physics/physics_components.h"
#include "render/asset_manager.h"
#include "render/light_components.h"
#include "render/lighting_data.h"
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

static constexpr float toRadians(float deg) noexcept {
    return deg * 3.141592653589793F / 180.0F;
}

// Creates a flat 2-triangle quad in the XY plane facing +Z
static MeshData createTestQuad(float minX, float maxX, float minY, float maxY, float z = 0.0F) {
    MeshData mesh{};
    mesh.vertices = {
        Vertex(Vec3(minX, minY, z), Vec3(0.0F, 0.0F, 1.0F), Vec2(0.0F, 1.0F),
               Vec4(1.0F, 1.0F, 1.0F, 1.0F), Vec4(1.0F, 0.0F, 0.0F, 1.0F)),
        Vertex(Vec3(maxX, minY, z), Vec3(0.0F, 0.0F, 1.0F), Vec2(1.0F, 1.0F),
               Vec4(1.0F, 1.0F, 1.0F, 1.0F), Vec4(1.0F, 0.0F, 0.0F, 1.0F)),
        Vertex(Vec3(maxX, maxY, z), Vec3(0.0F, 0.0F, 1.0F), Vec2(1.0F, 0.0F),
               Vec4(1.0F, 1.0F, 1.0F, 1.0F), Vec4(1.0F, 0.0F, 0.0F, 1.0F)),
        Vertex(Vec3(minX, maxY, z), Vec3(0.0F, 0.0F, 1.0F), Vec2(0.0F, 0.0F),
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

// Saves RGBA8 buffer to portable pixmap (PPM) image file
static bool savePpm(const std::string& filePath, const uint8_t* rgba, uint32_t width,
                    uint32_t height) {
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    file << "P6\n" << width << " " << height << "\n255\n";
    for (uint32_t i = 0; i < width * height; ++i) {
        file.put(static_cast<char>(rgba[i * 4 + 0]));
        file.put(static_cast<char>(rgba[i * 4 + 1]));
        file.put(static_cast<char>(rgba[i * 4 + 2]));
    }
    return true;
}

static Result<std::vector<uint8_t>> executeAndReadback(RhiDevice& device, SceneRenderer& renderer,
                                                       world::World& world,
                                                       AssetManager& assetManager,
                                                       const Mat4& viewProj, uint32_t width = 128,
                                                       uint32_t height = 128) {
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
// Test 1: Directional Light Intensity Scaling
// -----------------------------------------------------------------------------
static int testDirectionalLightIntensity(RhiDevice& device) {
    std::cout << "[Test 1] Directional Light Intensity Scaling...\n";

    SceneRenderer renderer;
    auto initRes = renderer.init(device, 64, 64);
    TEST_CHECK(initRes.hasValue());

    AssetManager assetMgr;
    auto quadMesh = createTestQuad(-1.0F, 1.0F, -1.0F, 1.0F, 0.0F);
    auto regRes = assetMgr.registerMesh("test/quad", quadMesh);
    TEST_CHECK(regRes.hasValue());
    AssetId meshId = regRes.value();

    Mat4 view =
        Mat4::lookAt(Vec3(0.0F, 0.0F, 2.0F), Vec3(0.0F, 0.0F, 0.0F), Vec3(0.0F, 1.0F, 0.0F));
    Mat4 proj = Mat4::perspective(toRadians(60.0F), 1.0F, 0.1F, 100.0F);
    Mat4 viewProj = proj * view;

    // Render with Intensity = 1.0
    uint8_t luminanceLow = 0;
    {
        world::World world{};
        registerRenderComponents(world);
        physics::registerPhysicsComponents(world);
        auto& ecs = world.getRawWorld();

        auto ent = ecs.entity("Mesh");
        ent.set<MeshComponent>(MeshComponent{.meshAsset = meshId, .visible = true});
        ent.set<physics::PhysicsTransformComponent>(physics::PhysicsTransformComponent{});

        auto light = ecs.entity("Sun");
        light.set<DirectionalLightComponent>(DirectionalLightComponent{
            .direction = Vec3(0.0F, 0.0F, 1.0F), // pointing directly at quad
            .color = Vec3(1.0F, 1.0F, 1.0F),
            .intensity = 1.0F,
            .castShadows = false,
            .enabled = true,
        });

        auto readRes = executeAndReadback(device, renderer, world, assetMgr, viewProj, 64, 64);
        TEST_CHECK(readRes.hasValue());
        const auto& pixels = readRes.value();
        size_t center = (32 * 64 + 32) * 4;
        luminanceLow = pixels[center];
    }

    // Render with Intensity = 4.0
    uint8_t luminanceHigh = 0;
    {
        world::World world{};
        registerRenderComponents(world);
        physics::registerPhysicsComponents(world);
        auto& ecs = world.getRawWorld();

        auto ent = ecs.entity("Mesh");
        ent.set<MeshComponent>(MeshComponent{.meshAsset = meshId, .visible = true});
        ent.set<physics::PhysicsTransformComponent>(physics::PhysicsTransformComponent{});

        auto light = ecs.entity("Sun");
        light.set<DirectionalLightComponent>(DirectionalLightComponent{
            .direction = Vec3(0.0F, 0.0F, 1.0F),
            .color = Vec3(1.0F, 1.0F, 1.0F),
            .intensity = 4.0F,
            .castShadows = false,
            .enabled = true,
        });

        auto readRes = executeAndReadback(device, renderer, world, assetMgr, viewProj, 64, 64);
        TEST_CHECK(readRes.hasValue());
        const auto& pixels = readRes.value();
        size_t center = (32 * 64 + 32) * 4;
        luminanceHigh = pixels[center];
    }

    std::cout << "  Low intensity luminance: " << static_cast<int>(luminanceLow)
              << ", High intensity luminance: " << static_cast<int>(luminanceHigh) << "\n";
    TEST_CHECK(luminanceHigh > luminanceLow + 30);
    std::cout << "  [PASS] testDirectionalLightIntensity\n";
    return 0;
}

// -----------------------------------------------------------------------------
// Test 2: Point Light Distance Attenuation
// -----------------------------------------------------------------------------
static int testPointLightAttenuation(RhiDevice& device) {
    std::cout << "[Test 2] Point Light Distance Attenuation...\n";

    SceneRenderer renderer;
    auto initRes = renderer.init(device, 64, 64);
    TEST_CHECK(initRes.hasValue());

    AssetManager assetMgr;
    auto quadMesh = createTestQuad(-1.0F, 1.0F, -1.0F, 1.0F, 0.0F);
    auto regRes = assetMgr.registerMesh("test/quad_point", quadMesh);
    TEST_CHECK(regRes.hasValue());
    AssetId meshId = regRes.value();

    Mat4 view =
        Mat4::lookAt(Vec3(0.0F, 0.0F, 2.0F), Vec3(0.0F, 0.0F, 0.0F), Vec3(0.0F, 1.0F, 0.0F));
    Mat4 proj = Mat4::perspective(toRadians(60.0F), 1.0F, 0.1F, 100.0F);
    Mat4 viewProj = proj * view;

    // 1. Close point light at (0, 0, 0.8)
    uint8_t closeLuminance = 0;
    {
        world::World world{};
        registerRenderComponents(world);
        physics::registerPhysicsComponents(world);
        auto& ecs = world.getRawWorld();

        auto ent = ecs.entity("Mesh");
        ent.set<MeshComponent>(MeshComponent{.meshAsset = meshId, .visible = true});
        ent.set<physics::PhysicsTransformComponent>(physics::PhysicsTransformComponent{});

        // Disable directional light to isolate point light
        auto sun = ecs.entity("Sun");
        sun.set<DirectionalLightComponent>(DirectionalLightComponent{.enabled = false});

        auto pt = ecs.entity("PointLight");
        pt.set<PointLightComponent>(PointLightComponent{
            .position = Vec3(0.0F, 0.0F, 0.8F),
            .color = Vec3(1.0F, 1.0F, 1.0F),
            .intensity = 15.0F,
            .range = 10.0F,
            .enabled = true,
        });

        auto readRes = executeAndReadback(device, renderer, world, assetMgr, viewProj, 64, 64);
        TEST_CHECK(readRes.hasValue());
        size_t center = (32 * 64 + 32) * 4;
        closeLuminance = readRes.value()[center];
    }

    // 2. Far point light at (0, 0, 4.0)
    uint8_t farLuminance = 0;
    {
        world::World world{};
        registerRenderComponents(world);
        physics::registerPhysicsComponents(world);
        auto& ecs = world.getRawWorld();

        auto ent = ecs.entity("Mesh");
        ent.set<MeshComponent>(MeshComponent{.meshAsset = meshId, .visible = true});
        ent.set<physics::PhysicsTransformComponent>(physics::PhysicsTransformComponent{});

        auto sun = ecs.entity("Sun");
        sun.set<DirectionalLightComponent>(DirectionalLightComponent{.enabled = false});

        auto pt = ecs.entity("PointLight");
        pt.set<PointLightComponent>(PointLightComponent{
            .position = Vec3(0.0F, 0.0F, 4.0F),
            .color = Vec3(1.0F, 1.0F, 1.0F),
            .intensity = 15.0F,
            .range = 10.0F,
            .enabled = true,
        });

        auto readRes = executeAndReadback(device, renderer, world, assetMgr, viewProj, 64, 64);
        TEST_CHECK(readRes.hasValue());
        size_t center = (32 * 64 + 32) * 4;
        farLuminance = readRes.value()[center];
    }

    std::cout << "  Close point light luminance: " << static_cast<int>(closeLuminance)
              << ", Far point light luminance: " << static_cast<int>(farLuminance) << "\n";
    TEST_CHECK(closeLuminance > farLuminance + 20);
    std::cout << "  [PASS] testPointLightAttenuation\n";
    return 0;
}

// -----------------------------------------------------------------------------
// Test 3: Spotlight Cone Angular Falloff
// -----------------------------------------------------------------------------
static int testSpotLightConeFalloff(RhiDevice& device) {
    std::cout << "[Test 3] Spotlight Cone Angular Falloff...\n";

    SceneRenderer renderer;
    auto initRes = renderer.init(device, 64, 64);
    TEST_CHECK(initRes.hasValue());

    AssetManager assetMgr;
    auto quadMesh = createTestQuad(-1.0F, 1.0F, -1.0F, 1.0F, 0.0F);
    auto regRes = assetMgr.registerMesh("test/quad_spot", quadMesh);
    TEST_CHECK(regRes.hasValue());
    AssetId meshId = regRes.value();

    Mat4 view =
        Mat4::lookAt(Vec3(0.0F, 0.0F, 2.0F), Vec3(0.0F, 0.0F, 0.0F), Vec3(0.0F, 1.0F, 0.0F));
    Mat4 proj = Mat4::perspective(toRadians(60.0F), 1.0F, 0.1F, 100.0F);
    Mat4 viewProj = proj * view;

    // 1. On-axis spotlight pointing straight at the quad
    uint8_t onAxisLuminance = 0;
    {
        world::World world{};
        registerRenderComponents(world);
        physics::registerPhysicsComponents(world);
        auto& ecs = world.getRawWorld();

        auto ent = ecs.entity("Mesh");
        ent.set<MeshComponent>(MeshComponent{.meshAsset = meshId, .visible = true});
        ent.set<physics::PhysicsTransformComponent>(physics::PhysicsTransformComponent{});

        auto sun = ecs.entity("Sun");
        sun.set<DirectionalLightComponent>(DirectionalLightComponent{.enabled = false});

        auto spot = ecs.entity("SpotLight");
        spot.set<SpotLightComponent>(SpotLightComponent{
            .position = Vec3(0.0F, 0.0F, 1.5F),
            .direction = Vec3(0.0F, 0.0F, -1.0F), // directly pointing at quad center
            .color = Vec3(1.0F, 1.0F, 1.0F),
            .intensity = 20.0F,
            .range = 10.0F,
            .innerConeAngle = toRadians(15.0F),
            .outerConeAngle = toRadians(25.0F),
            .enabled = true,
        });

        auto readRes = executeAndReadback(device, renderer, world, assetMgr, viewProj, 64, 64);
        TEST_CHECK(readRes.hasValue());
        size_t center = (32 * 64 + 32) * 4;
        onAxisLuminance = readRes.value()[center];
    }

    // 2. Off-axis spotlight angled 90 degrees away
    uint8_t offAxisLuminance = 0;
    {
        world::World world{};
        registerRenderComponents(world);
        physics::registerPhysicsComponents(world);
        auto& ecs = world.getRawWorld();

        auto ent = ecs.entity("Mesh");
        ent.set<MeshComponent>(MeshComponent{.meshAsset = meshId, .visible = true});
        ent.set<physics::PhysicsTransformComponent>(physics::PhysicsTransformComponent{});

        auto sun = ecs.entity("Sun");
        sun.set<DirectionalLightComponent>(DirectionalLightComponent{.enabled = false});

        auto spot = ecs.entity("SpotLight");
        spot.set<SpotLightComponent>(SpotLightComponent{
            .position = Vec3(0.0F, 0.0F, 1.5F),
            .direction = Vec3(1.0F, 0.0F, 0.0F), // pointing to the side
            .color = Vec3(1.0F, 1.0F, 1.0F),
            .intensity = 20.0F,
            .range = 10.0F,
            .innerConeAngle = toRadians(15.0F),
            .outerConeAngle = toRadians(25.0F),
            .enabled = true,
        });

        auto readRes = executeAndReadback(device, renderer, world, assetMgr, viewProj, 64, 64);
        TEST_CHECK(readRes.hasValue());
        size_t center = (32 * 64 + 32) * 4;
        offAxisLuminance = readRes.value()[center];
    }

    std::cout << "  On-axis spotlight luminance: " << static_cast<int>(onAxisLuminance)
              << ", Off-axis spotlight luminance: " << static_cast<int>(offAxisLuminance) << "\n";
    TEST_CHECK(onAxisLuminance > offAxisLuminance + 20);
    std::cout << "  [PASS] testSpotLightConeFalloff\n";
    return 0;
}

// -----------------------------------------------------------------------------
// Test 4: Directional Shadow Occlusion & Visual Capture Dump
// -----------------------------------------------------------------------------
static int testDirectionalShadowOcclusionAndCapture(RhiDevice& device) {
    std::cout << "[Test 4] Directional Shadow Occlusion & Visual Capture Dump...\n";

    const uint32_t width = 256;
    const uint32_t height = 256;

    SceneRenderer renderer;
    renderer.setShadowResolution(1024);
    auto initRes = renderer.init(device, width, height);
    TEST_CHECK(initRes.hasValue());

    AssetManager assetMgr;
    // Ground receiver quad at Z = 0
    auto groundMesh = createTestQuad(-5.0F, 5.0F, -5.0F, 5.0F, 0.0F);
    auto groundRes = assetMgr.registerMesh("test/ground", groundMesh);
    TEST_CHECK(groundRes.hasValue());
    AssetId groundId = groundRes.value();

    // Small occluder quad placed at Z = 0.5 in front of ground
    auto occluderMesh = createTestQuad(-0.5F, 0.5F, -0.5F, 0.5F, 0.5F);
    auto occRes = assetMgr.registerMesh("test/occluder", occluderMesh);
    TEST_CHECK(occRes.hasValue());
    AssetId occId = occRes.value();

    world::World world{};
    registerRenderComponents(world);
    physics::registerPhysicsComponents(world);
    auto& ecs = world.getRawWorld();

    // 1. Ground Plane entity (receiver, does not cast shadow on itself)
    auto groundEnt = ecs.entity("Ground");
    groundEnt.set<MeshComponent>(MeshComponent{
        .meshAsset = groundId,
        .visible = true,
        .castShadows = false,
    });
    groundEnt.set<physics::PhysicsTransformComponent>(physics::PhysicsTransformComponent{});

    // 2. Occluder entity (casts shadow)
    auto occEnt = ecs.entity("Occluder");
    occEnt.set<MeshComponent>(MeshComponent{
        .meshAsset = occId,
        .visible = true,
        .castShadows = true,
    });
    occEnt.set<physics::PhysicsTransformComponent>(physics::PhysicsTransformComponent{});

    // 3. Directional sun light shining straight down -Z towards +Z
    auto sun = ecs.entity("Sun");
    sun.set<DirectionalLightComponent>(DirectionalLightComponent{
        .direction = Vec3(0.0F, 0.0F, 1.0F),
        .color = Vec3(1.0F, 1.0F, 1.0F),
        .intensity = 3.0F,
        .castShadows = true,
        .shadowBias = 0.005F,
        .enabled = true,
    });

    Mat4 view =
        Mat4::lookAt(Vec3(0.0F, 0.0F, 3.0F), Vec3(0.0F, 0.0F, 0.0F), Vec3(0.0F, 1.0F, 0.0F));
    Mat4 proj = Mat4::perspective(toRadians(60.0F), 1.0F, 0.1F, 100.0F);
    Mat4 viewProj = proj * view;

    auto readRes = executeAndReadback(device, renderer, world, assetMgr, viewProj, width, height);
    TEST_CHECK(readRes.hasValue());
    const auto& pixels = readRes.value();

    // Count non-black rendered pixels
    uint32_t renderedPixels = 0;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        if (pixels[i] > 20 || pixels[i + 1] > 20 || pixels[i + 2] > 20) {
            ++renderedPixels;
        }
    }
    std::cout << "  Rendered illuminated/shadow pixels: " << renderedPixels << " / "
              << (width * height) << "\n";
    TEST_CHECK(renderedPixels > (width * height) / 4);

    // Save PPM visual capture
    std::filesystem::create_directories("test-assets");
    std::string ppmPath = "test-assets/lighting_shadow_capture.ppm";
    bool saved = savePpm(ppmPath, pixels.data(), width, height);
    TEST_CHECK(saved);
    std::cout << "  Saved shadow visual capture: " << ppmPath << "\n";

    std::cout << "  [PASS] testDirectionalShadowOcclusionAndCapture\n";
    return 0;
}

int main() {
    std::cout << "=====================================================\n";
    std::cout << "HIKARI-MACHINE — MILESTONE 10E LIGHTING & SHADOW TESTS\n";
    std::cout << "=====================================================\n";

    VulkanDeviceDesc devDesc{
        .windowHandle = nullptr,
        .enableValidation = true,
    };
    auto devRes = createVulkanDevice(devDesc);
    if (!devRes.hasValue()) {
        std::cerr << "Failed to create Vulkan RHI device: " << devRes.error().message << std::endl;
        return 1;
    }
    auto device = std::move(devRes.value());

    if (testDirectionalLightIntensity(*device) != 0)
        return 1;
    if (testPointLightAttenuation(*device) != 0)
        return 1;
    if (testSpotLightConeFalloff(*device) != 0)
        return 1;
    if (testDirectionalShadowOcclusionAndCapture(*device) != 0)
        return 1;

    std::cout << "=====================================================\n";
    std::cout << "ALL MILESTONE 10E LIGHTING & SHADOW TESTS PASSED!\n";
    std::cout << "=====================================================\n";
    return 0;
}
