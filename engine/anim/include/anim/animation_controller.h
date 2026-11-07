/// @file animation_controller.h
/// @brief Playback state machine, cross-fade clip blending, and animation controller.
///
/// Purpose:
/// Coordinates single-clip playback, loop modes, variable playback speed,
/// and smooth cross-fade blending between two animation clips over a specified duration.

#pragma once

#include "anim/animation_clip.h"
#include "anim/skeleton.h"
#include "core/math.h"

#include <cstdint>
#include <span>

namespace eng::anim {

/// @enum PlaybackState
/// @brief Operational state of an AnimationController.
enum class PlaybackState : uint8_t { Playing = 0, Paused = 1, Stopped = 2 };

/// @class AnimationController
/// @brief State machine governing playback, seeking, speed scaling, and cross-fading.
class AnimationController {
public:
    AnimationController() = default;

    /// Starts playing an animation clip immediately.
    ///
    /// @param clip Non-owning pointer to the animation clip (must outlive playback).
    /// @param loop Whether the clip should loop continuously.
    /// @param playbackSpeed Playback speed multiplier (1.0 = normal).
    void play(const AnimationClip* clip, bool loop = true, float playbackSpeed = 1.0F) noexcept;

    /// Initiates a smooth cross-fade transition from the current clip to a target clip.
    ///
    /// @param targetClip Non-owning pointer to the target animation clip.
    /// @param durationSeconds Transition blend duration in seconds.
    /// @param loop Whether the target clip should loop continuously.
    /// @param playbackSpeed Target clip playback speed multiplier.
    void crossFade(const AnimationClip* targetClip, float durationSeconds, bool loop = true,
                   float playbackSpeed = 1.0F) noexcept;

    /// Pauses playback at the current timestamp.
    void pause() noexcept;

    /// Resumes paused playback.
    void resume() noexcept;

    /// Stops playback and rewinds to timestamp 0.
    void stop() noexcept;

    /// Seeks playback to a specific timestamp in seconds.
    void setTime(float timeSeconds) noexcept;

    /// Current evaluation timestamp in seconds.
    [[nodiscard]] float getTime() const noexcept { return currentTimeSeconds_; }

    /// Current playback state.
    [[nodiscard]] PlaybackState getState() const noexcept { return state_; }

    /// Whether playback is currently configured to loop.
    [[nodiscard]] bool isLooping() const noexcept { return isLooping_; }

    /// Whether a cross-fade blend is currently in progress.
    [[nodiscard]] bool isBlending() const noexcept { return isBlending_; }

    /// Normalized cross-fade blend factor in [0, 1].
    [[nodiscard]] float getBlendFactor() const noexcept { return blendFactor_; }

    /// Advances playback and cross-fade blending time.
    ///
    /// @param deltaSeconds Time delta in seconds.
    void update(float deltaSeconds) noexcept;

    /// Evaluates current local-space bone transforms (with cross-fade blending if active).
    ///
    /// @param skeleton Reference skeleton providing bind poses and bone counts.
    /// @param outLocalTransforms Output span for evaluated local-space transforms.
    void evaluateLocalPose(const Skeleton& skeleton,
                           std::span<Transform> outLocalTransforms) const noexcept;

private:
    const AnimationClip* currentClip_{nullptr};
    const AnimationClip* targetClip_{nullptr};

    float currentTimeSeconds_{0.0F};
    float targetTimeSeconds_{0.0F};
    float playbackSpeed_{1.0F};
    float targetPlaybackSpeed_{1.0F};

    float blendDuration_{0.0F};
    float blendElapsed_{0.0F};
    float blendFactor_{0.0F};

    PlaybackState state_{PlaybackState::Stopped};
    bool isLooping_{true};
    bool targetLooping_{true};
    bool isBlending_{false};
};

} // namespace eng::anim
