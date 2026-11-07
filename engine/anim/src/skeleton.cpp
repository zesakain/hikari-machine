/// @file skeleton.cpp
/// @brief Skeletal hierarchy and forward kinematics implementation.

#include "anim/skeleton.h"

#include "core/error.h"

namespace eng::anim {

Result<uint16_t> Skeleton::addBone(std::string_view name, uint16_t parentIndex,
                                   const Transform& localBindPose) {
    if (parentIndex != kInvalidBoneIndex && parentIndex >= bones_.size()) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Parent bone index must be strictly less than current bone count"};
    }

    const auto index = static_cast<uint16_t>(bones_.size());
    bones_.push_back(Bone{
        .index = index,
        .parentIndex = parentIndex,
        .localBindPose = localBindPose,
        .inverseBindMatrix = Mat4::identity(),
    });
    boneNames_.emplace_back(name);
    isFinalized_ = false;

    return index;
}

uint16_t Skeleton::getBoneCount() const noexcept {
    return static_cast<uint16_t>(bones_.size());
}

std::span<const Bone> Skeleton::getBones() const noexcept {
    return bones_;
}

int32_t Skeleton::findBoneIndex(std::string_view name) const noexcept {
    for (size_t i = 0; i < boneNames_.size(); ++i) {
        if (boneNames_[i] == name) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

std::string_view Skeleton::getBoneName(uint16_t index) const noexcept {
    if (index < boneNames_.size()) {
        return boneNames_[index];
    }
    return {};
}

Result<void> Skeleton::finalize() {
    if (bones_.empty()) {
        isFinalized_ = true;
        return Result<void>::success();
    }

    // Evaluate model-space bind poses
    std::vector<Transform> modelBindPoses(bones_.size());
    for (size_t i = 0; i < bones_.size(); ++i) {
        const auto& bone = bones_[i];
        if (bone.parentIndex == kInvalidBoneIndex) {
            modelBindPoses[i] = bone.localBindPose;
        } else {
            modelBindPoses[i] = modelBindPoses[bone.parentIndex] * bone.localBindPose;
        }
        bones_[i].inverseBindMatrix = modelBindPoses[i].toMatrix().inverseAffine();
    }

    isFinalized_ = true;
    return Result<void>::success();
}

Result<void> Skeleton::evaluateModelPose(std::span<const Transform> localTransforms,
                                         std::span<Transform> outModelTransforms) const noexcept {
    if (localTransforms.size() < bones_.size() || outModelTransforms.size() < bones_.size()) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Transform spans must have size >= skeleton bone count"};
    }

    for (size_t i = 0; i < bones_.size(); ++i) {
        const auto& bone = bones_[i];
        if (bone.parentIndex == kInvalidBoneIndex) {
            outModelTransforms[i] = localTransforms[i];
        } else {
            outModelTransforms[i] = outModelTransforms[bone.parentIndex] * localTransforms[i];
        }
    }

    return Result<void>::success();
}

Result<void>
Skeleton::evaluateSkinningMatrices(std::span<const Transform> modelTransforms,
                                   std::span<Mat4> outSkinningMatrices) const noexcept {
    if (modelTransforms.size() < bones_.size() || outSkinningMatrices.size() < bones_.size()) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Skinning spans must have size >= skeleton bone count"};
    }

    for (size_t i = 0; i < bones_.size(); ++i) {
        outSkinningMatrices[i] = modelTransforms[i].toMatrix() * bones_[i].inverseBindMatrix;
    }

    return Result<void>::success();
}

} // namespace eng::anim
