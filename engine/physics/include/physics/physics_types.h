/// @file physics_types.h
/// @brief Types, descriptors, handles, and query structures for 3D physics.
///
/// Purpose:
/// Defines hardware-neutral, zero-overhead rigid body, collider, query, and contact structures.
/// Adheres strictly to Rule 2 and Rule 3: encapsulates all physics concepts without exposing
/// third-party Jolt headers to engine callers.
///
/// Ownership and Lifetime:
/// Descriptors and queries are value types passed by const reference.
/// BodyHandle is a generational handle referencing a physics body managed by PhysicsWorld.
///
/// Thread-safety:
/// Descriptors are immutable value types; fully thread-safe.

#pragma once

#include "core/generational_handle.h"
#include "core/math.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace eng::physics {

/// Generational handle uniquely referencing an allocated rigid body.
using BodyHandle = GenerationalHandle;

/// @enum MotionType
/// @brief Kinematic motion classification for rigid bodies.
enum class MotionType : uint8_t {
    /// Immovable body with infinite mass (e.g. terrain, buildings).
    Static = 0,

    /// Keyframed body moved by explicit velocity or position, ignoring forces.
    Kinematic = 1,

    /// Fully simulated body responding to forces, impulses, and collisions.
    Dynamic = 2
};

/// @enum ColliderType
/// @brief Geometric primitive classification for collision shapes.
enum class ColliderType : uint8_t { Box = 0, Sphere = 1, Capsule = 2 };

/// @struct CollisionLayer
/// @brief Standard collision filtering categories.
struct CollisionLayer {
    static constexpr uint16_t kNonMoving = 0;
    static constexpr uint16_t kMoving = 1;
    static constexpr uint16_t kPlayer = 2;
    static constexpr uint16_t kSensor = 3;
    static constexpr uint16_t kCount = 4;
};

/// @struct ColliderDesc
/// @brief Geometric collision primitive configuration.
struct ColliderDesc {
    ColliderType type{ColliderType::Box};

    /// Half-extents along XYZ axes for Box colliders.
    Vec3 halfExtents{0.5F, 0.5F, 0.5F};

    /// Radius for Sphere and Capsule colliders.
    float radius{0.5F};

    /// Half-height along Y axis for Capsule colliders.
    float halfHeight{0.5F};

    /// Local center offset relative to the rigid body origin.
    Vec3 offset{Vec3::zero()};

    /// Local orientation relative to the rigid body origin.
    Quat rotation{Quat::identity()};

    [[nodiscard]] static constexpr ColliderDesc
    makeBox(const Vec3& halfExtents, const Vec3& offset = Vec3::zero()) noexcept {
        ColliderDesc desc{};
        desc.type = ColliderType::Box;
        desc.halfExtents = halfExtents;
        desc.offset = offset;
        return desc;
    }

    [[nodiscard]] static constexpr ColliderDesc
    makeSphere(float radius, const Vec3& offset = Vec3::zero()) noexcept {
        ColliderDesc desc{};
        desc.type = ColliderType::Sphere;
        desc.radius = radius;
        desc.offset = offset;
        return desc;
    }

    [[nodiscard]] static constexpr ColliderDesc
    makeCapsule(float radius, float halfHeight, const Vec3& offset = Vec3::zero()) noexcept {
        ColliderDesc desc{};
        desc.type = ColliderType::Capsule;
        desc.radius = radius;
        desc.halfHeight = halfHeight;
        desc.offset = offset;
        return desc;
    }
};

/// @struct BodyDesc
/// @brief Initialization configuration for instantiating a rigid body in PhysicsWorld.
struct BodyDesc {
    MotionType motionType{MotionType::Dynamic};
    ColliderDesc collider{};

    Vec3 position{Vec3::zero()};
    Quat rotation{Quat::identity()};
    Vec3 linearVelocity{Vec3::zero()};
    Vec3 angularVelocity{Vec3::zero()};

    float mass{1.0F};
    float friction{0.2F};
    float restitution{0.0F};
    float gravityFactor{1.0F};
    float linearDamping{0.05F};
    float angularDamping{0.05F};

    uint16_t collisionLayer{CollisionLayer::kMoving};
    bool isSensor{false};
    void* userData{nullptr};
};

/// @struct RaycastQuery
/// @brief Spatial raycast parameters.
struct RaycastQuery {
    Vec3 origin{Vec3::zero()};
    Vec3 direction{Vec3::unitY()};
    float maxDistance{1000.0F};
    uint16_t layerMask{0xFFFF};
};

/// @struct RaycastHit
/// @brief Result of a raycast intersection query.
struct RaycastHit {
    bool hasHit{false};
    BodyHandle body;
    Vec3 position{Vec3::zero()};
    Vec3 normal{Vec3::zero()};
    float fraction{1.0F};
};

/// @struct ShapeCastQuery
/// @brief Sweep / shape cast query parameters.
struct ShapeCastQuery {
    ColliderDesc shape{};
    Transform startTransform;
    Vec3 direction{Vec3::unitY()};
    float maxDistance{100.0F};
    uint16_t layerMask{0xFFFF};
};

/// @struct ShapeCastHit
/// @brief Result of a shape sweep intersection query.
struct ShapeCastHit {
    bool hasHit{false};
    BodyHandle body;
    Vec3 contactPoint{Vec3::zero()};
    Vec3 contactNormal{Vec3::zero()};
    float fraction{1.0F};
};

/// @enum CollisionEventType
/// @brief Lifecycle state of a physical contact between two bodies.
enum class CollisionEventType : uint8_t {
    ContactAdded = 0,
    ContactPersisted = 1,
    ContactRemoved = 2
};

/// @struct CollisionEvent
/// @brief Recorded contact event between two rigid bodies.
struct CollisionEvent {
    CollisionEventType type{CollisionEventType::ContactAdded};
    BodyHandle bodyA;
    BodyHandle bodyB;
    Vec3 contactPoint{Vec3::zero()};
    Vec3 contactNormal{Vec3::zero()};
};

} // namespace eng::physics
