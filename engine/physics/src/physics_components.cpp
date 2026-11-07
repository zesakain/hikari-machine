/// @file physics_components.cpp
/// @brief Registration of physics ECS components, observers, and continuous sync systems.

#include "physics/physics_components.h"

#include "physics/physics_world.h"

namespace eng::physics {

void registerPhysicsComponents(world::World& world) {
    auto& ecs = world.getRawWorld();

    ecs.component<RigidBodyComponent>("RigidBodyComponent");
    ecs.component<ColliderComponent>("ColliderComponent");
    ecs.component<PhysicsTransformComponent>("PhysicsTransformComponent");
}

void registerPhysicsSystems(world::World& world, PhysicsWorld& physicsWorld) {
    auto& ecs = world.getRawWorld();

    // 1. Observer: Clean up Jolt body when RigidBodyComponent is removed or entity is deleted
    ecs.observer<RigidBodyComponent>()
        .event(flecs::OnRemove)
        .each([&physicsWorld](flecs::iter& /*it*/, size_t /*index*/, RigidBodyComponent& rb) {
            if (rb.bodyHandle.isValid()) {
                (void)physicsWorld.destroyBody(rb.bodyHandle);
                rb.bodyHandle = {};
            }
        });

    // 2. Pre-sync: Push kinematic transforms from ECS into Jolt simulation
    auto preSync =
        ecs.system<const PhysicsTransformComponent, const RigidBodyComponent>("PhysicsPreSync")
            .kind(flecs::OnUpdate)
            .each(
                [&physicsWorld](const PhysicsTransformComponent& t, const RigidBodyComponent& rb) {
                    if (rb.motionType == MotionType::Kinematic && rb.bodyHandle.isValid()) {
                        (void)physicsWorld.setTransform(rb.bodyHandle, t.transform);
                    }
                });
    world.registerSystem(world::SystemGroup::Continuous, preSync);

    // 3. Step: Advance fixed-timestep Jolt physical simulation
    auto stepSystem =
        ecs.system<>("PhysicsStep").kind(flecs::OnUpdate).run([&physicsWorld](flecs::iter& it) {
            (void)physicsWorld.step(it.delta_time());
        });
    world.registerSystem(world::SystemGroup::Continuous, stepSystem);

    // 4. Post-sync: Pull dynamic simulated body transforms back into ECS
    auto postSync =
        ecs.system<PhysicsTransformComponent, const RigidBodyComponent>("PhysicsPostSync")
            .kind(flecs::PostUpdate)
            .each([&physicsWorld](PhysicsTransformComponent& t, const RigidBodyComponent& rb) {
                if (rb.motionType == MotionType::Dynamic && rb.bodyHandle.isValid()) {
                    auto res = physicsWorld.getTransform(rb.bodyHandle);
                    if (res.hasValue()) {
                        t.transform = res.value();
                    }
                }
            });
    world.registerSystem(world::SystemGroup::Continuous, postSync);
}

} // namespace eng::physics
