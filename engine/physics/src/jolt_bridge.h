/// @file jolt_bridge.h
/// @brief Internal bridge, broadphase layers, filters, and conversions for Jolt Physics.
///
/// Purpose:
/// Translates between Hikari core math/physics types and Jolt's internal data structures.
/// Adheres strictly to Rule 3: This header is private to engine/physics/src/ and never exposed
/// to outer engine modules or public headers.

#pragma once

#include "physics/physics_types.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CollisionDispatch.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <mutex>
#include <vector>

namespace eng::physics {

namespace BroadPhaseLayers {
static constexpr JPH::BroadPhaseLayer kNonMoving(0);
static constexpr JPH::BroadPhaseLayer kMoving(1);
static constexpr uint32_t kNumLayers(2);
} // namespace BroadPhaseLayers

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    [[nodiscard]] uint32_t GetNumBroadPhaseLayers() const override {
        return BroadPhaseLayers::kNumLayers;
    }

    [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
        if (inLayer == CollisionLayer::kNonMoving) {
            return BroadPhaseLayers::kNonMoving;
        }
        return BroadPhaseLayers::kMoving;
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    [[nodiscard]] const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
        switch (static_cast<JPH::BroadPhaseLayer::Type>(inLayer)) {
        case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::kNonMoving):
            return "NON_MOVING";
        case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::kMoving):
            return "MOVING";
        default:
            return "INVALID";
        }
    }
#endif
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer inLayer1,
                                     JPH::BroadPhaseLayer inLayer2) const override {
        if (inLayer1 == CollisionLayer::kNonMoving) {
            return inLayer2 == BroadPhaseLayers::kMoving;
        }
        return true;
    }
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer inLayer1,
                                     JPH::ObjectLayer inLayer2) const override {
        return inLayer1 != CollisionLayer::kNonMoving || inLayer2 != CollisionLayer::kNonMoving;
    }
};

// Math conversion helpers
[[nodiscard]] inline JPH::Vec3 toJolt(const Vec3& v) noexcept {
    return {v.x, v.y, v.z};
}

[[nodiscard]] inline Vec3 fromJolt(const JPH::Vec3& v) noexcept {
    return {v.GetX(), v.GetY(), v.GetZ()};
}

[[nodiscard]] inline JPH::Quat toJolt(const Quat& q) noexcept {
    return {q.x, q.y, q.z, q.w};
}

[[nodiscard]] inline Quat fromJolt(const JPH::Quat& q) noexcept {
    return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
}

[[nodiscard]] inline JPH::EMotionType toJolt(MotionType m) noexcept {
    switch (m) {
    case MotionType::Static:
        return JPH::EMotionType::Static;
    case MotionType::Kinematic:
        return JPH::EMotionType::Kinematic;
    case MotionType::Dynamic:
        return JPH::EMotionType::Dynamic;
    }
    return JPH::EMotionType::Dynamic;
}

[[nodiscard]] inline MotionType fromJolt(JPH::EMotionType m) noexcept {
    switch (m) {
    case JPH::EMotionType::Static:
        return MotionType::Static;
    case JPH::EMotionType::Kinematic:
        return MotionType::Kinematic;
    case JPH::EMotionType::Dynamic:
        return MotionType::Dynamic;
    }
    return MotionType::Dynamic;
}

JPH::Ref<JPH::Shape> createJoltShape(const ColliderDesc& desc);

} // namespace eng::physics
