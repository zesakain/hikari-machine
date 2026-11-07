/// @file skeleton.h
/// @brief Skeletal hierarchy DAG and linear forward kinematics evaluation.
///
/// Purpose:
/// Represents a topologically ordered skeletal bone hierarchy.
/// Evaluates forward kinematics from local bone transforms to model-space poses in $O(N)$ time
/// and computes final skinning matrices ($M_{model} \times M^{-1}_{bind}$).
///
/// Invariants:
/// Every bone added must satisfy parentIndex < boneIndex or parentIndex == kInvalidBoneIndex.
/// Forward kinematics propagation visits bones in strictly increasing index order without
/// recursion.

#pragma once

#include "anim/bone.h"
#include "core/result.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eng::anim {

/// @class Skeleton
/// @brief Skeletal bone hierarchy definition and skinning evaluator.
class Skeleton {
public:
    Skeleton() = default;

    /// Adds a bone to the skeleton hierarchy.
    ///
    /// Preconditions: parentIndex must be strictly less than the current bone count,
    /// or equal to kInvalidBoneIndex for root bones.
    ///
    /// @param name Diagnostic bone identifier.
    /// @param parentIndex Index of parent bone or kInvalidBoneIndex.
    /// @param localBindPose Rest-pose local transformation relative to parent.
    /// @return Assigned bone index or error if hierarchy invariant is violated.
    Result<uint16_t> addBone(std::string_view name, uint16_t parentIndex,
                             const Transform& localBindPose);

    /// Total number of bones in this skeleton.
    [[nodiscard]] uint16_t getBoneCount() const noexcept;

    /// Read-only view of all bones in topological order.
    [[nodiscard]] std::span<const Bone> getBones() const noexcept;

    /// Look up bone index by diagnostic name, or -1 if not found.
    [[nodiscard]] int32_t findBoneIndex(std::string_view name) const noexcept;

    /// Diagnostic name of bone at given index.
    [[nodiscard]] std::string_view getBoneName(uint16_t index) const noexcept;

    /// Finalizes the skeleton hierarchy by precomputing all inverse bind matrices.
    ///
    /// Must be called once after all bones are added via addBone().
    Result<void> finalize();

    /// Evaluates forward kinematics from local-space bone transforms into model-space transforms.
    ///
    /// Preconditions: localTransforms and outModelTransforms must have size >= getBoneCount().
    /// Complexity: $O(N)$ linear forward pass.
    [[nodiscard]] Result<void>
    evaluateModelPose(std::span<const Transform> localTransforms,
                      std::span<Transform> outModelTransforms) const noexcept;

    /// Computes final skinning matrices for GPU vertex blending:
    /// `outSkinningMatrices[i] = modelTransforms[i].toMat4() * inverseBindMatrix[i]`.
    ///
    /// Preconditions: modelTransforms and outSkinningMatrices must have size >= getBoneCount().
    [[nodiscard]] Result<void>
    evaluateSkinningMatrices(std::span<const Transform> modelTransforms,
                             std::span<Mat4> outSkinningMatrices) const noexcept;

private:
    std::vector<Bone> bones_;
    std::vector<std::string> boneNames_;
    bool isFinalized_{false};
};

} // namespace eng::anim
