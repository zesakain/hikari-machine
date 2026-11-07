/// @file animation_clip.h
/// @brief Keyframed skeletal animation clip and bone track definitions.
///
/// Purpose:
/// Represents a discrete animation track collection (translation, rotation, scale)
/// parameterized by time in seconds.

#pragma once

#include "anim/bone.h"
#include "core/math.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eng::anim {

/// @struct Keyframe
/// @brief Discrete time-value sample in an animation channel.
template <typename T> struct Keyframe {
    float timeSeconds{0.0F};
    T value{};
};

/// @struct BoneTrack
/// @brief Animation channels for a single bone.
struct BoneTrack {
    uint16_t boneIndex{kInvalidBoneIndex};
    std::vector<Keyframe<Vec3>> translationKeys;
    std::vector<Keyframe<Quat>> rotationKeys;
    std::vector<Keyframe<Vec3>> scaleKeys;
};

/// @class AnimationClip
/// @brief Container for multi-bone keyframed skeletal animation tracks.
class AnimationClip {
public:
    AnimationClip() = default;
    explicit AnimationClip(std::string_view name, float durationSeconds = 0.0F);

    /// Clip name.
    [[nodiscard]] std::string_view getName() const noexcept { return name_; }

    /// Total duration in seconds.
    [[nodiscard]] float getDuration() const noexcept { return durationSeconds_; }

    /// Sets clip duration in seconds.
    void setDuration(float durationSeconds) noexcept { durationSeconds_ = durationSeconds; }

    /// Adds or replaces a bone animation track.
    void addTrack(BoneTrack track);

    /// Read-only view of all bone animation tracks.
    [[nodiscard]] std::span<const BoneTrack> getTracks() const noexcept { return tracks_; }

    /// Finds a track matching the specified bone index, or nullptr if none exists.
    [[nodiscard]] const BoneTrack* findTrack(uint16_t boneIndex) const noexcept;

private:
    std::string name_;
    float durationSeconds_{0.0F};
    std::vector<BoneTrack> tracks_;
};

} // namespace eng::anim
