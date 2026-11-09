/// @file transform_propagation_test.cpp
/// @brief Comprehensive unit tests for hierarchical transform propagation and dirty invalidation.

#include "core/math.h"
#include "world/components.h"
#include "world/entity.h"
#include "world/hierarchy_systems.h"
#include "world/world.h"

#include <cmath>
#include <iostream>

using namespace eng;
using namespace eng::world;

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << '\n'; \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

static bool isNear(float a, float b, float eps = 1e-3F) {
    return std::abs(a - b) <= eps;
}

static bool isNearVec3(const Vec3& a, const Vec3& b, float eps = 1e-3F) {
    return isNear(a.x, b.x, eps) && isNear(a.y, b.y, eps) && isNear(a.z, b.z, eps);
}

// 1. Root world transform initialization
static int testRootTransformInitialization() {
    World world;
    EntityHandle root = world.createEntity("Root");
    world.setComponent(root, TransformComponent{Transform{Vec3{1.0F, 2.0F, 3.0F}}});

    updateHierarchyTransforms(world);

    const auto* wt = world.getComponent<WorldTransformComponent>(root);
    TEST_CHECK(wt != nullptr);
    TEST_CHECK(isNearVec3(wt->worldTransform.position, Vec3{1.0F, 2.0F, 3.0F}));
    TEST_CHECK(isNear(wt->worldMatrix.m[12], 1.0F));
    TEST_CHECK(isNear(wt->worldMatrix.m[13], 2.0F));
    TEST_CHECK(isNear(wt->worldMatrix.m[14], 3.0F));

    return 0;
}

// 2. Parent translation propagation
static int testParentTranslationPropagation() {
    World world;
    EntityHandle parent = world.createEntity("Parent");
    EntityHandle child = world.createEntity("Child");

    world.setComponent(parent, TransformComponent{Transform{Vec3{10.0F, 0.0F, 0.0F}}});
    world.setComponent(child, TransformComponent{Transform{Vec3{0.0F, 5.0F, 0.0F}}});
    TEST_CHECK(world.setParent(child, parent, false));

    updateHierarchyTransforms(world);

    const auto* childWt = world.getComponent<WorldTransformComponent>(child);
    TEST_CHECK(childWt != nullptr);
    TEST_CHECK(isNearVec3(childWt->worldTransform.position, Vec3{10.0F, 5.0F, 0.0F}));
    TEST_CHECK(isNear(childWt->worldMatrix.m[12], 10.0F));
    TEST_CHECK(isNear(childWt->worldMatrix.m[13], 5.0F));
    TEST_CHECK(isNear(childWt->worldMatrix.m[14], 0.0F));

    return 0;
}

// 3. Parent rotation propagation
static int testParentRotationPropagation() {
    World world;
    EntityHandle parent = world.createEntity("Parent");
    EntityHandle child = world.createEntity("Child");

    // Parent rotated 90 degrees around Y axis (pi/2 radians)
    Quat rotY90 = Quat::fromAxisAngle(Vec3::unitY(), 1.5707963F);
    world.setComponent(parent, TransformComponent{Transform{Vec3::zero(), rotY90, Vec3::one()}});
    // Child translated 5 units forward along local Z
    world.setComponent(child, TransformComponent{Transform{Vec3{0.0F, 0.0F, 5.0F}}});
    TEST_CHECK(world.setParent(child, parent, false));

    updateHierarchyTransforms(world);

    const auto* childWt = world.getComponent<WorldTransformComponent>(child);
    TEST_CHECK(childWt != nullptr);
    // In right-handed coords, rotating +Z by 90 deg around +Y points to +X
    TEST_CHECK(isNearVec3(childWt->worldTransform.position, Vec3{5.0F, 0.0F, 0.0F}));

    return 0;
}

// 4. Parent scale propagation
static int testParentScalePropagation() {
    World world;
    EntityHandle parent = world.createEntity("Parent");
    EntityHandle child = world.createEntity("Child");

    world.setComponent(parent, TransformComponent{Transform{Vec3::zero(), Quat::identity(),
                                                            Vec3{2.0F, 2.0F, 2.0F}}});
    world.setComponent(child, TransformComponent{Transform{Vec3{1.0F, 2.0F, 3.0F}, Quat::identity(),
                                                           Vec3{0.5F, 0.5F, 0.5F}}});
    TEST_CHECK(world.setParent(child, parent, false));

    updateHierarchyTransforms(world);

    const auto* childWt = world.getComponent<WorldTransformComponent>(child);
    TEST_CHECK(childWt != nullptr);
    TEST_CHECK(isNearVec3(childWt->worldTransform.position, Vec3{2.0F, 4.0F, 6.0F}));
    TEST_CHECK(isNearVec3(childWt->worldTransform.scale, Vec3{1.0F, 1.0F, 1.0F}));

    return 0;
}

// 5. Three-level nested hierarchy
static int testThreeLevelNestedHierarchy() {
    World world;
    EntityHandle root = world.createEntity("Root");
    EntityHandle child = world.createEntity("Child");
    EntityHandle grandchild = world.createEntity("Grandchild");

    world.setComponent(root, TransformComponent{Transform{Vec3{10.0F, 0.0F, 0.0F}}});
    world.setComponent(child, TransformComponent{Transform{Vec3{0.0F, 5.0F, 0.0F}}});
    world.setComponent(grandchild, TransformComponent{Transform{Vec3{0.0F, 0.0F, 2.0F}}});

    TEST_CHECK(world.setParent(child, root, false));
    TEST_CHECK(world.setParent(grandchild, child, false));

    updateHierarchyTransforms(world);

    const auto* gcWt = world.getComponent<WorldTransformComponent>(grandchild);
    TEST_CHECK(gcWt != nullptr);
    TEST_CHECK(isNearVec3(gcWt->worldTransform.position, Vec3{10.0F, 5.0F, 2.0F}));

    return 0;
}

// 6. Local child transform changes
static int testLocalChildTransformChanges() {
    World world;
    EntityHandle parent = world.createEntity("Parent");
    EntityHandle child = world.createEntity("Child");

    world.setComponent(parent, TransformComponent{Transform{Vec3{10.0F, 0.0F, 0.0F}}});
    world.setComponent(child, TransformComponent{Transform{Vec3{0.0F, 1.0F, 0.0F}}});
    TEST_CHECK(world.setParent(child, parent, false));

    updateHierarchyTransforms(world);

    // Now modify child local transform and mark dirty
    auto* childTc = world.getMutableComponent<TransformComponent>(child);
    childTc->transform.position = Vec3{0.0F, 10.0F, 0.0F};
    childTc->isDirty = true;

    updateHierarchyTransforms(world);

    const auto* childWt = world.getComponent<WorldTransformComponent>(child);
    TEST_CHECK(childWt != nullptr);
    TEST_CHECK(isNearVec3(childWt->worldTransform.position, Vec3{10.0F, 10.0F, 0.0F}));

    // Parent remains unchanged
    const auto* parentWt = world.getComponent<WorldTransformComponent>(parent);
    TEST_CHECK(parentWt != nullptr);
    TEST_CHECK(isNearVec3(parentWt->worldTransform.position, Vec3{10.0F, 0.0F, 0.0F}));

    return 0;
}

// 7. Dirty propagation to descendants
static int testDirtyPropagationToDescendants() {
    World world;
    EntityHandle root = world.createEntity("Root");
    EntityHandle child = world.createEntity("Child");
    EntityHandle grandchild = world.createEntity("Grandchild");

    world.setComponent(root, TransformComponent{Transform{Vec3{1.0F, 0.0F, 0.0F}}});
    world.setComponent(child, TransformComponent{Transform{Vec3{0.0F, 2.0F, 0.0F}}});
    world.setComponent(grandchild, TransformComponent{Transform{Vec3{0.0F, 0.0F, 3.0F}}});

    world.setParent(child, root, false);
    world.setParent(grandchild, child, false);

    updateHierarchyTransforms(world);

    // Modify only root; child and grandchild have isDirty == false
    auto* rootTc = world.getMutableComponent<TransformComponent>(root);
    rootTc->transform.position = Vec3{10.0F, 0.0F, 0.0F};
    rootTc->isDirty = true;

    TEST_CHECK(!world.getComponent<TransformComponent>(child)->isDirty);
    TEST_CHECK(!world.getComponent<TransformComponent>(grandchild)->isDirty);

    updateHierarchyTransforms(world);

    // Downward invalidation must recompute child and grandchild
    const auto* childWt = world.getComponent<WorldTransformComponent>(child);
    const auto* gcWt = world.getComponent<WorldTransformComponent>(grandchild);
    TEST_CHECK(isNearVec3(childWt->worldTransform.position, Vec3{10.0F, 2.0F, 0.0F}));
    TEST_CHECK(isNearVec3(gcWt->worldTransform.position, Vec3{10.0F, 2.0F, 3.0F}));

    return 0;
}

// 8. Reparenting invalidation with preserved world transform
static int testReparentingPreserveWorldTransform() {
    World world;
    EntityHandle p1 = world.createEntity("Parent1");
    EntityHandle p2 = world.createEntity("Parent2");
    EntityHandle child = world.createEntity("Child");

    world.setComponent(p1, TransformComponent{Transform{Vec3{10.0F, 0.0F, 0.0F}}});
    world.setComponent(p2, TransformComponent{Transform{Vec3{30.0F, 0.0F, 0.0F}}});
    world.setComponent(child, TransformComponent{Transform{Vec3{5.0F, 0.0F, 0.0F}}});

    // Parent to P1: child world position is 15
    TEST_CHECK(world.setParent(child, p1, false));
    updateHierarchyTransforms(world);

    const auto* wt1 = world.getComponent<WorldTransformComponent>(child);
    TEST_CHECK(isNearVec3(wt1->worldTransform.position, Vec3{15.0F, 0.0F, 0.0F}));

    // Reparent under P2 with preserveWorldTransform == true
    TEST_CHECK(world.setParent(child, p2, true));
    updateHierarchyTransforms(world);

    // World position must still be 15
    const auto* wt2 = world.getComponent<WorldTransformComponent>(child);
    TEST_CHECK(isNearVec3(wt2->worldTransform.position, Vec3{15.0F, 0.0F, 0.0F}));

    // Local position relative to P2 (at 30) should now be -15
    const auto* tc2 = world.getComponent<TransformComponent>(child);
    TEST_CHECK(isNear(tc2->transform.position.x, -15.0F));

    return 0;
}

// 9. Detaching an entity to root
static int testDetachingEntityToRoot() {
    World world;
    EntityHandle parent = world.createEntity("Parent");
    EntityHandle child = world.createEntity("Child");

    world.setComponent(parent, TransformComponent{Transform{Vec3{10.0F, 20.0F, 30.0F}}});
    world.setComponent(child, TransformComponent{Transform{Vec3{1.0F, 2.0F, 3.0F}}});

    TEST_CHECK(world.setParent(child, parent, false));
    updateHierarchyTransforms(world);

    // Detach to root with preserveWorldTransform == true
    TEST_CHECK(world.setParent(child, EntityHandle{}, true));
    updateHierarchyTransforms(world);

    TEST_CHECK(!world.getParent(child).isValid());
    const auto* childWt = world.getComponent<WorldTransformComponent>(child);
    TEST_CHECK(isNearVec3(childWt->worldTransform.position, Vec3{11.0F, 22.0F, 33.0F}));

    return 0;
}

// 10. Non-uniform scale and rotated-child matrix correctness
static int testNonUniformScaleAndRotatedChild() {
    World world;
    EntityHandle parent = world.createEntity("Parent");
    EntityHandle child = world.createEntity("Child");

    // Parent has non-uniform scale (2.0 on X, 1.0 on Y, 1.0 on Z)
    world.setComponent(parent, TransformComponent{Transform{Vec3::zero(), Quat::identity(),
                                                            Vec3{2.0F, 1.0F, 1.0F}}});

    // Child is rotated 45 degrees around Z
    Quat rotZ45 = Quat::fromAxisAngle(Vec3::unitZ(), 0.7853982F);
    world.setComponent(child,
                       TransformComponent{Transform{Vec3{0.0F, 1.0F, 0.0F}, rotZ45, Vec3::one()}});

    TEST_CHECK(world.setParent(child, parent, false));
    updateHierarchyTransforms(world);

    const auto* childWt = world.getComponent<WorldTransformComponent>(child);
    TEST_CHECK(childWt != nullptr);

    // Matrix ground truth: M_world = M_parent * M_child
    Mat4 parentMat = Mat4::fromTransform(Vec3::zero(), Quat::identity(), Vec3{2.0F, 1.0F, 1.0F});
    Mat4 childMat = Mat4::fromTransform(Vec3{0.0F, 1.0F, 0.0F}, rotZ45, Vec3::one());
    Mat4 expectedWorldMat = parentMat * childMat;

    for (size_t i = 0; i < 16; ++i) {
        TEST_CHECK(isNear(childWt->worldMatrix.m[i], expectedWorldMat.m[i], 1e-4F));
    }

    return 0;
}

// 11. Newly created entity initialization
static int testNewlyCreatedEntityInitialization() {
    World world;
    EntityHandle root = world.createEntity("Root");
    world.setComponent(root, TransformComponent{Transform{Vec3{5.0F, 5.0F, 5.0F}}});

    // Run tick 1
    updateHierarchyTransforms(world);

    // Dynamically create child in tick 2
    EntityHandle child = world.createEntity("Child");
    world.setComponent(child, TransformComponent{Transform{Vec3{1.0F, 0.0F, 0.0F}}});
    TEST_CHECK(world.setParent(child, root, false));

    // Run tick 2
    updateHierarchyTransforms(world);

    const auto* childWt = world.getComponent<WorldTransformComponent>(child);
    TEST_CHECK(childWt != nullptr);
    TEST_CHECK(isNearVec3(childWt->worldTransform.position, Vec3{6.0F, 5.0F, 5.0F}));

    return 0;
}

// 12. Invalid or missing transform handling
static int testInvalidOrMissingTransformHandling() {
    World world;
    EntityHandle noTransform = world.createEntity("NoTransform");
    EntityHandle withTransform = world.createEntity("WithTransform");

    world.setComponent(withTransform, TransformComponent{Transform{Vec3{3.0F, 0.0F, 0.0F}}});
    // Parenting entity without TransformComponent must not crash
    TEST_CHECK(world.setParent(noTransform, withTransform, false));

    updateHierarchyTransforms(world);

    TEST_CHECK(world.getComponent<WorldTransformComponent>(noTransform) == nullptr);
    const auto* wt = world.getComponent<WorldTransformComponent>(withTransform);
    TEST_CHECK(wt != nullptr);
    TEST_CHECK(isNearVec3(wt->worldTransform.position, Vec3{3.0F, 0.0F, 0.0F}));

    return 0;
}

// 13. System execution during normal world simulation progress
static int testHierarchySystemDuringSimulationProgress() {
    World world;
    EntityHandle root = world.createEntity("Root");
    EntityHandle child = world.createEntity("Child");

    world.setComponent(root, TransformComponent{Transform{Vec3{100.0F, 0.0F, 0.0F}}});
    world.setComponent(child, TransformComponent{Transform{Vec3{0.0F, 50.0F, 0.0F}}});
    TEST_CHECK(world.setParent(child, root, false));

    // Progress world simulation; registered Continuous HierarchyTransformSystem must execute
    world.progress(0.016F);

    const auto* childWt = world.getComponent<WorldTransformComponent>(child);
    TEST_CHECK(childWt != nullptr);
    TEST_CHECK(isNearVec3(childWt->worldTransform.position, Vec3{100.0F, 50.0F, 0.0F}));

    // Modify root and step simulation again
    auto* rootTc = world.getMutableComponent<TransformComponent>(root);
    rootTc->transform.position = Vec3{200.0F, 0.0F, 0.0F};
    rootTc->isDirty = true;

    world.progress(0.016F);

    TEST_CHECK(isNearVec3(childWt->worldTransform.position, Vec3{200.0F, 50.0F, 0.0F}));

    return 0;
}

int main() {
    std::cout << "[TransformPropagationTest] Running Phase 2 transform evaluation tests...\n";

    if (testRootTransformInitialization() != 0)
        return 1;
    if (testParentTranslationPropagation() != 0)
        return 1;
    if (testParentRotationPropagation() != 0)
        return 1;
    if (testParentScalePropagation() != 0)
        return 1;
    if (testThreeLevelNestedHierarchy() != 0)
        return 1;
    if (testLocalChildTransformChanges() != 0)
        return 1;
    if (testDirtyPropagationToDescendants() != 0)
        return 1;
    if (testReparentingPreserveWorldTransform() != 0)
        return 1;
    if (testDetachingEntityToRoot() != 0)
        return 1;
    if (testNonUniformScaleAndRotatedChild() != 0)
        return 1;
    if (testNewlyCreatedEntityInitialization() != 0)
        return 1;
    if (testInvalidOrMissingTransformHandling() != 0)
        return 1;
    if (testHierarchySystemDuringSimulationProgress() != 0)
        return 1;

    std::cout << "[TransformPropagationTest] All Phase 2 transform propagation tests passed "
                 "successfully!\n";
    return 0;
}
