/// @file scene_renderer_test.cpp
/// @brief Comprehensive tests for SceneRenderer GPU offscreen integration and pixel verification.

#include "core/assert.h"
#include "core/math.h"
#include "physics/physics_components.h"
#include "render/asset_manager.h"
#include "render/mesh_component.h"
#include "render/scene_renderer.h"
#include "rhi/rhi_device.h"
#include "world/world.h"

#include <cmath>
#include <cstring>
#include <iostream>
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
// Suppress known one-time process-lifetime allocations within OS graphics driver / dbus / wayland
// stacks
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
    return deg * (3.14159265358979323846F / 180.0F);
}

static int testSceneRendererLifecycleWithNullDevice() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());
    SceneRenderer renderer{};

    TEST_CHECK(!renderer.isInitialized());
    TEST_CHECK(!renderer.getColorTarget().isValid());
    TEST_CHECK(!renderer.getDepthTarget().isValid());
    TEST_CHECK(!renderer.getPipeline().isValid());
    TEST_CHECK(!renderer.getPipelineLayout().isValid());

    // 1. Initialize renderer
    auto initRes = renderer.init(*device, 800, 600);
    TEST_CHECK(initRes.hasValue());
    TEST_CHECK(renderer.isInitialized());
    TEST_CHECK(renderer.getColorTarget().isValid());
    TEST_CHECK(renderer.getDepthTarget().isValid());
    TEST_CHECK(renderer.getPipeline().isValid());
    TEST_CHECK(renderer.getPipelineLayout().isValid());
    TEST_CHECK(renderer.getWidth() == 800);
    TEST_CHECK(renderer.getHeight() == 600);

    // 2. Resize renderer
    auto resizeRes = renderer.resize(1024, 768);
    TEST_CHECK(resizeRes.hasValue());
    TEST_CHECK(renderer.getWidth() == 1024);
    TEST_CHECK(renderer.getHeight() == 768);
    TEST_CHECK(renderer.getColorTarget().isValid());
    TEST_CHECK(renderer.getDepthTarget().isValid());

    // Invalid resize dimensions
    TEST_CHECK(!renderer.resize(0, 768).hasValue());
    TEST_CHECK(!renderer.resize(1024, 0).hasValue());

    // 3. Move construction and assignment
    SceneRenderer movedRenderer = std::move(renderer);
    TEST_CHECK(!renderer.isInitialized());
    TEST_CHECK(movedRenderer.isInitialized());
    TEST_CHECK(movedRenderer.getWidth() == 1024);
    TEST_CHECK(movedRenderer.getHeight() == 768);

    // 4. Shutdown
    movedRenderer.shutdown();
    TEST_CHECK(!movedRenderer.isInitialized());
    TEST_CHECK(!movedRenderer.getColorTarget().isValid());
    TEST_CHECK(!movedRenderer.getDepthTarget().isValid());
    TEST_CHECK(movedRenderer.getWidth() == 0);
    TEST_CHECK(movedRenderer.getHeight() == 0);

    std::cout << "SceneRenderer lifecycle with NullDevice verified!\n";
    return 0;
}

static int testSceneRendererEmptyScene() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());
    SceneRenderer renderer{};
    TEST_CHECK(renderer.init(*device, 640, 480).hasValue());

    world::World world{};
    AssetManager assetManager{};

    auto cmdRes = device->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());
    cmd->begin();

    RenderStats stats = renderer.renderScene(world, assetManager, *device, *cmd);
    cmd->end();

    TEST_CHECK(stats.drawCalls == 0);
    TEST_CHECK(stats.triangles == 0);
    TEST_CHECK(stats.meshesRendered == 0);

    renderer.shutdown();
    std::cout << "SceneRenderer empty scene verified!\n";
    return 0;
}

static int testSceneRendererEntityFilteringAndVisibility() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());
    SceneRenderer renderer{};
    TEST_CHECK(renderer.init(*device, 640, 480).hasValue());

    world::World world{};
    physics::registerPhysicsComponents(world);
    AssetManager assetManager{};

    auto& ecs = world.getRawWorld();

    // 1. Invisible entity
    auto invEntity = ecs.entity("InvisibleObject");
    invEntity.set<MeshComponent>(MeshComponent{
        .assetPath = "assets/models/cube.obj",
        .visible = false,
    });

    // 2. Entity with non-existent asset path
    auto invalidAssetEntity = ecs.entity("NonExistentObject");
    invalidAssetEntity.set<MeshComponent>(MeshComponent{
        .assetPath = "assets/models/does_not_exist_xyz.obj",
        .visible = true,
    });

    auto cmdRes = device->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());
    cmd->begin();

    RenderStats stats = renderer.renderScene(world, assetManager, *device, *cmd);
    cmd->end();

    // Both entities should be safely skipped
    TEST_CHECK(stats.drawCalls == 0);
    TEST_CHECK(stats.triangles == 0);
    TEST_CHECK(stats.meshesRendered == 0);

    renderer.shutdown();
    std::cout << "SceneRenderer entity filtering and visibility verified!\n";
    return 0;
}

static int testSceneRendererMultipleEntities() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());
    SceneRenderer renderer{};
    TEST_CHECK(renderer.init(*device, 640, 480).hasValue());

    world::World world{};
    physics::registerPhysicsComponents(world);
    AssetManager assetManager{};

    auto loadRes = assetManager.loadMesh("assets/models/cube.obj");
    TEST_CHECK(loadRes.hasValue());
    AssetId cubeId = loadRes.value();

    auto& ecs = world.getRawWorld();

    // Entity 1
    auto e1 = ecs.entity("Cube1");
    e1.set<MeshComponent>(MeshComponent{
        .meshAsset = cubeId,
        .visible = true,
    });
    e1.set<physics::PhysicsTransformComponent>(physics::PhysicsTransformComponent{
        .transform = Transform(Vec3(-2.0F, 0.0F, 0.0F)),
    });

    // Entity 2
    auto e2 = ecs.entity("Cube2");
    e2.set<MeshComponent>(MeshComponent{
        .meshAsset = cubeId,
        .visible = true,
    });
    e2.set<physics::PhysicsTransformComponent>(physics::PhysicsTransformComponent{
        .transform = Transform(Vec3(2.0F, 0.0F, 0.0F)),
    });

    auto cmdRes = device->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());
    cmd->begin();

    Mat4 viewProj = Mat4::perspective(toRadians(60.0F), 1.333F, 0.1F, 100.0F) *
                    Mat4::lookAt(Vec3(0.0F, 0.0F, 5.0F), Vec3(0.0F, 0.0F, 0.0F));

    RenderStats stats = renderer.renderScene(world, assetManager, *device, *cmd, viewProj);
    cmd->end();

    TEST_CHECK(stats.drawCalls == 2);
    TEST_CHECK(stats.triangles > 0);
    TEST_CHECK(stats.meshesRendered == 2);

    renderer.shutdown();
    std::cout << "SceneRenderer multiple entities verified!\n";
    return 0;
}

static int testVulkanSceneRendererHardwareRenderingAndPixelReadback() {
#if defined(HIKARI_HAS_TSAN)
    std::cout << "ThreadSanitizer active: skipping live GPU driver initialization\n";
    return 0;
#endif

    VulkanDeviceDesc devDesc{.windowHandle = nullptr, .enableValidation = true};
    auto devRes = createVulkanDevice(devDesc);
    if (!devRes.hasValue()) {
        std::cout << "Vulkan device unavailable; skipping hardware rendering test\n";
        return 0;
    }
    auto device = std::move(devRes.value());

    const uint32_t vpWidth = 64;
    const uint32_t vpHeight = 64;
    const size_t byteSize = vpWidth * vpHeight * 4;

    SceneRenderer renderer{};
    auto initRes = renderer.init(*device, vpWidth, vpHeight);
    TEST_CHECK(initRes.hasValue());
    TEST_CHECK(renderer.isInitialized());

    world::World world{};
    physics::registerPhysicsComponents(world);
    AssetManager assetManager{};

    auto loadRes = assetManager.loadMesh("assets/models/cube.obj");
    TEST_CHECK(loadRes.hasValue());
    AssetId baseCubeId = loadRes.value();
    const MeshData* baseMesh = assetManager.getMeshData(baseCubeId);
    TEST_CHECK(baseMesh != nullptr);

    // Create distinguishable Front Red Cube
    MeshData redMesh = *baseMesh;
    for (auto& v : redMesh.vertices) {
        v.color = Vec4(1.0F, 0.0F, 0.0F, 1.0F); // Pure Red
    }
    auto redRegRes = assetManager.registerMesh("procedural/red_cube", redMesh);
    TEST_CHECK(redRegRes.hasValue());
    AssetId redCubeId = redRegRes.value();

    // Create distinguishable Rear Green Cube
    MeshData greenMesh = *baseMesh;
    for (auto& v : greenMesh.vertices) {
        v.color = Vec4(0.0F, 1.0F, 0.0F, 1.0F); // Pure Green
    }
    auto greenRegRes = assetManager.registerMesh("procedural/green_cube", greenMesh);
    TEST_CHECK(greenRegRes.hasValue());
    AssetId greenCubeId = greenRegRes.value();

    auto& ecs = world.getRawWorld();

    // Create a red cube entity at origin (z = 0.0, front)
    auto cubeEntity = ecs.entity("CenterCube");
    cubeEntity.set<MeshComponent>(MeshComponent{
        .meshAsset = redCubeId,
        .visible = true,
    });
    cubeEntity.set<physics::PhysicsTransformComponent>(physics::PhysicsTransformComponent{
        .transform = Transform(Vec3(0.0F, 0.0F, 0.0F)),
    });

    // Create destination buffer for offscreen readback
    BufferDesc readbackDesc{
        .sizeInBytes = byteSize,
        .usage = BufferUsageFlags::TransferDst,
        .memoryUsage = MemoryUsage::CpuToGpu,
    };
    auto readbackBufRes = device->createBuffer(readbackDesc);
    TEST_CHECK(readbackBufRes.hasValue());
    BufferHandle readbackBuf = readbackBufRes.value();

    // Camera looking directly at origin from z = 2.5 (cube extends z in [-0.5, 0.5])
    Mat4 view =
        Mat4::lookAt(Vec3(0.0F, 0.0F, 2.5F), Vec3(0.0F, 0.0F, 0.0F), Vec3(0.0F, 1.0F, 0.0F));
    Mat4 proj = Mat4::perspective(toRadians(60.0F), 1.0F, 0.1F, 100.0F);
    Mat4 viewProj = proj * view;

    auto cmdRes = device->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());

    cmd->begin();
    RenderStats stats = renderer.renderScene(world, assetManager, *device, *cmd, viewProj);
    // Copy rendered offscreen color target to readback buffer
    cmd->copyTextureToBuffer(renderer.getColorTarget(), readbackBuf);
    cmd->end();

    TEST_CHECK(stats.drawCalls >= 1);
    TEST_CHECK(stats.triangles >= 12);
    TEST_CHECK(stats.meshesRendered == 1);

    auto subRes = device->submit(cmd.get());
    TEST_CHECK(subRes.hasValue());
    device->waitIdle();

    // Read back rendered pixels from GPU
    std::vector<uint8_t> pixels(byteSize, 0);
    TEST_CHECK(device->readBuffer(readbackBuf, 0, pixels.data(), byteSize).hasValue());

    // 1. Verify clear color at corner (x = 2, y = 2)
    // Clear color is (0.08, 0.12, 0.20, 1.0) -> R: ~20, G: ~31, B: ~51, A: 255
    const size_t cornerIdx = (2 * vpWidth + 2) * 4;
    uint8_t cornerR = pixels[cornerIdx + 0];
    uint8_t cornerG = pixels[cornerIdx + 1];
    uint8_t cornerB = pixels[cornerIdx + 2];
    uint8_t cornerA = pixels[cornerIdx + 3];

    TEST_CHECK(cornerA == 255);
    TEST_CHECK(cornerR >= 16 && cornerR <= 26);
    TEST_CHECK(cornerG >= 26 && cornerG <= 36);
    TEST_CHECK(cornerB >= 45 && cornerB <= 57);

    // 2. Verify rendered geometry at center (x = 32, y = 32)
    // Cube front face is directly in front of camera with directional diffuse lighting.
    const size_t centerIdx = (32 * vpWidth + 32) * 4;
    uint8_t centerR = pixels[centerIdx + 0];
    uint8_t centerG = pixels[centerIdx + 1];
    uint8_t centerB = pixels[centerIdx + 2];
    uint8_t centerA = pixels[centerIdx + 3];

    TEST_CHECK(centerA == 255);
    // Center pixel must NOT be clear color (must be shaded geometry)
    bool isCenterDifferentFromClear =
        (std::abs(static_cast<int>(centerR) - static_cast<int>(cornerR)) > 15) ||
        (std::abs(static_cast<int>(centerG) - static_cast<int>(cornerG)) > 15) ||
        (std::abs(static_cast<int>(centerB) - static_cast<int>(cornerB)) > 15);
    TEST_CHECK(isCenterDifferentFromClear);

    // 3. Test Depth Occlusion with Distinguishable Colors (Finding 8B.2-01)
    // Add a second cube behind the first one at z = -2.0 with distinct Green color
    auto rearCube = ecs.entity("RearCube");
    rearCube.set<MeshComponent>(MeshComponent{
        .meshAsset = greenCubeId,
        .visible = true,
    });
    rearCube.set<physics::PhysicsTransformComponent>(physics::PhysicsTransformComponent{
        .transform = Transform(Vec3(0.0F, 0.0F, -2.0F)),
    });

    cmd->begin();
    stats = renderer.renderScene(world, assetManager, *device, *cmd, viewProj);
    cmd->copyTextureToBuffer(renderer.getColorTarget(), readbackBuf);
    cmd->end();

    TEST_CHECK(stats.drawCalls >= 2);
    TEST_CHECK(stats.meshesRendered == 2);

    TEST_CHECK(device->submit(cmd.get()).hasValue());
    device->waitIdle();

    std::vector<uint8_t> pixelsOccluded(byteSize, 0);
    TEST_CHECK(device->readBuffer(readbackBuf, 0, pixelsOccluded.data(), byteSize).hasValue());

    // Center pixel must show Front Red Cube, occluding the Rear Green Cube
    uint8_t occludedCenterR = pixelsOccluded[centerIdx + 0];
    uint8_t occludedCenterG = pixelsOccluded[centerIdx + 1];
    uint8_t occludedCenterB = pixelsOccluded[centerIdx + 2];

    // Front cube is Red, so R must dominate and match front cube, G and B must be low
    TEST_CHECK(occludedCenterR >= 80);
    TEST_CHECK(occludedCenterG <= 40);
    TEST_CHECK(occludedCenterB <= 40);
    TEST_CHECK(std::abs(static_cast<int>(occludedCenterR) - static_cast<int>(centerR)) <= 4);

    // Now hide front Red cube and verify Rear Green Cube is revealed
    cubeEntity.set<MeshComponent>(MeshComponent{
        .meshAsset = redCubeId,
        .visible = false,
    });

    cmd->begin();
    stats = renderer.renderScene(world, assetManager, *device, *cmd, viewProj);
    cmd->copyTextureToBuffer(renderer.getColorTarget(), readbackBuf);
    cmd->end();

    TEST_CHECK(stats.drawCalls >= 1);
    TEST_CHECK(device->submit(cmd.get()).hasValue());
    device->waitIdle();

    std::vector<uint8_t> pixelsRevealed(byteSize, 0);
    TEST_CHECK(device->readBuffer(readbackBuf, 0, pixelsRevealed.data(), byteSize).hasValue());

    uint8_t revealedCenterR = pixelsRevealed[centerIdx + 0];
    uint8_t revealedCenterG = pixelsRevealed[centerIdx + 1];
    uint8_t revealedCenterB = pixelsRevealed[centerIdx + 2];

    // Rear Green cube is now visible: G must dominate, R and B must be low
    TEST_CHECK(revealedCenterG >= 80);
    TEST_CHECK(revealedCenterR <= 40);
    TEST_CHECK(revealedCenterB <= 40);

    // 4. Test Vulkan Zero-Size Handling & Repeated Resize (Finding 8B.2-03)
    TEST_CHECK(!renderer.resize(0, 128).hasValue());
    TEST_CHECK(!renderer.resize(128, 0).hasValue());
    TEST_CHECK(!renderer.resize(0, 0).hasValue());
    TEST_CHECK(renderer.getWidth() == vpWidth);
    TEST_CHECK(renderer.getHeight() == vpHeight);

    // Repeated valid resizing
    TEST_CHECK(renderer.resize(128, 128).hasValue());
    TEST_CHECK(renderer.getWidth() == 128);
    TEST_CHECK(renderer.getHeight() == 128);

    TEST_CHECK(renderer.resize(256, 128).hasValue());
    TEST_CHECK(renderer.getWidth() == 256);
    TEST_CHECK(renderer.getHeight() == 128);

    TEST_CHECK(renderer.resize(64, 64).hasValue());
    TEST_CHECK(renderer.getWidth() == 64);
    TEST_CHECK(renderer.getHeight() == 64);

    renderer.shutdown();
    device->destroyBuffer(readbackBuf);

    std::cout << "Vulkan SceneRenderer hardware rendering & pixel readback verified!\n";
    return 0;
}

int main() {
    std::cout << "Running SceneRenderer Tests...\n";

    if (testSceneRendererLifecycleWithNullDevice() != 0) {
        return 1;
    }
    if (testSceneRendererEmptyScene() != 0) {
        return 1;
    }
    if (testSceneRendererEntityFilteringAndVisibility() != 0) {
        return 1;
    }
    if (testSceneRendererMultipleEntities() != 0) {
        return 1;
    }
    if (testVulkanSceneRendererHardwareRenderingAndPixelReadback() != 0) {
        return 1;
    }

    std::cout << "All SceneRenderer Tests Passed!\n";
    return 0;
}
