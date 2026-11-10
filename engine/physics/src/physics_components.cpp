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
    // it to root while preserving its exact world-space placement
    ecs.observer<RigidBodyComponent>()
        .event(flecs::OnSet)
        .each([](flecs::iter& it, size_t index, RigidBodyComponent& rb) {
            if (rb.motionType == MotionType::Dynamic) {
                flecs::entity e = it.entity(index);
                flecs::entity parent = e.target(flecs::ChildOf);
                if (parent.is_valid()) {
                    // Compute world transform before removing ChildOf
                    Transform worldTr{};
                    if (const auto* wt = e.get<world::WorldTransformComponent>()) {
                        worldTr = wt->worldTransform;
                    } else if (const auto* tc = e.get<world::TransformComponent>()) {
                        Transform parentTr{};
                        if (const auto* pwt = parent.get<world::WorldTransformComponent>()) {
                            parentTr = pwt->worldTransform;
                        } else if (const auto* ptc = parent.get<world::TransformComponent>()) {
                            parentTr = ptc->transform;
                        }
                        worldTr = parentTr.combine(tc->transform);
                    } else if (const auto* ptc = e.get<PhysicsTransformComponent>()) {
                        Transform parentTr{};
                        if (const auto* pwt = parent.get<world::WorldTransformComponent>()) {
                            parentTr = pwt->worldTransform;
                        } else if (const auto* pt = parent.get<PhysicsTransformComponent>()) {
                            parentTr = pt->transform;
                        }
                        worldTr = parentTr.combine(ptc->transform);
                    }

                    // Detach from parent
                    e.remove(flecs::ChildOf, flecs::Wildcard);

                    // Reassign preserved world transform as root local transform
                    if (auto* tc = e.get_mut<world::TransformComponent>()) {
                        tc->transform = worldTr;
                        tc->isDirty = true;
                    }
                    if (auto* ptc = e.get_mut<PhysicsTransformComponent>()) {
                        ptc->transform = worldTr;
                    }
                    if (auto* wt = e.get_mut<world::WorldTransformComponent>()) {
                        wt->worldTransform = worldTr;
                        wt->worldMatrix = worldTr.toMatrix();
                    }
                }
            }
        });
}

PhysicsPipelinePhases registerPhysicsSystems(world::World& world, PhysicsWorld& physicsWorld) {
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

    // 2. Establish deterministic physics pipeline sub-phases strictly chained between PostUpdate
    // and PreStore. Invariant: Gameplay/scripts (OnUpdate) -> Hierarchy PreSync (OnValidate) ->
    // PostUpdate ->
    //            PhysicsPreSyncPhase -> PhysicsStepPhase -> PhysicsPostSyncPhase -> PreStore
    //            (Hierarchy PostSync).
    auto preSyncPhase =
        ecs.entity("PhysicsPreSyncPhase").add(flecs::Phase).depends_on(flecs::PostUpdate);
    auto stepPhase = ecs.entity("PhysicsStepPhase").add(flecs::Phase).depends_on(preSyncPhase);
    auto postSyncPhase = ecs.entity("PhysicsPostSyncPhase").add(flecs::Phase).depends_on(stepPhase);
    ecs.entity(flecs::PreStore).depends_on(postSyncPhase);

    // 3. Pre-sync: Push kinematic transforms from ECS into Jolt simulation
    // Scheduled in PhysicsPreSyncPhase (after HierarchyTransformPreSync in OnValidate and
    // PostUpdate)
    auto preSync =
        ecs.system<PhysicsTransformComponent, const RigidBodyComponent>("PhysicsPreSync")
            .kind(preSyncPhase)
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

    // 4. Step: Advance fixed-timestep Jolt physical simulation
    // Scheduled in PhysicsStepPhase (strictly after PhysicsPreSyncPhase)
    auto stepSystem =
        ecs.system<>("PhysicsStep").kind(stepPhase).run([&physicsWorld](flecs::iter& it) {
            (void)physicsWorld.step(it.delta_time());
        });
    world.registerSystem(world::SystemGroup::Continuous, stepSystem);

    // 5. Post-sync: Pull dynamic simulated body transforms back into ECS
    // Scheduled in PhysicsPostSyncPhase (strictly after PhysicsStepPhase, before PreStore)
    auto postSync =
        ecs.system<PhysicsTransformComponent, const RigidBodyComponent>("PhysicsPostSync")
            .kind(postSyncPhase)
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

    return PhysicsPipelinePhases{
        .preSync = preSyncPhase,
        .step = stepPhase,
        .postSync = postSyncPhase,
    };
}

} // namespace eng::physics
