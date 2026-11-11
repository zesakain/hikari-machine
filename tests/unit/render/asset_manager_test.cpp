/// @file asset_manager_test.cpp
/// @brief Unit tests for AssetManager caching, MeshComponent ECS integration, serialization, and
/// scene rendering.

#include "framework/scene_serializer.h"
#include "physics/physics_components.h"
#include "render/asset_manager.h"
#include "render/mesh_component.h"
#include "render/scene_renderer.h"
#include "rhi/rhi_device.h"
#include "world/world.h"

#include <iostream>

using namespace eng;
using namespace eng::render;

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << '\n'; \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

static int testAssetManagerLoadingAndDeduplication() {
    AssetManager manager;

    auto idRes1 = manager.loadMesh("assets/models/cube.obj");
    TEST_CHECK(idRes1.hasValue());
    const AssetId id1 = idRes1.value();

    auto idRes2 = manager.loadMesh("assets/models/cube.obj");
    TEST_CHECK(idRes2.hasValue());
    const AssetId id2 = idRes2.value();

    TEST_CHECK(id1 == id2);
    TEST_CHECK(manager.isLoaded(id1));

    const MeshData* md = manager.getMeshData(id1);
    TEST_CHECK(md != nullptr);
    TEST_CHECK(md->getTriangleCount() == 12);

    const MeshData* mdByPath = manager.getMeshData("assets/models/cube.obj");
    TEST_CHECK(mdByPath == md);

    const AssetMetadata* meta = manager.getMetadata(id1);
    TEST_CHECK(meta != nullptr);
    TEST_CHECK(meta->type == AssetType::Mesh);
    TEST_CHECK(meta->loadState == AssetLoadState::Loaded);

    return 0;
}

static int testAssetManagerGpuUploadAndUnload() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto& device = *devRes.value();

    AssetManager manager;
    auto idRes = manager.loadMesh("assets/models/prism.obj");
    TEST_CHECK(idRes.hasValue());
    const AssetId id = idRes.value();

    TEST_CHECK(manager.getGpuMesh(id) == nullptr);

    auto uploadRes = manager.uploadMesh(id, device);
    TEST_CHECK(uploadRes.hasValue());
    GpuMesh* gpuMesh = uploadRes.value();
    TEST_CHECK(gpuMesh != nullptr);
    TEST_CHECK(gpuMesh->isValid());
    TEST_CHECK(manager.getGpuMesh(id) == gpuMesh);

    // Unload
    manager.unload(id);
    TEST_CHECK(!manager.isLoaded(id));
    TEST_CHECK(manager.getMeshData(id) == nullptr);
    TEST_CHECK(manager.getGpuMesh(id) == nullptr);

    return 0;
}

static int testAssetManagerTextureLoadingAndDeduplication() {
    AssetManager manager;

    ImageData img{
        .width = 8,
        .height = 8,
        .channels = 4,
        .format = rhi::TextureFormat::RGBA8_UNORM,
        .pixels = std::vector<uint8_t>(8 * 8 * 4, 128),
    };

    auto regRes1 = manager.registerTexture("textures/test_diffuse.png", std::move(img));
    TEST_CHECK(regRes1.hasValue());
    const AssetId id1 = regRes1.value();

    TEST_CHECK(manager.isLoaded(id1));
    const ImageData* data = manager.getImageData(id1);
    TEST_CHECK(data != nullptr);
    TEST_CHECK(data->width == 8);
    TEST_CHECK(data->height == 8);
    TEST_CHECK(data->channels == 4);

    const ImageData* dataByPath = manager.getImageData("textures/test_diffuse.png");
    TEST_CHECK(dataByPath == data);

    const AssetMetadata* meta = manager.getMetadata(id1);
    TEST_CHECK(meta != nullptr);
    TEST_CHECK(meta->type == AssetType::Texture);
    TEST_CHECK(meta->loadState == AssetLoadState::Loaded);

    // Invalid file path returns error
    auto failRes = manager.loadTexture("assets/textures/non_existent.png");
    TEST_CHECK(!failRes.hasValue());

    return 0;
}

static int testAssetManagerTextureGpuUploadAndUnload() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto& device = *devRes.value();

    AssetManager manager;

    ImageData img{
        .width = 4,
        .height = 4,
        .channels = 4,
        .format = rhi::TextureFormat::RGBA8_UNORM,
        .pixels = std::vector<uint8_t>(4 * 4 * 4, 200),
    };

    auto regRes = manager.registerTexture("textures/brick.png", std::move(img));
    TEST_CHECK(regRes.hasValue());
    const AssetId id = regRes.value();

    TEST_CHECK(manager.getGpuTexture(id) == nullptr);

    auto uploadRes = manager.uploadTexture(id, device, true);
    TEST_CHECK(uploadRes.hasValue());
    GpuTexture* gpuTex = uploadRes.value();
    TEST_CHECK(gpuTex != nullptr);
    TEST_CHECK(gpuTex->isValid());
    TEST_CHECK(gpuTex->getWidth() == 4);
    TEST_CHECK(gpuTex->getHeight() == 4);
    TEST_CHECK(manager.getGpuTexture(id) == gpuTex);

    // Calling uploadTexture again returns the cached GpuTexture
    auto uploadRes2 = manager.uploadTexture(id, device);
    TEST_CHECK(uploadRes2.hasValue());
    TEST_CHECK(uploadRes2.value() == gpuTex);

    // Unload
    manager.unload(id);
    TEST_CHECK(!manager.isLoaded(id));
    TEST_CHECK(manager.getImageData(id) == nullptr);
    TEST_CHECK(manager.getGpuTexture(id) == nullptr);

    return 0;
}

static int testMeshComponentECSAndSerialization() {
    world::World world;
    physics::registerPhysicsComponents(world);
    registerRenderComponents(world);

    // Create an entity with Transform and MeshComponent
    auto handle = world.createEntity("TestMeshEntity");
    world.setComponent(handle, physics::PhysicsTransformComponent{
                                   .transform = Transform(Vec3(1.0F, 2.0F, 3.0F)),
                               });
    const AssetId meshId = makeAssetIdFromPath("assets/models/cube.obj");
    world.setComponent(handle, MeshComponent{
                                   .meshAsset = meshId,
                                   .assetPath = "assets/models/cube.obj",
                                   .submeshIndex = 0,
                                   .visible = true,
                                   .castShadows = true,
                               });

    TEST_CHECK(world.hasComponent<MeshComponent>(handle));
    const auto* mc = world.getComponent<MeshComponent>(handle);
    TEST_CHECK(mc != nullptr);
    TEST_CHECK(mc->assetPath == "assets/models/cube.obj");
    TEST_CHECK(mc->meshAsset == meshId);

    // Serialize to string
    framework::SceneMetadata meta{.name = "MeshTestScene"};
    auto serRes = framework::SceneSerializer::serializeToString(world, meta);
    TEST_CHECK(serRes.hasValue());
    const std::string jsonText = serRes.value();

    // Verify "Mesh" appears in json
    TEST_CHECK(jsonText.find("\"Mesh\"") != std::string::npos);
    TEST_CHECK(jsonText.find("assets/models/cube.obj") != std::string::npos);

    // Deserialize into a fresh world
    world::World newWorld;
    physics::registerPhysicsComponents(newWorld);
    registerRenderComponents(newWorld);

    auto desRes = framework::SceneSerializer::deserializeFromString(newWorld, jsonText, true);
    TEST_CHECK(desRes.hasValue());
    TEST_CHECK(desRes.value().entitiesLoaded == 1);

    auto entities = newWorld.getAllEntities();
    TEST_CHECK(entities.size() == 1);
    auto h2 = entities[0];
    TEST_CHECK(newWorld.hasComponent<MeshComponent>(h2));
    const auto* mc2 = newWorld.getComponent<MeshComponent>(h2);
    TEST_CHECK(mc2 != nullptr);
    TEST_CHECK(mc2->assetPath == "assets/models/cube.obj");
    TEST_CHECK(mc2->meshAsset == meshId);
    TEST_CHECK(mc2->visible == true);

    return 0;
}

static int testSceneRendererCommandRecording() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto& device = *devRes.value();

    auto cmdRes = device.createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto& cmdList = *cmdRes.value();

    world::World world;
    physics::registerPhysicsComponents(world);
    registerRenderComponents(world);

    AssetManager manager;
    auto loadRes = manager.loadMesh("assets/models/cube.obj");
    TEST_CHECK(loadRes.hasValue());

    // Spawn 2 entities with mesh components
    auto h1 = world.createEntity("Mesh1");
    world.setComponent(h1, physics::PhysicsTransformComponent{
                               .transform = Transform(Vec3(-2.0F, 0.0F, 0.0F)),
                           });
    world.setComponent(h1, MeshComponent{
                               .meshAsset = loadRes.value(),
                               .assetPath = "assets/models/cube.obj",
                               .visible = true,
                           });

    auto h2 = world.createEntity("Mesh2");
    world.setComponent(h2, physics::PhysicsTransformComponent{
                               .transform = Transform(Vec3(2.0F, 0.0F, 0.0F)),
                           });
    world.setComponent(h2, MeshComponent{
                               .meshAsset = loadRes.value(),
                               .assetPath = "assets/models/cube.obj",
                               .visible = true,
                           });

    // Hidden entity should be skipped
    auto h3 = world.createEntity("MeshHidden");
    world.setComponent(h3, physics::PhysicsTransformComponent{});
    world.setComponent(h3, MeshComponent{
                               .meshAsset = loadRes.value(),
                               .assetPath = "assets/models/cube.obj",
                               .visible = false,
                           });

    cmdList.begin();
    SceneRenderer renderer;
    RenderStats stats = renderer.renderScene(world, manager, device, cmdList);
    cmdList.end();

    TEST_CHECK(stats.meshesRendered == 2);
    TEST_CHECK(stats.drawCalls == 2);
    TEST_CHECK(stats.triangles == 24); // 12 * 2

    return 0;
}

int main() {
    std::cout << "[Running AssetManager & Render Integration Tests]\n";
    if (testAssetManagerLoadingAndDeduplication() != 0)
        return 1;
    if (testAssetManagerGpuUploadAndUnload() != 0)
        return 2;
    if (testAssetManagerTextureLoadingAndDeduplication() != 0)
        return 3;
    if (testAssetManagerTextureGpuUploadAndUnload() != 0)
        return 4;
    if (testMeshComponentECSAndSerialization() != 0)
        return 5;
    if (testSceneRendererCommandRecording() != 0)
        return 6;
    std::cout << "[AssetManager & Render Tests Passed: 6/6]\n";
    return 0;
}
