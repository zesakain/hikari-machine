/// @file camera_component.h
/// @brief Flecs ECS camera component for perspective viewports and scene viewpoint framing.
///
/// Purpose:
/// Defines the ECS camera component within the rendering module. Represents an in-world camera
/// source with field of view and near/far clipping bounds.
///
/// Contract:
/// - Plain old data (POD) value type stored within Flecs ECS archetypes.
/// - Angles are specified in degrees for FOV; clipping planes in meters.
/// - Valid ranges: fovDegrees in [1.0, 179.0], nearPlane > 0, farPlane > nearPlane.
///
/// Ownership and Lifetime:
/// Owned by the flecs::world / eng::world::World. Destroyed when the host entity is removed.
///
/// Thread Safety:
/// Read safely during scene rendering command recording. Modifiable during simulation/editor ticks.
///
/// Cost:
/// Zero heap allocations. Standard layout struct.

#pragma once

#include <cstdint>

namespace eng::render {

/// @struct CameraComponent
/// @brief Perspective camera component defining field-of-view and clip depths.
struct CameraComponent {
    /// Vertical field of view in degrees (e.g. 60.0 deg).
    float fovDegrees{60.0F};

    /// Distance to near clipping plane in meters (must be strictly positive).
    float nearPlane{0.1F};

    /// Distance to far clipping plane in meters (must exceed nearPlane).
    float farPlane{1000.0F};

    /// Whether this camera is designated as the primary active scene camera.
    bool isPrimary{true};
};

} // namespace eng::render
