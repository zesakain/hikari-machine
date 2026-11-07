/// @file animation_clip.cpp
/// @brief AnimationClip and track management implementation.

#include "anim/animation_clip.h"

namespace eng::anim {

AnimationClip::AnimationClip(std::string_view name, float durationSeconds)
    : name_(name), durationSeconds_(durationSeconds) {}

void AnimationClip::addTrack(BoneTrack track) {
    for (auto& existing : tracks_) {
        if (existing.boneIndex == track.boneIndex) {
            existing = std::move(track);
            return;
        }
    }
    tracks_.push_back(std::move(track));
}

const BoneTrack* AnimationClip::findTrack(uint16_t boneIndex) const noexcept {
    for (const auto& track : tracks_) {
        if (track.boneIndex == boneIndex) {
            return &track;
        }
    }
    return nullptr;
}

} // namespace eng::anim
