/// @file animation_components.cpp
/// @brief Registration of animation ECS components and evaluation systems.

#include "anim/animation_components.h"

namespace eng::anim {

void registerAnimationComponents(world::World& world) {
    auto& ecs = world.getRawWorld();

    ecs.component<SkeletonComponent>("SkeletonComponent");
    ecs.component<AnimationComponent>("AnimationComponent");
    ecs.component<SkinningPaletteComponent>("SkinningPaletteComponent");
}

void registerAnimationSystems(world::World& world) {
    auto& ecs = world.getRawWorld();

    // 1. Advance playback time and blending
    auto updateSys = ecs.system<AnimationComponent>("AnimationUpdate")
                         .kind(flecs::OnUpdate)
                         .each([](flecs::iter& it, size_t /*index*/, AnimationComponent& anim) {
                             anim.controller.update(it.delta_time());
                         });
    world.registerSystem(world::SystemGroup::Continuous, updateSys);

    // 2. Evaluate skeletal poses and skinning matrices
    auto evalSys =
        ecs.system<const SkeletonComponent, const AnimationComponent, SkinningPaletteComponent>(
               "AnimationPoseEvaluation")
            .kind(flecs::PostUpdate)
            .each([](const SkeletonComponent& skelComp, const AnimationComponent& animComp,
                     SkinningPaletteComponent& palette) {
                if (skelComp.skeleton == nullptr) {
                    return;
                }
                const auto boneCount = skelComp.skeleton->getBoneCount();
                if (boneCount == 0) {
                    return;
                }

                if (palette.localPose.size() != boneCount) {
                    palette.localPose.resize(boneCount);
                }
                if (palette.modelPose.size() != boneCount) {
                    palette.modelPose.resize(boneCount);
                }
                if (palette.skinningMatrices.size() != boneCount) {
                    palette.skinningMatrices.resize(boneCount);
                }

                animComp.controller.evaluateLocalPose(*skelComp.skeleton, palette.localPose);
                (void)skelComp.skeleton->evaluateModelPose(palette.localPose, palette.modelPose);
                (void)skelComp.skeleton->evaluateSkinningMatrices(palette.modelPose,
                                                                  palette.skinningMatrices);
            });
    world.registerSystem(world::SystemGroup::Continuous, evalSys);
}

} // namespace eng::anim
