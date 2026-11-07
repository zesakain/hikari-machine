/// @file physics_components.h
/// @brief Flecs ECS components and synchronization systems for physics integration.
///
/// Purpose:
/// Binds Jolt rigid bodies and colliders into the authoritative Shared World.
/// Implements transform synchronization between ECS components and Jolt physical bodies
/// under the Two-State Conductor's continuous execution phase.
///
/// Ownership and Lifetime:
/// Components are value types stored in Flecs archetypes. Associated Jolt bodies are cleaned up
/// automatically on entity deletion.
///
/// Thread-safety:
/// Flecs system pipelines are executed on the main simulation thread.

#pragma once

#include "core/math.h"
#include "physics/physics_types.h"
#include "world/world.h"

namespace eng::physics {

class PhysicsWorld;

/// @struct RigidBodyComponent
/// @brief Flecs ECS component associating an entity with an active physics rigid body.
struct RigidBodyComponent {
    BodyHandle bodyHandle;
    MotionType motionType{MotionType::Dynamic};
    uint16_t collisionLayer{CollisionLayer::kMoving};
};

/// @struct ColliderComponent
/// @brief Flecs ECS component specifying the shape of an entity's physical collider.
struct ColliderComponent {
    ColliderDesc desc{};
};

/// @struct PhysicsTransformComponent
/// @brief Simulated world transform synchronized with the active physics body.
struct PhysicsTransformComponent {
    Transform transform;
};

/// Registers physics component reflection and observers in the Flecs World.
void registerPhysicsComponents(world::World& world);

/// Registers continuous physics stepping and transform synchronization systems in the World.
///
/// Preconditions:
/// - registerPhysicsComponents() has been called.
/// - physicsWorld is initialized and valid.
void registerPhysicsSystems(world::World& world, PhysicsWorld& physicsWorld);

} // namespace eng::physics
