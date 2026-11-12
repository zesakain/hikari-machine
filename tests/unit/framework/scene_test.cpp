/// @file scene_test.cpp
/// @brief Comprehensive regression test suite for native .hikari scene serialization &
/// transactional deserialization.

#include "core/log.h"
#include "framework/scene.h"
#include "framework/scene_serializer.h"
#include "physics/physics_components.h"
#include "render/camera_component.h"
#include "render/light_components.h"
#include "render/post_process_types.h"
#include "script/script_components.h"
#include "world/world.h"

#include <cmath>
#include <filesystem>
#include <iostream>

namespace {

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << '\n'; \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

using namespace eng;
using namespace eng::framework;
using namespace eng::world;

int testEmptySceneRoundTrip() {
    World worldA;
    SceneMetadata meta{.name = "EmptyLevel", .generator = "Hikari Test", .timestamp = 12345};

    auto serRes = SceneSerializer::serializeToString(worldA, meta);
    TEST_CHECK(serRes.hasValue());
    std::string json = serRes.value();
    TEST_CHECK(json.find("\"formatVersion\": 1") != std::string::npos);
    TEST_CHECK(json.find("\"name\": \"EmptyLevel\"") != std::string::npos);

    World worldB;
    // Populate worldB with dummy entity to ensure it gets cleared
    auto dummy = worldB.createEntity("Dummy");
    (void)dummy;
    TEST_CHECK(worldB.getAllEntities().size() == 1);

    auto desRes = SceneSerializer::deserializeFromString(worldB, json, true);
    TEST_CHECK(desRes.hasValue());
    TEST_CHECK(desRes.value().entitiesLoaded == 0);
    TEST_CHECK(worldB.getAllEntities().empty());

    return 0;
}

int testSingleEntityWithAllComponentsRoundTrip() {
    World worldA;
    EntityHandle e1 = worldA.createEntity("PlayerKnight");
    worldA.setComponent(e1, StableIdComponent{1001});

    worldA.setComponent(e1,
                        physics::PhysicsTransformComponent{
                            .transform = Transform(Vec3(1.0F, 2.5F, -4.0F),
                                                   Quat(0.0F, 0.7071F, 0.0F, 0.7071F).normalized(),
                                                   Vec3(1.2F, 1.2F, 1.2F)),
                        });

    worldA.setComponent(e1, physics::RigidBodyComponent{
                                .bodyHandle = {},
                                .motionType = physics::MotionType::Kinematic,
                                .collisionLayer = physics::CollisionLayer::kPlayer,
                            });

    worldA.setComponent(
        e1, physics::ColliderComponent{
                .desc = physics::ColliderDesc::makeCapsule(0.4F, 0.9F, Vec3(0, 0.9F, 0)),
            });

    script::ScriptComponent sc{};
    std::snprintf(sc.scriptName, sizeof(sc.scriptName), "player_controller");
    sc.enabled = true;
    sc.updateInterval = 0.02F;
    worldA.setComponent(e1, sc);

    // Serialize
    SceneMetadata meta{.name = "HeroScene", .generator = "Hikari Test", .timestamp = 99999};
    auto serRes = SceneSerializer::serializeToString(worldA, meta);
    TEST_CHECK(serRes.hasValue());

    // Deserialize into World B
    World worldB;
    auto desRes = SceneSerializer::deserializeFromString(worldB, serRes.value());
    TEST_CHECK(desRes.hasValue());
    TEST_CHECK(desRes.value().entitiesLoaded == 1);

    auto entitiesB = worldB.getAllEntities();
    TEST_CHECK(entitiesB.size() == 1);
    EntityHandle hB = entitiesB[0];

    // Verify Name
    TEST_CHECK(worldB.getEntityName(hB) == "PlayerKnight");

    // Verify Stable ID
    const auto* stableB = worldB.getComponent<StableIdComponent>(hB);
    TEST_CHECK(stableB != nullptr);
    TEST_CHECK(stableB->id == 1001);

    // Verify Transform
    const auto* ptB = worldB.getComponent<physics::PhysicsTransformComponent>(hB);
    TEST_CHECK(ptB != nullptr);
    TEST_CHECK(std::abs(ptB->transform.position.x - 1.0F) < 1e-4F);
    TEST_CHECK(std::abs(ptB->transform.position.y - 2.5F) < 1e-4F);
    TEST_CHECK(std::abs(ptB->transform.position.z - (-4.0F)) < 1e-4F);
    TEST_CHECK(std::abs(ptB->transform.scale.x - 1.2F) < 1e-4F);

    // Verify RigidBody
    const auto* rbB = worldB.getComponent<physics::RigidBodyComponent>(hB);
    TEST_CHECK(rbB != nullptr);
    TEST_CHECK(rbB->motionType == physics::MotionType::Kinematic);
    TEST_CHECK(rbB->collisionLayer == physics::CollisionLayer::kPlayer);
    TEST_CHECK(!rbB->bodyHandle.isValid()); // Runtime-only handle must be unassigned

    // Verify Collider
    const auto* colB = worldB.getComponent<physics::ColliderComponent>(hB);
    TEST_CHECK(colB != nullptr);
    TEST_CHECK(colB->desc.type == physics::ColliderType::Capsule);
    TEST_CHECK(std::abs(colB->desc.radius - 0.4F) < 1e-4F);
    TEST_CHECK(std::abs(colB->desc.halfHeight - 0.9F) < 1e-4F);
    TEST_CHECK(std::abs(colB->desc.offset.y - 0.9F) < 1e-4F);

    // Verify Script
    const auto* scB = worldB.getComponent<script::ScriptComponent>(hB);
    TEST_CHECK(scB != nullptr);
    TEST_CHECK(std::string_view(scB->scriptName) == "player_controller");
    TEST_CHECK(scB->enabled == true);
    TEST_CHECK(std::abs(scB->updateInterval - 0.02F) < 1e-4F);

    return 0;
}

int testMultipleEntitiesAndPreservation() {
    World worldA;

    for (int i = 1; i <= 5; ++i) {
        char nameBuf[32];
        std::snprintf(nameBuf, sizeof(nameBuf), "BoxObstacle_%d", i);
        auto h = worldA.createEntity(nameBuf);
        worldA.setComponent(h, StableIdComponent{static_cast<uint64_t>(i * 10)});
        worldA.setComponent(h, physics::PhysicsTransformComponent{
                                   .transform = Transform(Vec3(static_cast<float>(i) * 2.0F, 0, 0)),
                               });
    }

    auto serRes = SceneSerializer::serializeToString(worldA);
    TEST_CHECK(serRes.hasValue());

    World worldB;
    auto desRes = SceneSerializer::deserializeFromString(worldB, serRes.value());
    TEST_CHECK(desRes.hasValue());
    TEST_CHECK(desRes.value().entitiesLoaded == 5);

    auto entsB = worldB.getAllEntities();
    TEST_CHECK(entsB.size() == 5);

    for (int i = 1; i <= 5; ++i) {
        char expectedName[32];
        std::snprintf(expectedName, sizeof(expectedName), "BoxObstacle_%d", i);

        bool found = false;
        for (auto h : entsB) {
            if (worldB.getEntityName(h) == expectedName) {
                found = true;
                const auto* s = worldB.getComponent<StableIdComponent>(h);
                TEST_CHECK(s != nullptr && s->id == static_cast<uint64_t>(i * 10));
                const auto* pt = worldB.getComponent<physics::PhysicsTransformComponent>(h);
                TEST_CHECK(pt != nullptr && std::abs(pt->transform.position.x -
                                                     static_cast<float>(i) * 2.0F) < 1e-4F);
                break;
            }
        }
        TEST_CHECK(found);
    }

    return 0;
}

int testTransactionalSafetyOnInvalidScene() {
    World world;
    auto orig1 = world.createEntity("Original_1");
    auto orig2 = world.createEntity("Original_2");
    world.setComponent(orig1, StableIdComponent{10});
    world.setComponent(orig2, StableIdComponent{20});

    TEST_CHECK(world.getAllEntities().size() == 2);

    // 1. Corrupted JSON syntax
    auto res1 = SceneSerializer::deserializeFromString(world, "{ broken json syntax [}");
    TEST_CHECK(!res1.hasValue());
    TEST_CHECK(world.getAllEntities().size() == 2);
    TEST_CHECK(world.isAlive(orig1));
    TEST_CHECK(world.isAlive(orig2));

    // 2. Unsupported formatVersion
    std::string unsuppVer = R"({
        "formatVersion": 99,
        "metadata": { "name": "Future" },
        "entities": []
    })";
    auto res2 = SceneSerializer::deserializeFromString(world, unsuppVer);
    TEST_CHECK(!res2.hasValue());
    TEST_CHECK(world.getAllEntities().size() == 2);
    TEST_CHECK(world.isAlive(orig1));
    TEST_CHECK(world.isAlive(orig2));

    // 3. Duplicate stable IDs in file
    std::string dupIds = R"({
        "formatVersion": 1,
        "metadata": { "name": "DupTest" },
        "entities": [
            { "id": 5, "name": "EntityA" },
            { "id": 5, "name": "EntityB" }
        ]
    })";
    auto res3 = SceneSerializer::deserializeFromString(world, dupIds);
    TEST_CHECK(!res3.hasValue());
    TEST_CHECK(world.getAllEntities().size() == 2);
    TEST_CHECK(world.isAlive(orig1));
    TEST_CHECK(world.isAlive(orig2));

    // 4. Invalid Transform scale (negative)
    std::string negScale = R"({
        "formatVersion": 1,
        "metadata": { "name": "ScaleTest" },
        "entities": [
            {
                "id": 1,
                "name": "InvalidScaleEntity",
                "components": {
                    "Transform": {
                        "position": [0, 0, 0],
                        "rotation": [0, 0, 0, 1],
                        "scale": [1, -0.5, 1]
                    }
                }
            }
        ]
    })";
    auto res4 = SceneSerializer::deserializeFromString(world, negScale);
    TEST_CHECK(!res4.hasValue());
    TEST_CHECK(world.getAllEntities().size() == 2);
    TEST_CHECK(world.isAlive(orig1));
    TEST_CHECK(world.isAlive(orig2));

    return 0;
}

int testAtomicFileSaveAndLoad() {
    std::filesystem::path testDir = "tests_temp_scene";
    std::filesystem::create_directories(testDir);
    std::filesystem::path scenePath = testDir / "test_level.hikari";

    // Clean any leftover file
    std::filesystem::remove(scenePath);

    World worldA;
    auto e = worldA.createEntity("SavedEntity");
    worldA.setComponent(e, StableIdComponent{777});
    worldA.setComponent(e, physics::PhysicsTransformComponent{
                               .transform = Transform(Vec3(3.0F, 7.0F, 11.0F)),
                           });

    SceneMetadata meta{.name = "SavedLevel", .generator = "Hikari Test", .timestamp = 54321};
    auto saveRes = SceneSerializer::serializeToFile(worldA, scenePath, meta);
    TEST_CHECK(saveRes.hasValue());
    TEST_CHECK(std::filesystem::exists(scenePath));

    World worldB;
    auto loadRes = SceneSerializer::deserializeFromFile(worldB, scenePath);
    TEST_CHECK(loadRes.hasValue());
    TEST_CHECK(loadRes.value().entitiesLoaded == 1);

    auto loadedEntities = worldB.getAllEntities();
    TEST_CHECK(loadedEntities.size() == 1);
    TEST_CHECK(worldB.getEntityName(loadedEntities[0]) == "SavedEntity");

    const auto* pt = worldB.getComponent<physics::PhysicsTransformComponent>(loadedEntities[0]);
    TEST_CHECK(pt != nullptr);
    TEST_CHECK(std::abs(pt->transform.position.y - 7.0F) < 1e-4F);

    // Clean up
    std::filesystem::remove_all(testDir);
    return 0;
}

int testUnknownComponentWarning() {
    std::string jsonWithUnknownComp = R"({
        "formatVersion": 1,
        "metadata": { "name": "UnknownTest" },
        "entities": [
            {
                "id": 12,
                "name": "EntityWithUnknown",
                "components": {
                    "Transform": {
                        "position": [0, 1, 0],
                        "rotation": [0, 0, 0, 1],
                        "scale": [1, 1, 1]
                    },
                    "FutureAIComponent": {
                        "behaviorTree": "boss_phase_1.bt"
                    }
                }
            }
        ]
    })";

    World world;
    auto res = SceneSerializer::deserializeFromString(world, jsonWithUnknownComp);
    TEST_CHECK(res.hasValue());
    TEST_CHECK(res.value().entitiesLoaded == 1);
    TEST_CHECK(res.value().warnings.size() == 1);
    TEST_CHECK(res.value().warnings[0].find("FutureAIComponent") != std::string::npos);

    auto ents = world.getAllEntities();
    TEST_CHECK(ents.size() == 1);
    TEST_CHECK(world.hasComponent<physics::PhysicsTransformComponent>(ents[0]));

    return 0;
}

int testSampleSceneFileLoads() {
#if defined(HIKARI_SAMPLE_SCENE_PATH)
    std::filesystem::path samplePath = HIKARI_SAMPLE_SCENE_PATH;
#else
    std::filesystem::path samplePath = "assets/scenes/sample.hikari";
#endif

    TEST_CHECK(std::filesystem::exists(samplePath));

    World world;
    auto res = SceneSerializer::deserializeFromFile(world, samplePath);
    TEST_CHECK(res.hasValue());
    TEST_CHECK(res.value().entitiesLoaded == 4);
    TEST_CHECK(res.value().warnings.empty());

    auto ents = world.getAllEntities();
    TEST_CHECK(ents.size() == 4);

    return 0;
}

int testLightAndCameraSerializationRoundTrip() {
    World worldA;

    // 1. Directional Sun
    EntityHandle sun = worldA.createEntity("Sunlight");
    render::DirectionalLightComponent dl{};
    dl.direction = Vec3(0.4F, 0.85F, 0.3F).normalized();
    dl.color = Vec3(1.0F, 0.96F, 0.90F);
    dl.intensity = 3.5F;
    dl.castShadows = true;
    dl.shadowBias = 0.0025F;
    dl.enabled = true;
    worldA.setComponent(sun, dl);

    // 2. Point Light
    EntityHandle lamp = worldA.createEntity("TorchLamp");
    render::PointLightComponent pl{};
    pl.position = Vec3(2.0F, 1.5F, -3.0F);
    pl.color = Vec3(1.0F, 0.7F, 0.3F);
    pl.intensity = 12.0F;
    pl.range = 8.5F;
    pl.enabled = true;
    worldA.setComponent(lamp, pl);

    // 3. Spot Light
    EntityHandle spot = worldA.createEntity("WallSpot");
    render::SpotLightComponent sl{};
    sl.position = Vec3(-1.0F, 4.0F, 0.0F);
    sl.direction = Vec3(0.0F, -1.0F, 0.0F);
    sl.color = Vec3(0.8F, 0.9F, 1.0F);
    sl.intensity = 20.0F;
    sl.range = 15.0F;
    sl.innerConeAngle = 0.35F;
    sl.outerConeAngle = 0.55F;
    sl.castShadows = true;
    sl.enabled = true;
    worldA.setComponent(spot, sl);

    // 4. Primary Scene Camera
    EntityHandle cam = worldA.createEntity("MainCamera");
    worldA.setComponent(cam, TransformComponent{Transform(Vec3(0.0F, 2.0F, 5.0F))});
    render::CameraComponent cc{};
    cc.fovDegrees = 65.0F;
    cc.nearPlane = 0.05F;
    cc.farPlane = 2000.0F;
    cc.isPrimary = true;
    worldA.setComponent(cam, cc);

    // Post process settings
    render::PostProcessSettings ppSettings{};
    ppSettings.enabled = true;
    ppSettings.exposure = 1.25F;
    ppSettings.toneMapOp = render::ToneMapOperator::AcesFitted;
    ppSettings.bloomEnabled = true;
    ppSettings.bloomThreshold = 0.95F;
    ppSettings.bloomIntensity = 0.05F;
    ppSettings.bloomKnee = 0.45F;
    ppSettings.reinhardWhitePoint = 3.8F;

    SceneMetadata meta{.name = "BenchmarkTestScene", .generator = "Hikari 10G", .timestamp = 55555};
    Vec3 camPos(0.0F, 2.0F, 5.0F);
    Vec3 camTgt(0.0F, 1.5F, 0.0F);

    auto serRes = SceneSerializer::serializeToString(worldA, meta, {}, ppSettings, camPos, camTgt);
    TEST_CHECK(serRes.hasValue());
    const std::string jsonText = serRes.value();

    TEST_CHECK(jsonText.find("\"DirectionalLight\"") != std::string::npos);
    TEST_CHECK(jsonText.find("\"PointLight\"") != std::string::npos);
    TEST_CHECK(jsonText.find("\"SpotLight\"") != std::string::npos);
    TEST_CHECK(jsonText.find("\"Camera\"") != std::string::npos);
    TEST_CHECK(jsonText.find("\"settings\"") != std::string::npos);
    TEST_CHECK(jsonText.find("\"postProcess\"") != std::string::npos);
    TEST_CHECK(jsonText.find("\"ACES\"") != std::string::npos);

    // Deserialize into fresh World B
    World worldB;
    auto desRes = SceneSerializer::deserializeFromString(worldB, jsonText, true);
    TEST_CHECK(desRes.hasValue());
    const auto& res = desRes.value();
    TEST_CHECK(res.entitiesLoaded == 4);
    TEST_CHECK(res.metadata.name == "BenchmarkTestScene");
    TEST_CHECK(res.postProcessSettings.has_value());
    TEST_CHECK(res.postProcessSettings->enabled == true);
    TEST_CHECK(std::abs(res.postProcessSettings->exposure - 1.25F) < 1e-4F);
    TEST_CHECK(res.postProcessSettings->toneMapOp == render::ToneMapOperator::AcesFitted);
    TEST_CHECK(res.postProcessSettings->bloomEnabled == true);
    TEST_CHECK(res.cameraPosition.has_value());
    TEST_CHECK(std::abs(res.cameraPosition->z - 5.0F) < 1e-4F);
    TEST_CHECK(res.cameraTarget.has_value());
    TEST_CHECK(std::abs(res.cameraTarget->y - 1.5F) < 1e-4F);

    // Verify entity components in World B
    auto entsB = worldB.getAllEntities();
    TEST_CHECK(entsB.size() == 4);

    bool foundSun = false;
    bool foundLamp = false;
    bool foundSpot = false;
    bool foundCam = false;

    for (auto h : entsB) {
        if (worldB.getEntityName(h) == "Sunlight") {
            const auto* dlc = worldB.getComponent<render::DirectionalLightComponent>(h);
            TEST_CHECK(dlc != nullptr);
            TEST_CHECK(dlc->castShadows == true);
            TEST_CHECK(std::abs(dlc->intensity - 3.5F) < 1e-4F);
            TEST_CHECK(std::abs(dlc->shadowBias - 0.0025F) < 1e-4F);
            foundSun = true;
        } else if (worldB.getEntityName(h) == "TorchLamp") {
            const auto* plc = worldB.getComponent<render::PointLightComponent>(h);
            TEST_CHECK(plc != nullptr);
            TEST_CHECK(std::abs(plc->intensity - 12.0F) < 1e-4F);
            TEST_CHECK(std::abs(plc->range - 8.5F) < 1e-4F);
            foundLamp = true;
        } else if (worldB.getEntityName(h) == "WallSpot") {
            const auto* slc = worldB.getComponent<render::SpotLightComponent>(h);
            TEST_CHECK(slc != nullptr);
            TEST_CHECK(slc->castShadows == true);
            TEST_CHECK(std::abs(slc->intensity - 20.0F) < 1e-4F);
            TEST_CHECK(std::abs(slc->range - 15.0F) < 1e-4F);
            foundSpot = true;
        } else if (worldB.getEntityName(h) == "MainCamera") {
            const auto* ccB = worldB.getComponent<render::CameraComponent>(h);
            TEST_CHECK(ccB != nullptr);
            TEST_CHECK(ccB->isPrimary == true);
            TEST_CHECK(std::abs(ccB->fovDegrees - 65.0F) < 1e-4F);
            TEST_CHECK(std::abs(ccB->nearPlane - 0.05F) < 1e-4F);
            TEST_CHECK(std::abs(ccB->farPlane - 2000.0F) < 1e-4F);
            foundCam = true;
        }
    }

    TEST_CHECK(foundSun && foundLamp && foundSpot && foundCam);

    return 0;
}

} // namespace

int main() {
    if (testEmptySceneRoundTrip() != 0)
        return 1;
    if (testSingleEntityWithAllComponentsRoundTrip() != 0)
        return 1;
    if (testMultipleEntitiesAndPreservation() != 0)
        return 1;
    if (testTransactionalSafetyOnInvalidScene() != 0)
        return 1;
    if (testAtomicFileSaveAndLoad() != 0)
        return 1;
    if (testUnknownComponentWarning() != 0)
        return 1;
    if (testSampleSceneFileLoads() != 0)
        return 1;
    if (testLightAndCameraSerializationRoundTrip() != 0)
        return 1;

    std::cout << "All SceneSerializer unit tests passed successfully.\n";
    return 0;
}
