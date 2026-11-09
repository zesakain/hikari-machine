/// @file components.h
/// @brief Core spatial transform components for hierarchical scene graph representation.
///
/// Purpose:
/// Defines local and world spatial transformation components stored in Flecs ECS archetypes.
/// Decouples local parent-relative TRS transforms from computed global world matrices.
///
/// Ownership and Lifetime:
/// Pure value types stored within Flecs ECS archetype tables. Contains no heap pointers or handles.
///
/// Thread-safety:
/// Value types safely readable across worker jobs during non-mutating queries.
///
/// Performance:
/// POD structures fitting contiguous archetype memory without runtime allocation overhead.

#pragma once

#include "core/math.h"

namespace eng::world {

/// @struct TransformComponent
/// @brief Local spatial transformation relative to parent entity (or world origin if root).
struct TransformComponent {
    Transform transform{};
    bool isDirty{true};

    constexpr TransformComponent() noexcept = default;
    constexpr explicit TransformComponent(const Transform& t) noexcept
        : transform(t), isDirty(true) {}
};

/// @struct WorldTransformComponent
/// @brief Evaluated world-space transformation matrix and decomposed TRS.
struct WorldTransformComponent {
    Mat4 worldMatrix{Mat4::identity()};
    Transform worldTransform{};

    constexpr WorldTransformComponent() noexcept = default;
    constexpr explicit WorldTransformComponent(const Mat4& mat) noexcept
        : worldMatrix(mat), worldTransform() {}
    constexpr WorldTransformComponent(const Mat4& mat, const Transform& tr) noexcept
        : worldMatrix(mat), worldTransform(tr) {}
};

} // namespace eng::world
