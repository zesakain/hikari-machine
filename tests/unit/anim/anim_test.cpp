/// @file anim_test.cpp
/// @brief Comprehensive unit tests for Skeletal Animation System, Sampler, Controller, and ECS
/// sync.

#include "anim/animation_clip.h"
#include "anim/animation_components.h"
#include "anim/animation_controller.h"
#include "anim/animation_sampler.h"
#include "anim/skeleton.h"
#include "world/world.h"

#include <cmath>
#include <iostream>

using namespace eng;
using namespace eng::anim;

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << '\n'; \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

static int testSkeletonHierarchyAndForwardKinematics() {
    Skeleton skeleton;

    // Bone 0: Root at (0, 1, 0)
    auto rootRes = skeleton.addBone("Root", kInvalidBoneIndex, Transform{Vec3(0.0F, 1.0F, 0.0F)});
    TEST_CHECK(rootRes.hasValue());
    TEST_CHECK(rootRes.value() == 0);

    // Bone 1: Spine attached to Root with local offset (0, 2, 0)
    auto spineRes = skeleton.addBone("Spine", 0, Transform{Vec3(0.0F, 2.0F, 0.0F)});
    TEST_CHECK(spineRes.hasValue());
    TEST_CHECK(spineRes.value() == 1);

    // Bone 2: Head attached to Spine with local offset (0, 1, 0)
    auto headRes = skeleton.addBone("Head", 1, Transform{Vec3(0.0F, 1.0F, 0.0F)});
    TEST_CHECK(headRes.hasValue());
    TEST_CHECK(headRes.value() == 2);

    // Invariant: adding bone with out-of-order or invalid parent index must fail
    auto invalidRes = skeleton.addBone("Invalid", 99, Transform{});
    TEST_CHECK(!invalidRes.hasValue());

    TEST_CHECK(skeleton.getBoneCount() == 3);
    TEST_CHECK(skeleton.findBoneIndex("Spine") == 1);
    TEST_CHECK(skeleton.findBoneIndex("NonExistent") == -1);
    TEST_CHECK(skeleton.getBoneName(2) == "Head");

    // Finalize skeleton to precompute inverse bind matrices
    TEST_CHECK(skeleton.finalize().hasValue());

    // Evaluate bind pose forward kinematics
    std::vector<Transform> localPose(3);
    for (size_t i = 0; i < 3; ++i) {
        localPose[i] = skeleton.getBones()[i].localBindPose;
    }

    std::vector<Transform> modelPose(3);
    TEST_CHECK(skeleton.evaluateModelPose(localPose, modelPose).hasValue());

    // Root should be at (0, 1, 0)
    TEST_CHECK(std::abs(modelPose[0].position.y - 1.0F) < 1e-4F);
    // Spine should be at (0, 1 + 2 = 3, 0)
    TEST_CHECK(std::abs(modelPose[1].position.y - 3.0F) < 1e-4F);
    // Head should be at (0, 3 + 1 = 4, 0)
    TEST_CHECK(std::abs(modelPose[2].position.y - 4.0F) < 1e-4F);

    // Evaluate skinning matrices in rest pose -> all should be identity matrices!
    std::vector<Mat4> skinningMatrices(3);
    TEST_CHECK(skeleton.evaluateSkinningMatrices(modelPose, skinningMatrices).hasValue());

    for (size_t i = 0; i < 3; ++i) {
        // Diagonal should be 1.0, off-diagonal 0.0
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                const float expected = (row == col) ? 1.0F : 0.0F;
                TEST_CHECK(std::abs(skinningMatrices[i].at(row, col) - expected) < 1e-4F);
            }
        }
    }

    return 0;
}

static int testAnimationSamplerInterpolation() {
    // 1. Translation LERP
    std::vector<Keyframe<Vec3>> posKeys = {
        {.timeSeconds = 0.0F, .value = Vec3(0.0F, 0.0F, 0.0F)},
        {.timeSeconds = 1.0F, .value = Vec3(10.0F, 20.0F, 30.0F)},
    };

    auto p0 = AnimationSampler::sampleTranslation(posKeys, 0.0F);
    TEST_CHECK(p0.isNearlyEqual(Vec3(0.0F, 0.0F, 0.0F)));

    auto pMid = AnimationSampler::sampleTranslation(posKeys, 0.5F);
    TEST_CHECK(pMid.isNearlyEqual(Vec3(5.0F, 10.0F, 15.0F)));

    auto p1 = AnimationSampler::sampleTranslation(posKeys, 1.0F);
    TEST_CHECK(p1.isNearlyEqual(Vec3(10.0F, 20.0F, 30.0F)));

    // Boundary clamping
    auto pBefore = AnimationSampler::sampleTranslation(posKeys, -0.5F);
    TEST_CHECK(pBefore.isNearlyEqual(Vec3(0.0F, 0.0F, 0.0F)));

    auto pAfter = AnimationSampler::sampleTranslation(posKeys, 2.0F);
    TEST_CHECK(pAfter.isNearlyEqual(Vec3(10.0F, 20.0F, 30.0F)));

    // 2. Rotation SLERP
    const Quat q0 = Quat::identity();
    const Quat q90 = Quat::fromAxisAngle(Vec3::unitY(), 1.5707963F); // 90 deg around Y

    std::vector<Keyframe<Quat>> rotKeys = {
        {.timeSeconds = 0.0F, .value = q0},
        {.timeSeconds = 1.0F, .value = q90},
    };

    auto qMid = AnimationSampler::sampleRotation(rotKeys, 0.5F);
    // At t=0.5, rotation should be 45 degrees around Y
    const Quat q45 = Quat::fromAxisAngle(Vec3::unitY(), 0.78539816F);
    TEST_CHECK(std::abs(std::abs(qMid.dot(q45)) - 1.0F) < 1e-3F);

    return 0;
}

static int testAnimationControllerPlaybackAndBlending() {
    Skeleton skeleton;
    (void)skeleton.addBone("Root", kInvalidBoneIndex, Transform{Vec3(0.0F, 0.0F, 0.0F)});
    (void)skeleton.finalize();

    // Clip A: Moves Root to (4, 0, 0) over 2.0s
    AnimationClip clipA("Walk", 2.0F);
    BoneTrack trackA;
    trackA.boneIndex = 0;
    trackA.translationKeys = {
        {.timeSeconds = 0.0F, .value = Vec3(0.0F, 0.0F, 0.0F)},
        {.timeSeconds = 2.0F, .value = Vec3(4.0F, 0.0F, 0.0F)},
    };
    clipA.addTrack(trackA);

    // Clip B: Moves Root to (10, 0, 0) over 2.0s
    AnimationClip clipB("Run", 2.0F);
    BoneTrack trackB;
    trackB.boneIndex = 0;
    trackB.translationKeys = {
        {.timeSeconds = 0.0F, .value = Vec3(10.0F, 0.0F, 0.0F)},
        {.timeSeconds = 2.0F, .value = Vec3(10.0F, 0.0F, 0.0F)},
    };
    clipB.addTrack(trackB);

    AnimationController controller;
    controller.play(&clipA, true, 1.0F);
    TEST_CHECK(controller.getState() == PlaybackState::Playing);
    TEST_CHECK(controller.isLooping());

    // Advance 0.5s -> Root position is (1, 0, 0)
    controller.update(0.5F);
    TEST_CHECK(std::abs(controller.getTime() - 0.5F) < 1e-4F);

    std::vector<Transform> pose(1);
    controller.evaluateLocalPose(skeleton, pose);
    TEST_CHECK(std::abs(pose[0].position.x - 1.0F) < 1e-3F);

    // Initiate cross-fade to Clip B over 1.0 second
    controller.crossFade(&clipB, 1.0F, true, 1.0F);
    TEST_CHECK(controller.isBlending());
    TEST_CHECK(controller.getBlendFactor() == 0.0F);

    // Advance 0.5s into blend (50% blend)
    // Clip A is at t=1.0s -> pos = (2, 0, 0)
    // Clip B is at t=0.5s -> pos = (10, 0, 0)
    // Blended pos at alpha=0.5 -> 0.5 * 2 + 0.5 * 10 = 6.0
    controller.update(0.5F);
    TEST_CHECK(std::abs(controller.getBlendFactor() - 0.5F) < 1e-3F);

    controller.evaluateLocalPose(skeleton, pose);
    TEST_CHECK(std::abs(pose[0].position.x - 6.0F) < 1e-3F);

    // Advance another 0.5s -> blend completes
    controller.update(0.5F);
    TEST_CHECK(!controller.isBlending());
    TEST_CHECK(controller.getBlendFactor() == 0.0F);

    controller.evaluateLocalPose(skeleton, pose);
    TEST_CHECK(std::abs(pose[0].position.x - 10.0F) < 1e-3F);

    // Pause / Resume / Stop
    controller.pause();
    TEST_CHECK(controller.getState() == PlaybackState::Paused);
    controller.resume();
    TEST_CHECK(controller.getState() == PlaybackState::Playing);
    controller.stop();
    TEST_CHECK(controller.getState() == PlaybackState::Stopped);
    TEST_CHECK(controller.getTime() == 0.0F);

    return 0;
}

static int testFlecsAnimationSync() {
    world::World world;
    registerAnimationComponents(world);
    registerAnimationSystems(world);

    Skeleton skeleton;
    (void)skeleton.addBone("Root", kInvalidBoneIndex, Transform{Vec3(0.0F, 0.0F, 0.0F)});
    (void)skeleton.addBone("Child", 0, Transform{Vec3(0.0F, 5.0F, 0.0F)});
    (void)skeleton.finalize();

    AnimationClip clip("Anim", 1.0F);
    BoneTrack track;
    track.boneIndex = 0;
    track.translationKeys = {
        {.timeSeconds = 0.0F, .value = Vec3(0.0F, 0.0F, 0.0F)},
        {.timeSeconds = 1.0F, .value = Vec3(4.0F, 0.0F, 0.0F)},
    };
    clip.addTrack(track);

    auto entity = world.createEntity();
    world.setComponent<SkeletonComponent>(entity, SkeletonComponent{.skeleton = &skeleton});

    AnimationComponent animComp{};
    animComp.controller.play(&clip, true, 1.0F);
    world.setComponent<AnimationComponent>(entity, animComp);
    world.setComponent<SkinningPaletteComponent>(entity, SkinningPaletteComponent{});

    // Advance world simulation in Continuous group
    world.setSystemGroupActive(world::SystemGroup::Continuous, true);
    world.progress(0.5F);

    const auto* palette = world.getComponent<SkinningPaletteComponent>(entity);
    TEST_CHECK(palette != nullptr);
    TEST_CHECK(palette->modelPose.size() == 2);
    TEST_CHECK(palette->skinningMatrices.size() == 2);

    // Root evaluated at t=0.5 -> pos.x = 2.0
    TEST_CHECK(std::abs(palette->modelPose[0].position.x - 2.0F) < 1e-3F);
    // Child model pos = Root (2, 0, 0) * Child local (0, 5, 0) = (2, 5, 0)
    TEST_CHECK(std::abs(palette->modelPose[1].position.x - 2.0F) < 1e-3F);
    TEST_CHECK(std::abs(palette->modelPose[1].position.y - 5.0F) < 1e-3F);

    return 0;
}

int main() {
    std::cout << "[AnimTest] Running Skeletal Animation System tests...\n";

    if (testSkeletonHierarchyAndForwardKinematics() != 0) {
        return 1;
    }
    if (testAnimationSamplerInterpolation() != 0) {
        return 1;
    }
    if (testAnimationControllerPlaybackAndBlending() != 0) {
        return 1;
    }
    if (testFlecsAnimationSync() != 0) {
        return 1;
    }

    std::cout << "[AnimTest] All animation tests passed successfully!\n";
    return 0;
}
