/// @file hierarchy_systems.cpp
/// @brief Hierarchical transform propagation system implementation.

#include "world/hierarchy_systems.h"

#include "world/components.h"
#include "world/world.h"

#include <vector>

namespace eng::world {

namespace {

struct QueueItem {
    EntityHandle handle;
    bool parentDirty{false};
    Mat4 parentMatrix{Mat4::identity()};
    Transform parentTransform{};
};

} // namespace

void updateHierarchyTransforms(World& world) {
    // Reusable BFS queue to guarantee zero hot-path heap allocations once warm.
    // Invariant: Simulation progress and continuous systems execute on the main thread.
    static thread_local std::vector<QueueItem> queue;
    queue.clear();

    const auto roots = world.getRootEntities();
    for (EntityHandle root : roots) {
        if (!world.isAlive(root)) {
            continue;
        }

        auto* tc = world.getMutableComponent<TransformComponent>(root);
        auto* wt = world.getMutableComponent<WorldTransformComponent>(root);

        bool rootWasDirty = false;
        Mat4 currentWorldMatrix = Mat4::identity();
        Transform currentWorldTransform{};

        if (tc != nullptr) {
            rootWasDirty = tc->isDirty;

            if (wt == nullptr) {
                // Initialize WorldTransformComponent for newly created root entities
                world.setComponent(
                    root, WorldTransformComponent{tc->transform.toMatrix(), tc->transform});
                wt = world.getMutableComponent<WorldTransformComponent>(root);
                tc = world.getMutableComponent<TransformComponent>(root);
                if (tc != nullptr) {
                    tc->isDirty = false;
                }
                rootWasDirty = true;
            } else if (rootWasDirty) {
                wt->worldMatrix = tc->transform.toMatrix();
                wt->worldTransform = tc->transform;
                tc->isDirty = false;
            }

            currentWorldMatrix = wt ? wt->worldMatrix : tc->transform.toMatrix();
            currentWorldTransform = wt ? wt->worldTransform : tc->transform;
        } else if (wt != nullptr) {
            currentWorldMatrix = wt->worldMatrix;
            currentWorldTransform = wt->worldTransform;
        } else {
            // Root has neither TransformComponent nor WorldTransformComponent: skip hierarchy
            continue;
        }

        // Enqueue direct children of this root
        const auto children = world.getChildren(root);
        for (EntityHandle child : children) {
            queue.push_back(QueueItem{
                .handle = child,
                .parentDirty = rootWasDirty,
                .parentMatrix = currentWorldMatrix,
                .parentTransform = currentWorldTransform,
            });
        }
    }

    // Process BFS queue: Breadth-first ensures parents are evaluated strictly before their children
    size_t head = 0;
    while (head < queue.size()) {
        const QueueItem item = queue[head++];
        if (!world.isAlive(item.handle)) {
            continue;
        }

        auto* tc = world.getMutableComponent<TransformComponent>(item.handle);
        auto* wt = world.getMutableComponent<WorldTransformComponent>(item.handle);

        bool branchDirty = item.parentDirty;
        Mat4 currentWorldMatrix = item.parentMatrix;
        Transform currentWorldTransform = item.parentTransform;

        if (tc != nullptr) {
            if (tc->isDirty) {
                branchDirty = true;
            }

            const Mat4 localMat = tc->transform.toMatrix();
            const Mat4 worldMat = item.parentMatrix * localMat;
            const Transform worldTr = item.parentTransform.combine(tc->transform);

            if (wt == nullptr) {
                // Newly attached child without WorldTransformComponent
                world.setComponent(item.handle, WorldTransformComponent{worldMat, worldTr});
                wt = world.getMutableComponent<WorldTransformComponent>(item.handle);
                tc = world.getMutableComponent<TransformComponent>(item.handle);
                if (tc != nullptr) {
                    tc->isDirty = false;
                }
                branchDirty = true;
            } else if (branchDirty) {
                // Invariant: M_world = M_parent * M_local preserves full matrices including shear
                wt->worldMatrix = worldMat;
                wt->worldTransform = worldTr;
                tc->isDirty = false;
            }

            currentWorldMatrix = wt ? wt->worldMatrix : worldMat;
            currentWorldTransform = wt ? wt->worldTransform : worldTr;
        }

        // Enqueue direct children of this entity
        const auto children = world.getChildren(item.handle);
        for (EntityHandle child : children) {
            queue.push_back(QueueItem{
                .handle = child,
                .parentDirty = branchDirty,
                .parentMatrix = currentWorldMatrix,
                .parentTransform = currentWorldTransform,
            });
        }
    }
}

void registerHierarchySystems(World& world) {
    auto& ecs = world.getRawWorld();

    auto sys = ecs.system<>("HierarchyTransformSystem")
                   .kind(flecs::PreStore)
                   .run([&world](flecs::iter& /*it*/) { updateHierarchyTransforms(world); });

    world.registerSystem(SystemGroup::Continuous, sys);
}

} // namespace eng::world
