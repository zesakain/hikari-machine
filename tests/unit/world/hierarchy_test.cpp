/// @file hierarchy_test.cpp
/// @brief Unit tests for Flecs ECS entity hierarchy, TransformComponent, and parenting validation.

#include "core/math.h"
#include "physics/physics_components.h"
#include "world/components.h"
#include "world/entity.h"
#include "world/hierarchy_systems.h"
#include "world/world.h"

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace eng;
using namespace eng::world;
using namespace eng::physics;

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << '\n'; \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

static int testRootEntityCreation() {
    World world;

    EntityHandle e1 = world.createEntity("Root1");
    EntityHandle e2 = world.createEntity("Root2");
    world.setComponent(e1, TransformComponent{Transform{Vec3{1.0F, 0.0F, 0.0F}}});
    world.setComponent(e2, TransformComponent{Transform{Vec3{2.0F, 0.0F, 0.0F}}});

    TEST_CHECK(world.isAlive(e1));
    TEST_CHECK(world.isAlive(e2));

    // Initially, neither entity has a parent
    TEST_CHECK(!world.getParent(e1).isValid());
    TEST_CHECK(!world.getParent(e2).isValid());

    auto roots = world.getRootEntities();
    TEST_CHECK(roots.size() == 2);
    TEST_CHECK(std::find(roots.begin(), roots.end(), e1) != roots.end());
    TEST_CHECK(std::find(roots.begin(), roots.end(), e2) != roots.end());

    return 0;
}

static int testParentChildRelationship() {
    World world;

    EntityHandle parent = world.createEntity("Parent");
    EntityHandle child1 = world.createEntity("Child1");
    EntityHandle child2 = world.createEntity("Child2");

    world.setComponent(parent, TransformComponent{});
    world.setComponent(child1, TransformComponent{Transform{Vec3{0.0F, 1.0F, 0.0F}}});
    world.setComponent(child2, TransformComponent{Transform{Vec3{0.0F, 2.0F, 0.0F}}});

    TEST_CHECK(world.setParent(child1, parent));
    TEST_CHECK(world.setParent(child2, parent));

    TEST_CHECK(world.getParent(child1) == parent);
    TEST_CHECK(world.getParent(child2) == parent);

    auto children = world.getChildren(parent);
    TEST_CHECK(children.size() == 2);
    TEST_CHECK(std::find(children.begin(), children.end(), child1) != children.end());
    TEST_CHECK(std::find(children.begin(), children.end(), child2) != children.end());

    // Only parent is a root entity now
    auto roots = world.getRootEntities();
    TEST_CHECK(roots.size() == 1);
    TEST_CHECK(roots[0] == parent);

    // Detach child1 to root
    TEST_CHECK(world.setParent(child1, EntityHandle{}));
    TEST_CHECK(!world.getParent(child1).isValid());

    children = world.getChildren(parent);
    TEST_CHECK(children.size() == 1);
    TEST_CHECK(children[0] == child2);

    roots = world.getRootEntities();
    TEST_CHECK(roots.size() == 2);

    return 0;
}

static int testNestedHierarchy() {
    World world;

    EntityHandle root = world.createEntity("Root");
    EntityHandle child = world.createEntity("Child");
    EntityHandle grandchild = world.createEntity("Grandchild");

    TEST_CHECK(world.setParent(child, root));
    TEST_CHECK(world.setParent(grandchild, child));

    TEST_CHECK(world.getParent(grandchild) == child);
    TEST_CHECK(world.getParent(child) == root);
    TEST_CHECK(!world.getParent(root).isValid());

    // Ancestor checks
    TEST_CHECK(world.isDescendantOf(grandchild, child));
    TEST_CHECK(world.isDescendantOf(grandchild, root));
    TEST_CHECK(world.isDescendantOf(child, root));

    // Negative ancestor checks
    TEST_CHECK(!world.isDescendantOf(root, child));
    TEST_CHECK(!world.isDescendantOf(root, grandchild));
    TEST_CHECK(!world.isDescendantOf(child, grandchild));
    TEST_CHECK(!world.isDescendantOf(root, root));

    return 0;
}

static int testCycleRejection() {
    World world;

    EntityHandle a = world.createEntity("A");
    EntityHandle b = world.createEntity("B");
    EntityHandle c = world.createEntity("C");

    TEST_CHECK(world.setParent(b, a));
    TEST_CHECK(world.setParent(c, b));

    // Self-parenting: A -> A
    TEST_CHECK(!world.setParent(a, a));

    // Direct cycle: A -> B where B is child of A
    TEST_CHECK(!world.setParent(a, b));

    // Indirect cycle: A -> C where C is grandchild of A
    TEST_CHECK(!world.setParent(a, c));

    // Verify hierarchy was unchanged by the rejected operations
    TEST_CHECK(!world.getParent(a).isValid());
    TEST_CHECK(world.getParent(b) == a);
    TEST_CHECK(world.getParent(c) == b);

    return 0;
}

static int testInvalidEntityHandling() {
    World world;

    EntityHandle valid = world.createEntity("Valid");
    EntityHandle invalid{};

    // An invalid child cannot be parented
    TEST_CHECK(!world.setParent(invalid, valid));

    // Passing invalid handle as parent detaches child to root (returns true)
    TEST_CHECK(world.setParent(valid, invalid));
    TEST_CHECK(!world.getParent(valid).isValid());

    // Test dead entity
    EntityHandle dead = world.createEntity("Dead");
    world.destroyEntity(dead);
    TEST_CHECK(!world.isAlive(dead));

    // Neither child nor parent can be dead
    TEST_CHECK(!world.setParent(valid, dead));
    TEST_CHECK(!world.setParent(dead, valid));
    TEST_CHECK(!world.getParent(dead).isValid());
    TEST_CHECK(world.getChildren(dead).empty());
    TEST_CHECK(!world.isDescendantOf(dead, valid));
    TEST_CHECK(!world.isDescendantOf(valid, dead));

    return 0;
}

static int testCascadingDestruction() {
    World world;

    EntityHandle root = world.createEntity("Root");
    EntityHandle c1 = world.createEntity("C1");
    EntityHandle c2 = world.createEntity("C2");
    EntityHandle gc = world.createEntity("GC");

    world.setParent(c1, root);
    world.setParent(c2, root);
    world.setParent(gc, c1);

    TEST_CHECK(world.getAllEntities().size() == 4);

    // Destroy root entity
    world.destroyEntity(root);

    // All descendants must be dead
    TEST_CHECK(!world.isAlive(root));
    TEST_CHECK(!world.isAlive(c1));
    TEST_CHECK(!world.isAlive(c2));
    TEST_CHECK(!world.isAlive(gc));

    // activeEntities_ must have zero stale handles
    auto alive = world.getAllEntities();
    TEST_CHECK(alive.empty());
    TEST_CHECK(world.getRootEntities().empty());

    return 0;
}

static int testDynamicRigidBodyParentingRejection() {
    World world;
    registerPhysicsComponents(world);

    EntityHandle parent = world.createEntity("Parent");
    EntityHandle dynamicChild = world.createEntity("DynamicChild");
    EntityHandle kinematicChild = world.createEntity("KinematicChild");
    EntityHandle staticChild = world.createEntity("StaticChild");

    world.setComponent(dynamicChild, RigidBodyComponent{
                                         .bodyHandle = {},
                                         .motionType = MotionType::Dynamic,
                                     });
    world.setComponent(kinematicChild, RigidBodyComponent{
                                           .bodyHandle = {},
                                           .motionType = MotionType::Kinematic,
                                       });
    world.setComponent(staticChild, RigidBodyComponent{
                                        .bodyHandle = {},
                                        .motionType = MotionType::Static,
                                    });

    // 1. Dynamic body cannot be parented under another entity
    TEST_CHECK(!world.setParent(dynamicChild, parent));
    TEST_CHECK(!world.getParent(dynamicChild).isValid());

    // 2. Kinematic and Static bodies CAN participate in hierarchy
    TEST_CHECK(world.setParent(kinematicChild, parent));
    TEST_CHECK(world.getParent(kinematicChild) == parent);

    TEST_CHECK(world.setParent(staticChild, parent));
    TEST_CHECK(world.getParent(staticChild) == parent);

    // 3. If an entity is already a child and receives a Dynamic RigidBodyComponent, it is detached
    // to root preserving its exact world-space placement
    world.setComponent(parent, TransformComponent{Transform{Vec3{10.0F, 0.0F, 0.0F}}});

    EntityHandle newChild = world.createEntity("NewChild");
    world.setComponent(newChild, TransformComponent{Transform{Vec3{5.0F, 0.0F, 0.0F}}});
    TEST_CHECK(world.setParent(newChild, parent, false));
    TEST_CHECK(world.getParent(newChild) == parent);

    // Compute world transforms: parent at 10, child at 15
    updateHierarchyTransforms(world);

    // Receiving Dynamic RigidBodyComponent detaches it to root
    world.setComponent(newChild, RigidBodyComponent{
                                     .bodyHandle = {},
                                     .motionType = MotionType::Dynamic,
                                 });

    // Flecs observer on Dynamic RigidBodyComponent detaches it to root
    TEST_CHECK(!world.getParent(newChild).isValid());

    // Coordinate preservation: local position must now be 15.0F, NOT snapped to 5.0F
    const auto* tc = world.getComponent<TransformComponent>(newChild);
    TEST_CHECK(tc != nullptr);
    TEST_CHECK(std::abs(tc->transform.position.x - 15.0F) < 1e-4F);

    return 0;
}

int main() {
    std::cout
        << "[HierarchyTest] Running Phase 1 ECS Hierarchy and Transform Foundation tests...\n";

    if (testRootEntityCreation() != 0) {
        return 1;
    }
    if (testParentChildRelationship() != 0) {
        return 1;
    }
    if (testNestedHierarchy() != 0) {
        return 1;
    }
    if (testCycleRejection() != 0) {
        return 1;
    }
    if (testInvalidEntityHandling() != 0) {
        return 1;
    }
    if (testCascadingDestruction() != 0) {
        return 1;
    }
    if (testDynamicRigidBodyParentingRejection() != 0) {
        return 1;
    }

    std::cout << "[HierarchyTest] All Phase 1 hierarchy tests passed successfully!\n";
    return 0;
}
