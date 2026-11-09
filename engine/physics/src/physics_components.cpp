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

    // Dynamic rigid-body entities must remain root entities in this milestone
    world.addHierarchyValidator(
        [](const world::World& w, world::EntityHandle child, world::EntityHandle parent) -> bool {
            if (parent.isValid()) {
                const auto* rb = w.getComponent<RigidBodyComponent>(child);
                if (rb != nullptr && rb->motionType == MotionType::Dynamic) {
                    return false;
                }
            }
            return true;
        });

    // Observer: If an entity already has a parent and receives Dynamic RigidBodyComponent, detach
    // it to root
    ecs.observer<RigidBodyComponent>()
        .event(flecs::OnSet)
        .each([](flecs::iter& it, size_t index, RigidBodyComponent& rb) {
            if (rb.motionType == MotionType::Dynamic) {
                flecs::entity e = it.entity(index);
                if (e.target(flecs::ChildOf).is_valid()) {
                    e.remove(flecs::ChildOf, flecs::Wildcard);
                }
            }
        });
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
    // Scheduled in PostUpdate after HierarchyTransformPreSync (OnValidate)
    auto preSync =
        ecs.system<PhysicsTransformComponent, const RigidBodyComponent>("PhysicsPreSync")
            .kind(flecs::PostUpdate)
            .each([&physicsWorld](flecs::iter& it, size_t index, PhysicsTransformComponent& t,
                                  const RigidBodyComponent& rb) {
                if (rb.motionType == MotionType::Kinematic && rb.bodyHandle.isValid()) {
                    flecs::entity e = it.entity(index);
                    const auto* wt = e.get<world::WorldTransformComponent>();
                    const Transform targetTransform =
                        (wt != nullptr) ? wt->worldTransform : t.transform;
                    (void)physicsWorld.setTransform(rb.bodyHandle, targetTransform);
                    if (wt != nullptr) {
                        t.transform = targetTransform;
                    }
                }
            });
    world.registerSystem(world::SystemGroup::Continuous, preSync);

    // 3. Step: Advance fixed-timestep Jolt physical simulation
    // Scheduled in PostUpdate immediately after PhysicsPreSync
    auto stepSystem =
        ecs.system<>("PhysicsStep").kind(flecs::PostUpdate).run([&physicsWorld](flecs::iter& it) {
            (void)physicsWorld.step(it.delta_time());
        });
    world.registerSystem(world::SystemGroup::Continuous, stepSystem);

    // 4. Post-sync: Pull dynamic simulated body transforms back into ECS
    auto postSync =
        ecs.system<PhysicsTransformComponent, const RigidBodyComponent>("PhysicsPostSync")
            .kind(flecs::PostUpdate)
            .each([&physicsWorld](flecs::iter& it, size_t index, PhysicsTransformComponent& t,
                                  const RigidBodyComponent& rb) {
                if (rb.motionType == MotionType::Dynamic && rb.bodyHandle.isValid()) {
                    auto res = physicsWorld.getTransform(rb.bodyHandle);
                    if (res.hasValue()) {
                        t.transform = res.value();
                        flecs::entity e = it.entity(index);
                        if (auto* tc = e.get_mut<world::TransformComponent>()) {
                            tc->transform = res.value();
                            tc->isDirty = true;
                        }
                    }
                }
            });
    world.registerSystem(world::SystemGroup::Continuous, postSync);
}

} // namespace eng::physics
