/// @file animation_sampler.h
/// @brief Keyframe interpolation and animation channel sampling utilities.
///
/// Purpose:
/// Evaluates instantaneous local bone transforms at a given timestamp $t$.
/// Applies linear interpolation (LERP) for translation and scale, and spherical linear
/// interpolation (SLERP) with shortest-path hemisphere check for rotations.

#pragma once

#include "anim/animation_clip.h"
#include "core/math.h"

#include <span>

namespace eng::anim {

/// @class AnimationSampler
/// @brief Stateless sampler for keyframed animation channels and clips.
class AnimationSampler {
public:
    /// Samples an entire animation clip into a skeleton's local-space transform array.
    ///
    /// @param clip Animation clip to sample.
    /// @param timeSeconds Evaluation timestamp (unclamped or wrapped).
    /// @param inOutLocalTransforms Pre-populated local transforms (tracks overwrite matched
    /// indices).
    static void sampleClip(const AnimationClip& clip, float timeSeconds,
                           std::span<Transform> inOutLocalTransforms) noexcept;

    /// Samples a single bone track into a local transform.
    [[nodiscard]] static Transform sampleTrack(const BoneTrack& track, float timeSeconds) noexcept;

    /// Evaluates linear translation interpolation between keyframes.
    [[nodiscard]] static Vec3 sampleTranslation(std::span<const Keyframe<Vec3>> keys,
                                                float timeSeconds) noexcept;

    /// Evaluates spherical linear rotation interpolation (SLERP) between keyframes.
    [[nodiscard]] static Quat sampleRotation(std::span<const Keyframe<Quat>> keys,
                                             float timeSeconds) noexcept;

    /// Evaluates linear scale interpolation between keyframes.
    [[nodiscard]] static Vec3 sampleScale(std::span<const Keyframe<Vec3>> keys,
                                          float timeSeconds) noexcept;
};

} // namespace eng::anim
