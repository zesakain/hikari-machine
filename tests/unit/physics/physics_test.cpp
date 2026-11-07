/// @file physics_test.cpp
/// @brief Comprehensive unit tests for Jolt Physics integration and ECS synchronization.

#include "physics/physics_components.h"
#include "physics/physics_system.h"
#include "physics/physics_world.h"
#include "world/world.h"

#include <cmath>
#include <iostream>

using namespace eng;
using namespace eng::physics;

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << '\n'; \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

static int testPhysicsSystemLifecycle() {
    TEST_CHECK(PhysicsSystem::initialize().hasValue());
    TEST_CHECK(PhysicsSystem::isInitialized());
    // Idempotent init
    TEST_CHECK(PhysicsSystem::initialize().hasValue());
    TEST_CHECK(PhysicsSystem::isInitialized());
    return 0;
}

static int testBodyCreationAndHandles() {
    PhysicsWorldSettings settings{};
    settings.gravity = Vec3(0.0F, -9.81F, 0.0F);
    PhysicsWorld world(settings);

    // Create a static ground box
    BodyDesc groundDesc{};
    groundDesc.motionType = MotionType::Static;
    groundDesc.collisionLayer = CollisionLayer::kNonMoving;
    groundDesc.position = Vec3(0.0F, -0.5F, 0.0F);
    groundDesc.collider = ColliderDesc::makeBox(Vec3(10.0F, 1.0F, 10.0F));

    auto groundResult = world.createBody(groundDesc);
    TEST_CHECK(groundResult.hasValue());
    BodyHandle groundHandle = groundResult.value();
    TEST_CHECK(groundHandle.isValid());
    TEST_CHECK(world.isBodyValid(groundHandle));

    // Create a dynamic falling sphere
    BodyDesc sphereDesc{};
    sphereDesc.motionType = MotionType::Dynamic;
    sphereDesc.collisionLayer = CollisionLayer::kMoving;
    sphereDesc.position = Vec3(0.0F, 5.0F, 0.0F);
    sphereDesc.collider = ColliderDesc::makeSphere(0.5F);

    auto sphereResult = world.createBody(sphereDesc);
    TEST_CHECK(sphereResult.hasValue());
    BodyHandle sphereHandle = sphereResult.value();
    TEST_CHECK(sphereHandle.isValid());
    TEST_CHECK(world.isBodyValid(sphereHandle));
    TEST_CHECK(world.getBodyCount() == 2);

    // Verify initial positions
    auto posRes = world.getPosition(sphereHandle);
    TEST_CHECK(posRes.hasValue());
    TEST_CHECK(std::abs(posRes.value().y - 5.0F) < 1e-3F);

    // Destroy sphere and verify handle invalidation
    auto destroyRes = world.destroyBody(sphereHandle);
    TEST_CHECK(destroyRes.hasValue());
    TEST_CHECK(!world.isBodyValid(sphereHandle));
    TEST_CHECK(world.getBodyCount() == 1);

    // Calling operations on dead handle fails with HandleResolutionFailed
    auto deadPos = world.getPosition(sphereHandle);
    TEST_CHECK(!deadPos.hasValue());
    TEST_CHECK(deadPos.error().code == ErrorCode::HandleResolutionFailed);

    // Double destroy fails
    auto doubleDestroy = world.destroyBody(sphereHandle);
    TEST_CHECK(!doubleDestroy.hasValue());
    TEST_CHECK(doubleDestroy.error().code == ErrorCode::HandleResolutionFailed);

    return 0;
}

static int testKinematicAndDynamicProperties() {
    PhysicsWorld world;

    BodyDesc desc{};
    desc.motionType = MotionType::Kinematic;
    desc.collisionLayer = CollisionLayer::kMoving;
    desc.position = Vec3(1.0F, 2.0F, 3.0F);
    desc.collider = ColliderDesc::makeCapsule(0.5F, 1.0F);

    auto handle = world.createBody(desc).value();
    TEST_CHECK(world.isBodyValid(handle));

    // Transform
    Transform newTransform{Vec3(4.0F, 5.0F, 6.0F), Quat::fromAxisAngle(Vec3(0, 1, 0), 1.57F),
                           Vec3::one()};
    TEST_CHECK(world.setTransform(handle, newTransform).hasValue());
    auto readTransform = world.getTransform(handle).value();
    TEST_CHECK(std::abs(readTransform.position.x - 4.0F) < 1e-3F);
    TEST_CHECK(std::abs(readTransform.position.y - 5.0F) < 1e-3F);
    TEST_CHECK(std::abs(readTransform.position.z - 6.0F) < 1e-3F);

    // Linear Velocity
    TEST_CHECK(world.setLinearVelocity(handle, Vec3(0.0F, 2.0F, 0.0F)).hasValue());
    auto vel = world.getLinearVelocity(handle).value();
    TEST_CHECK(std::abs(vel.y - 2.0F) < 1e-3F);

    // Switch motion type to Dynamic
    TEST_CHECK(world.setMotionType(handle, MotionType::Dynamic).hasValue());
    TEST_CHECK(world.getMotionType(handle).value() == MotionType::Dynamic);

    // Apply force and impulse
    TEST_CHECK(world.addForce(handle, Vec3(100.0F, 0.0F, 0.0F)).hasValue());
    TEST_CHECK(world.addImpulse(handle, Vec3(0.0F, 50.0F, 0.0F)).hasValue());

    return 0;
}

static int testSimulationStepAndGravity() {
    PhysicsWorldSettings settings{};
    settings.gravity = Vec3(0.0F, -10.0F, 0.0F);
    settings.fixedDeltaTime = 1.0F / 60.0F;
    PhysicsWorld world(settings);

    BodyDesc desc{};
    desc.motionType = MotionType::Dynamic;
    desc.collisionLayer = CollisionLayer::kMoving;
    desc.position = Vec3(0.0F, 10.0F, 0.0F);
    desc.collider = ColliderDesc::makeSphere(0.5F);

    auto handle = world.createBody(desc).value();

    // Step physics for 0.5 seconds
    for (int i = 0; i < 30; ++i) {
        TEST_CHECK(world.step(1.0F / 60.0F).hasValue());
    }

    auto pos = world.getPosition(handle).value();
    // Sphere should have fallen significantly from y=10.0
    TEST_CHECK(pos.y < 9.0F);

    // Verify accumulator sub-stepping
    TEST_CHECK(world.getAccumulatedTime() >= 0.0F);
    TEST_CHECK(world.step(0.001F).hasValue());

    return 0;
}

static int testCollisionEvents() {
    PhysicsWorldSettings settings{};
    settings.gravity = Vec3(0.0F, -10.0F, 0.0F);
    PhysicsWorld world(settings);

    // Static floor at y = 0
    BodyDesc floorDesc{};
    floorDesc.motionType = MotionType::Static;
    floorDesc.collisionLayer = CollisionLayer::kNonMoving;
    floorDesc.position = Vec3(0.0F, -0.5F, 0.0F);
    floorDesc.collider = ColliderDesc::makeBox(Vec3(20.0F, 1.0F, 20.0F));
    auto floorHandle = world.createBody(floorDesc).value();

    // Dynamic box dropped from y = 1.0 (bottom touches floor surface at y=0)
    BodyDesc boxDesc{};
    boxDesc.motionType = MotionType::Dynamic;
    boxDesc.collisionLayer = CollisionLayer::kMoving;
    boxDesc.position = Vec3(0.0F, 1.0F, 0.0F);
    boxDesc.collider = ColliderDesc::makeBox(Vec3(1.0F, 1.0F, 1.0F));
    auto boxHandle = world.createBody(boxDesc).value();

    bool contacted = false;
    // Step until collision occurs
    for (int i = 0; i < 60; ++i) {
        TEST_CHECK(world.step(1.0F / 60.0F).hasValue());
        auto events = world.getCollisionEvents();
        for (const auto& ev : events) {
            if ((ev.bodyA == floorHandle && ev.bodyB == boxHandle) ||
                (ev.bodyA == boxHandle && ev.bodyB == floorHandle)) {
                contacted = true;
                break;
            }
        }
        if (contacted) {
            break;
        }
    }

    TEST_CHECK(contacted);

    world.clearCollisionEvents();
    TEST_CHECK(world.getCollisionEvents().empty());

    // Regression check: Destroy colliding box and step in free space; verify events remain clean
    // and do not leak or retain old contact history
    TEST_CHECK(world.destroyBody(boxHandle).hasValue());
    BodyDesc airBoxDesc{};
    airBoxDesc.motionType = MotionType::Dynamic;
    airBoxDesc.collisionLayer = CollisionLayer::kMoving;
    airBoxDesc.position = Vec3(0.0F, 100.0F, 0.0F);
    airBoxDesc.collider = ColliderDesc::makeBox(Vec3(1.0F, 1.0F, 1.0F));
    auto airHandle = world.createBody(airBoxDesc).value();

    TEST_CHECK(world.step(1.0F / 60.0F).hasValue());
    TEST_CHECK(world.getCollisionEvents().empty());

    TEST_CHECK(world.destroyBody(airHandle).hasValue());
    TEST_CHECK(world.destroyBody(floorHandle).hasValue());

    return 0;
}

static int testRaycastAndShapeCast() {
    PhysicsWorld world;

    // Static box at (0, 0, 5)
    BodyDesc desc{};
    desc.motionType = MotionType::Static;
    desc.collisionLayer = CollisionLayer::kNonMoving;
    desc.position = Vec3(0.0F, 0.0F, 5.0F);
    desc.collider = ColliderDesc::makeBox(Vec3(1.0F, 1.0F, 1.0F));
    auto boxHandle = world.createBody(desc).value();

    // Raycast pointing directly at the box
    RaycastQuery query{};
    query.origin = Vec3(0.0F, 0.0F, 0.0F);
    query.direction = Vec3(0.0F, 0.0F, 1.0F);
    query.maxDistance = 10.0F;

    auto hit = world.castRay(query);
    TEST_CHECK(hit.hasHit);
    TEST_CHECK(hit.body == boxHandle);
    TEST_CHECK(hit.fraction > 0.3F);
    TEST_CHECK(hit.fraction < 0.5F); // Box front face is at z = 4.0
    TEST_CHECK(std::abs(hit.position.z - 4.0F) < 0.1F);
    TEST_CHECK(std::abs(hit.normal.z - (-1.0F)) < 0.1F); // Surface normal points back (-Z)

    // Raycast missing the box
    RaycastQuery missQuery{};
    missQuery.origin = Vec3(0.0F, 0.0F, 0.0F);
    missQuery.direction = Vec3(0.0F, 1.0F, 0.0F);
    missQuery.maxDistance = 10.0F;

    auto missHit = world.castRay(missQuery);
    TEST_CHECK(!missHit.hasHit);

    // Shape cast (sphere sweep toward box)
    ShapeCastQuery shapeQuery{};
    shapeQuery.shape = ColliderDesc::makeSphere(0.5F);
    shapeQuery.startTransform = Transform(Vec3(0.0F, 0.0F, 0.0F), Quat::identity());
    shapeQuery.direction = Vec3(0.0F, 0.0F, 1.0F);
    shapeQuery.maxDistance = 10.0F;

    auto shapeHit = world.castShape(shapeQuery);
    TEST_CHECK(shapeHit.hasHit);
    TEST_CHECK(shapeHit.body == boxHandle);
    // Sphere of radius 0.5 hits box front (z=4.0) at contact point z=4.0
    TEST_CHECK(std::abs(shapeHit.contactPoint.z - 4.0F) < 0.2F);

    return 0;
}

static int testFlecsIntegration() {
    world::World ecsWorld;
    PhysicsWorld physicsWorld;

    registerPhysicsComponents(ecsWorld);
    registerPhysicsSystems(ecsWorld, physicsWorld);

    // Create dynamic entity
    auto entity = ecsWorld.createEntity();
    BodyDesc desc{};
    desc.motionType = MotionType::Dynamic;
    desc.collisionLayer = CollisionLayer::kMoving;
    desc.position = Vec3(0.0F, 10.0F, 0.0F);
    desc.collider = ColliderDesc::makeSphere(0.5F);

    auto handle = physicsWorld.createBody(desc).value();

    ecsWorld.setComponent<RigidBodyComponent>(
        entity, RigidBodyComponent{.bodyHandle = handle, .motionType = MotionType::Dynamic});
    ecsWorld.setComponent<PhysicsTransformComponent>(
        entity, PhysicsTransformComponent{
                    .transform = Transform(Vec3(0.0F, 10.0F, 0.0F), Quat::identity())});

    TEST_CHECK(physicsWorld.getBodyCount() == 1);

    // Enable Continuous system group and advance simulation via World
    ecsWorld.setSystemGroupActive(world::SystemGroup::Continuous, true);
    ecsWorld.progress(0.1F);

    // Verify dynamic transform updated in ECS
    const auto* comp = ecsWorld.getComponent<PhysicsTransformComponent>(entity);
    TEST_CHECK(comp != nullptr);
    TEST_CHECK(comp->transform.position.y < 10.0F);

    // Destroy entity -> observer should clean up Jolt body
    ecsWorld.destroyEntity(entity);
    TEST_CHECK(physicsWorld.getBodyCount() == 0);
    TEST_CHECK(!physicsWorld.isBodyValid(handle));

    return 0;
}

int main() {
    std::cout << "[PhysicsTest] Running Physics System and Jolt integration tests...\n";

    if (testPhysicsSystemLifecycle() != 0) {
        return 1;
    }
    if (testBodyCreationAndHandles() != 0) {
        return 1;
    }
    if (testKinematicAndDynamicProperties() != 0) {
        return 1;
    }
    if (testSimulationStepAndGravity() != 0) {
        return 1;
    }
    if (testCollisionEvents() != 0) {
        return 1;
    }
    if (testRaycastAndShapeCast() != 0) {
        return 1;
    }
    if (testFlecsIntegration() != 0) {
        return 1;
    }

    std::cout << "[PhysicsTest] All physics tests passed successfully!\n";
    return 0;
}
