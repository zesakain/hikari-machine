/// @file physics_world.h
/// @brief Discrete 3D physics simulation world and spatial query interface.
///
/// Purpose:
/// Coordinates rigid body simulation, collision detection, contact generation, and spatial
/// queries using Jolt Physics behind an opaque, encapsulated PIMPL interface.
///
/// Ownership and Lifetime:
/// Created by engine framework or Shared World. Owns all active rigid bodies and shapes.
///
/// Thread-safety:
/// Individual mutation calls (createBody, destroyBody, step) must be serialized on the main
/// simulation thread. Queries may be executed concurrently if not stepping.
///
/// Performance:
/// Stepping is governed by a fixed-timestep accumulator to ensure deterministic frame times.

#pragma once

#include "core/error.h"
#include "core/math.h"
#include "core/result.h"
#include "physics/physics_types.h"

#include <memory>
#include <span>

namespace eng::physics {

/// @struct PhysicsWorldSettings
/// @brief Configuration settings for initializing a PhysicsWorld.
struct PhysicsWorldSettings {
    Vec3 gravity{0.0F, -9.81F, 0.0F};
    uint32_t maxBodies{10240};
    uint32_t numBodyMutexes{0};
    uint32_t maxBodyPairs{10240};
    uint32_t maxContactConstraints{10240};

    /// Fixed simulation timestep (default: 60 Hz = 0.0166667 s).
    float fixedDeltaTime{1.0F / 60.0F};

    /// Maximum fixed substeps executed per step() call to prevent runaway death spiral.
    uint32_t maxSubSteps{4};

    /// Number of worker threads for physics simulation (0 = synchronous single-threaded, >=1 =
    /// multi-threaded pool).
    uint32_t numWorkerThreads{0};
};

/// @class PhysicsWorld
/// @brief Concrete physics world managing rigid body simulation and spatial intersection queries.
class PhysicsWorld {
public:
    explicit PhysicsWorld(const PhysicsWorldSettings& settings = {});
    ~PhysicsWorld() noexcept;

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsWorld(PhysicsWorld&&) noexcept;
    PhysicsWorld& operator=(PhysicsWorld&&) noexcept;

    /// Instantiates a new rigid body in the physics simulation.
    [[nodiscard]] Result<BodyHandle> createBody(const BodyDesc& desc);

    /// Removes and frees a rigid body from the simulation, invalidating its handle.
    [[nodiscard]] Result<void> destroyBody(BodyHandle handle);

    /// Checks whether a body handle currently references an active rigid body.
    [[nodiscard]] bool isBodyValid(BodyHandle handle) const noexcept;

    /// Returns the number of active bodies currently in the simulation.
    [[nodiscard]] uint32_t getBodyCount() const noexcept;

    // Body state mutators and accessors
    [[nodiscard]] Result<Vec3> getPosition(BodyHandle handle) const;
    [[nodiscard]] Result<void> setPosition(BodyHandle handle, const Vec3& position);

    [[nodiscard]] Result<Quat> getRotation(BodyHandle handle) const;
    [[nodiscard]] Result<void> setRotation(BodyHandle handle, const Quat& rotation);

    [[nodiscard]] Result<Transform> getTransform(BodyHandle handle) const;
    [[nodiscard]] Result<void> setTransform(BodyHandle handle, const Transform& transform);

    [[nodiscard]] Result<Vec3> getLinearVelocity(BodyHandle handle) const;
    [[nodiscard]] Result<void> setLinearVelocity(BodyHandle handle, const Vec3& velocity);

    [[nodiscard]] Result<Vec3> getAngularVelocity(BodyHandle handle) const;
    [[nodiscard]] Result<void> setAngularVelocity(BodyHandle handle, const Vec3& velocity);

    [[nodiscard]] Result<void> addForce(BodyHandle handle, const Vec3& force);
    [[nodiscard]] Result<void> addImpulse(BodyHandle handle, const Vec3& impulse);

    [[nodiscard]] Result<MotionType> getMotionType(BodyHandle handle) const;
    [[nodiscard]] Result<void> setMotionType(BodyHandle handle, MotionType motionType);

    // Spatial queries
    [[nodiscard]] RaycastHit castRay(const RaycastQuery& query) const;
    [[nodiscard]] ShapeCastHit castShape(const ShapeCastQuery& query) const;

    /// Advances the simulation using the fixed-timestep accumulator.
    ///
    /// @param deltaTime Elapsed real/unscaled frame time in seconds.
    /// @return Number of fixed substeps executed.
    [[nodiscard]] Result<uint32_t> step(float deltaTime);

    /// Current unconsumed accumulated time in seconds.
    [[nodiscard]] float getAccumulatedTime() const noexcept;

    /// Resets the time accumulator to zero.
    void resetAccumulator() noexcept;

    /// Returns all contact events generated during the previous step().
    [[nodiscard]] std::span<const CollisionEvent> getCollisionEvents() const noexcept;

    /// Clears recorded collision events.
    void clearCollisionEvents() noexcept;

    /// Returns current settings.
    [[nodiscard]] const PhysicsWorldSettings& getSettings() const noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace eng::physics
