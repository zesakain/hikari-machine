/// @file jolt_bridge.cpp
/// @brief Implementation of shape creation and conversions for Jolt Physics.

#include "jolt_bridge.h"

namespace eng::physics {

JPH::Ref<JPH::Shape> createJoltShape(const ColliderDesc& desc) {
    JPH::Ref<JPH::Shape> baseShape;

    switch (desc.type) {
    case ColliderType::Box: {
        JPH::BoxShapeSettings boxSettings(toJolt(desc.halfExtents));
        auto result = boxSettings.Create();
        if (result.IsValid()) {
            baseShape = result.Get();
        }
        break;
    }
    case ColliderType::Sphere: {
        JPH::SphereShapeSettings sphereSettings(desc.radius);
        auto result = sphereSettings.Create();
        if (result.IsValid()) {
            baseShape = result.Get();
        }
        break;
    }
    case ColliderType::Capsule: {
        JPH::CapsuleShapeSettings capsuleSettings(desc.halfHeight, desc.radius);
        auto result = capsuleSettings.Create();
        if (result.IsValid()) {
            baseShape = result.Get();
        }
        break;
    }
    }

    if (baseShape == nullptr) {
        // Fallback default box shape if creation fails
        JPH::BoxShapeSettings fallback(JPH::Vec3(0.5F, 0.5F, 0.5F));
        baseShape = fallback.Create().Get();
    }

    const bool hasOffset = !desc.offset.isNearlyEqual(Vec3::zero());
    const bool hasRotation = !desc.rotation.isNearlyEqual(Quat::identity());

    if (hasOffset || hasRotation) {
        JPH::RotatedTranslatedShapeSettings offsetSettings(toJolt(desc.offset),
                                                           toJolt(desc.rotation), baseShape);
        auto result = offsetSettings.Create();
        if (result.IsValid()) {
            return result.Get();
        }
    }

    return baseShape;
}

} // namespace eng::physics
