/// @file animation_sampler.cpp
/// @brief AnimationSampler interpolation algorithms implementation.

#include "anim/animation_sampler.h"

#include <algorithm>

namespace eng::anim {

namespace {

template <typename T>
[[nodiscard]] size_t findKeyframeIndex(std::span<const Keyframe<T>> keys,
                                       float timeSeconds) noexcept {
    auto it = std::upper_bound(keys.begin(), keys.end(), timeSeconds,
                               [](float t, const Keyframe<T>& k) { return t < k.timeSeconds; });
    if (it == keys.begin()) {
        return 0;
    }
    return static_cast<size_t>(std::distance(keys.begin(), it) - 1);
}

} // namespace

Vec3 AnimationSampler::sampleTranslation(std::span<const Keyframe<Vec3>> keys,
                                         float timeSeconds) noexcept {
    if (keys.empty()) {
        return Vec3::zero();
    }
    if (keys.size() == 1 || timeSeconds <= keys.front().timeSeconds) {
        return keys.front().value;
    }
    if (timeSeconds >= keys.back().timeSeconds) {
        return keys.back().value;
    }

    const size_t idx = findKeyframeIndex(keys, timeSeconds);
    const auto& k0 = keys[idx];
    const auto& k1 = keys[idx + 1];

    const float dt = k1.timeSeconds - k0.timeSeconds;
    const float factor = (dt > 1e-6F) ? ((timeSeconds - k0.timeSeconds) / dt) : 0.0F;

    return Vec3::lerp(k0.value, k1.value, factor);
}

Quat AnimationSampler::sampleRotation(std::span<const Keyframe<Quat>> keys,
                                      float timeSeconds) noexcept {
    if (keys.empty()) {
        return Quat::identity();
    }
    if (keys.size() == 1 || timeSeconds <= keys.front().timeSeconds) {
        return keys.front().value;
    }
    if (timeSeconds >= keys.back().timeSeconds) {
        return keys.back().value;
    }

    const size_t idx = findKeyframeIndex(keys, timeSeconds);
    const auto& k0 = keys[idx];
    const auto& k1 = keys[idx + 1];

    const float dt = k1.timeSeconds - k0.timeSeconds;
    const float factor = (dt > 1e-6F) ? ((timeSeconds - k0.timeSeconds) / dt) : 0.0F;

    return Quat::slerp(k0.value, k1.value, factor);
}

Vec3 AnimationSampler::sampleScale(std::span<const Keyframe<Vec3>> keys,
                                   float timeSeconds) noexcept {
    if (keys.empty()) {
        return Vec3::one();
    }
    if (keys.size() == 1 || timeSeconds <= keys.front().timeSeconds) {
        return keys.front().value;
    }
    if (timeSeconds >= keys.back().timeSeconds) {
        return keys.back().value;
    }

    const size_t idx = findKeyframeIndex(keys, timeSeconds);
    const auto& k0 = keys[idx];
    const auto& k1 = keys[idx + 1];

    const float dt = k1.timeSeconds - k0.timeSeconds;
    const float factor = (dt > 1e-6F) ? ((timeSeconds - k0.timeSeconds) / dt) : 0.0F;

    return Vec3::lerp(k0.value, k1.value, factor);
}

Transform AnimationSampler::sampleTrack(const BoneTrack& track, float timeSeconds) noexcept {
    return Transform{
        sampleTranslation(track.translationKeys, timeSeconds),
        sampleRotation(track.rotationKeys, timeSeconds),
        sampleScale(track.scaleKeys, timeSeconds),
    };
}

void AnimationSampler::sampleClip(const AnimationClip& clip, float timeSeconds,
                                  std::span<Transform> inOutLocalTransforms) noexcept {
    for (const auto& track : clip.getTracks()) {
        if (track.boneIndex < inOutLocalTransforms.size()) {
            inOutLocalTransforms[track.boneIndex] = sampleTrack(track, timeSeconds);
        }
    }
}

} // namespace eng::anim
