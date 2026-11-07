/// @file bone.h
/// @brief Skeletal bone descriptor and topological indexing primitives.
///
/// Purpose:
/// Represents an individual bone in a skeletal hierarchy.
/// Stores hierarchical linkage (index, parentIndex), local bind-pose transform,
/// and precomputed inverse bind-pose transformation matrix.
///
/// Invariants:
/// In a valid Skeleton, parentIndex < index or parentIndex == kInvalidBoneIndex,
/// guaranteeing a topological sort that allows $O(N)$ linear forward kinematics propagation.

#pragma once

#include "core/math.h"

#include <cstdint>

namespace eng::anim {

/// @brief Sentinel value indicating no parent (root bone) or invalid index.
constexpr uint16_t kInvalidBoneIndex = 0xFFFF;

/// @struct Bone
/// @brief Hierarchy node representing an articulated skeletal segment.
struct Bone {
    /// Bone index within the owning Skeleton array.
    uint16_t index{kInvalidBoneIndex};

    /// Index of parent bone, or kInvalidBoneIndex if this is a root bone.
    uint16_t parentIndex{kInvalidBoneIndex};

    /// Local transformation of this bone in bind pose relative to its parent.
    Transform localBindPose;

    /// Precomputed inverse model-space matrix in bind pose ($M^{-1}_{bind}$).
    Mat4 inverseBindMatrix;
};

} // namespace eng::anim
