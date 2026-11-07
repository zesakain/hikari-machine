/// @file animation_components.h
/// @brief Flecs ECS components and systems for skeletal animation and skinning.
///
/// Purpose:
/// Binds skeletons, animation controllers, and skinning matrices to entities in the shared World.
/// Evaluates poses within the Continuous system group.

#pragma once

#include "anim/animation_clip.h"
#include "anim/animation_controller.h"
#include "anim/skeleton.h"
#include "core/math.h"
#include "world/world.h"

#include <vector>

namespace eng::anim {

/// @struct SkeletonComponent
/// @brief Non-owning reference to an entity's skeletal hierarchy asset.
struct SkeletonComponent {
    const Skeleton* skeleton{nullptr};
};

/// @struct AnimationComponent
/// @brief Playback controller and state for an entity's skeletal animations.
struct AnimationComponent {
    AnimationController controller;
};

/// @struct SkinningPaletteComponent
/// @brief Evaluated model poses and skinning matrices ready for vertex shading.
struct SkinningPaletteComponent {
    std::vector<Transform> localPose;
    std::vector<Transform> modelPose;
    std::vector<Mat4> skinningMatrices;
};

/// Registers animation components in the Flecs World.
void registerAnimationComponents(world::World& world);

/// Registers continuous animation playback and pose evaluation systems in the World.
void registerAnimationSystems(world::World& world);

} // namespace eng::anim
