/// @file light_components.h
/// @brief ECS light components for directional, point, and spot illumination.
///
/// Purpose:
/// Defines Flecs ECS components representing physical light sources within the scene.
/// Supports directional sunlight, omnidirectional point lights, and conical spotlights.
///
/// Contract:
/// - Plain old data (POD) value types stored within Flecs ECS archetypes.
/// - Angles are specified in radians; intensities are in physical lumens/candelas or engine units.
///
/// Ownership and Lifetime:
/// Owned by the flecs::world / eng::world::World. Destroyed when the host entity is removed.
///
/// Thread-safety:
/// Read during scene rendering command recording. Modifiable during simulation/editor ticks.
///
/// Cost:
/// Zero heap allocations. Standard layout structs.

#pragma once

#include "core/math.h"

#include <cstdint>

namespace eng::render {

/// @struct DirectionalLightComponent
/// @brief Infinitely distant parallel light source (sun, moon) with orthographic shadow projection.
struct DirectionalLightComponent {
    /// Direction vector along which light rays travel (normalized in world space).
    Vec3 direction{-0.5F, -1.0F, -0.5F};

    /// Linear RGB radiance color of the emitted light.
    Vec3 color{1.0F, 1.0F, 1.0F};

    /// Radiant flux / intensity multiplier (default 2.5).
    float intensity{2.5F};

    /// Whether this directional light casts real-time shadow depth maps.
    bool castShadows{true};

    /// Constant depth bias for shadow acne mitigation.
    float shadowBias{0.005F};

    /// Toggle to enable or suppress illumination from this source.
    bool enabled{true};
};

/// @struct PointLightComponent
/// @brief Omnidirectional point light source with inverse-square distance attenuation.
struct PointLightComponent {
    /// Local position offset (overridden by WorldTransformComponent if entity has transform).
    Vec3 position{0.0F, 0.0F, 0.0F};

    /// Linear RGB radiance color.
    Vec3 color{1.0F, 1.0F, 1.0F};

    /// Radiant flux / luminous intensity (default 10.0).
    float intensity{10.0F};

    /// Maximum effective radius in meters beyond which illumination smoothly attenuates to zero.
    float range{10.0F};

    /// Toggle to enable or suppress illumination from this source.
    bool enabled{true};
};

/// @struct SpotLightComponent
/// @brief Directed conical light source with angular and distance attenuation.
struct SpotLightComponent {
    /// Local position offset (overridden by WorldTransformComponent if entity has transform).
    Vec3 position{0.0F, 0.0F, 0.0F};

    /// World-space pointing direction (normalized).
    Vec3 direction{0.0F, -1.0F, 0.0F};

    /// Linear RGB radiance color.
    Vec3 color{1.0F, 1.0F, 1.0F};

    /// Radiant flux / luminous intensity (default 15.0).
    float intensity{15.0F};

    /// Maximum effective distance in meters.
    float range{15.0F};

    /// Half-angle in radians defining the fully illuminated inner cone core (e.g. 0.35 rad ~ 20
    /// deg).
    float innerConeAngle{0.35F};

    /// Half-angle in radians defining the outer cone boundary (e.g. 0.55 rad ~ 31.5 deg).
    float outerConeAngle{0.55F};

    /// Whether this spotlight casts real-time shadow depth maps.
    bool castShadows{false};

    /// Toggle to enable or suppress illumination from this source.
    bool enabled{true};
};

} // namespace eng::render
