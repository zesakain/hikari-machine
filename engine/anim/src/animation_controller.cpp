/// @file animation_controller.cpp
/// @brief Animation playback state machine and cross-fade blending implementation.

#include "anim/animation_controller.h"

#include "anim/animation_sampler.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace eng::anim {

void AnimationController::play(const AnimationClip* clip, bool loop, float playbackSpeed) noexcept {
    currentClip_ = clip;
    targetClip_ = nullptr;
    currentTimeSeconds_ = 0.0F;
    playbackSpeed_ = playbackSpeed;
    isLooping_ = loop;
    isBlending_ = false;
    blendFactor_ = 0.0F;
    state_ = (clip != nullptr) ? PlaybackState::Playing : PlaybackState::Stopped;
}

void AnimationController::crossFade(const AnimationClip* targetClip, float durationSeconds,
                                    bool loop, float playbackSpeed) noexcept {
    if (state_ != PlaybackState::Playing || currentClip_ == nullptr || durationSeconds <= 1e-4F) {
        play(targetClip, loop, playbackSpeed);
        return;
    }

    targetClip_ = targetClip;
    targetTimeSeconds_ = 0.0F;
    targetPlaybackSpeed_ = playbackSpeed;
    targetLooping_ = loop;
    blendDuration_ = durationSeconds;
    blendElapsed_ = 0.0F;
    blendFactor_ = 0.0F;
    isBlending_ = (targetClip != nullptr);
}

void AnimationController::pause() noexcept {
    if (state_ == PlaybackState::Playing) {
        state_ = PlaybackState::Paused;
    }
}

void AnimationController::resume() noexcept {
    if (state_ == PlaybackState::Paused) {
        state_ = PlaybackState::Playing;
    }
}

void AnimationController::stop() noexcept {
    state_ = PlaybackState::Stopped;
    currentTimeSeconds_ = 0.0F;
    targetTimeSeconds_ = 0.0F;
    isBlending_ = false;
    blendFactor_ = 0.0F;
}

void AnimationController::setTime(float timeSeconds) noexcept {
    currentTimeSeconds_ = timeSeconds;
    if (currentClip_ != nullptr && currentClip_->getDuration() > 0.0F) {
        if (isLooping_) {
            currentTimeSeconds_ = std::fmod(currentTimeSeconds_, currentClip_->getDuration());
            if (currentTimeSeconds_ < 0.0F) {
                currentTimeSeconds_ += currentClip_->getDuration();
            }
        } else {
            currentTimeSeconds_ =
                std::clamp(currentTimeSeconds_, 0.0F, currentClip_->getDuration());
        }
    }
}

void AnimationController::update(float deltaSeconds) noexcept {
    if (state_ != PlaybackState::Playing) {
        return;
    }

    // Advance current clip
    if (currentClip_ != nullptr && currentClip_->getDuration() > 0.0F) {
        currentTimeSeconds_ += deltaSeconds * playbackSpeed_;
        if (isLooping_) {
            currentTimeSeconds_ = std::fmod(currentTimeSeconds_, currentClip_->getDuration());
            if (currentTimeSeconds_ < 0.0F) {
                currentTimeSeconds_ += currentClip_->getDuration();
            }
        } else {
            currentTimeSeconds_ =
                std::clamp(currentTimeSeconds_, 0.0F, currentClip_->getDuration());
        }
    }

    // Advance cross-fade blend
    if (isBlending_ && targetClip_ != nullptr) {
        if (targetClip_->getDuration() > 0.0F) {
            targetTimeSeconds_ += deltaSeconds * targetPlaybackSpeed_;
            if (targetLooping_) {
                targetTimeSeconds_ = std::fmod(targetTimeSeconds_, targetClip_->getDuration());
                if (targetTimeSeconds_ < 0.0F) {
                    targetTimeSeconds_ += targetClip_->getDuration();
                }
            } else {
                targetTimeSeconds_ =
                    std::clamp(targetTimeSeconds_, 0.0F, targetClip_->getDuration());
            }
        }

        blendElapsed_ += deltaSeconds;
        blendFactor_ = std::clamp(blendElapsed_ / blendDuration_, 0.0F, 1.0F);

        if (blendFactor_ >= 1.0F) {
            // Blend complete: promote target clip to current
            currentClip_ = targetClip_;
            currentTimeSeconds_ = targetTimeSeconds_;
            playbackSpeed_ = targetPlaybackSpeed_;
            isLooping_ = targetLooping_;
            targetClip_ = nullptr;
            isBlending_ = false;
            blendFactor_ = 0.0F;
        }
    }
}

void AnimationController::evaluateLocalPose(
    const Skeleton& skeleton, std::span<Transform> outLocalTransforms) const noexcept {
    const auto bones = skeleton.getBones();
    const size_t count = std::min(bones.size(), outLocalTransforms.size());

    for (size_t i = 0; i < count; ++i) {
        outLocalTransforms[i] = bones[i].localBindPose;
    }

    if (currentClip_ != nullptr) {
        AnimationSampler::sampleClip(*currentClip_, currentTimeSeconds_, outLocalTransforms);
    }

    if (isBlending_ && targetClip_ != nullptr && blendFactor_ > 0.0F) {
        std::vector<Transform> targetPose(count);
        for (size_t i = 0; i < count; ++i) {
            targetPose[i] = bones[i].localBindPose;
        }
        AnimationSampler::sampleClip(*targetClip_, targetTimeSeconds_, targetPose);

        for (size_t i = 0; i < count; ++i) {
            outLocalTransforms[i] =
                Transform::lerp(outLocalTransforms[i], targetPose[i], blendFactor_);
        }
    }
}

} // namespace eng::anim
