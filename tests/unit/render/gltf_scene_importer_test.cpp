/// @file gltf_scene_importer_test.cpp
/// @brief Unit tests for GltfSceneImporter — hierarchical glTF 2.0 scene ingestion (Milestone 8B.6
/// Phase 3).
///
/// Verified Scenarios:
///   1. MultiNodeEntityCount: Node graph ingestion produces matching entity counts.
///   2. ParentChildRelationships: Flecs ChildOf hierarchy matches glTF node graph.
///   3. LocalTransforms: Extracted TRS transforms remain in local space without vertex baking.
///   4. WorldTransformPropagation: WorldTransformComponent propagates across hierarchy after
///   update.
///   5. FiveLevelNesting: 5-level deep hierarchy propagates correctly to analytical ground truth.
///   6. LocatorNodesPreserved: Empty locator/pivot nodes exist as transform entities without mesh
///   components.
///   7. GeometryInstancing: Multiple nodes referencing the same mesh share identical meshAsset IDs.
///   8. MultiPrimitiveSplits: Multi-primitive meshes spawn child entities named
///   {NodeName}_Primitive_{i}.
///   9. NonexistentFile: Invalid/missing file path returns failure cleanly.

#include "render/gltf_scene_importer.h"
#include "render/mesh_component.h"
#include "world/components.h"
#include "world/entity.h"
#include "world/hierarchy_systems.h"
#include "world/world.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

using namespace eng;
using namespace eng::world;
using namespace eng::render;

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << '\n'; \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

static const std::string kFixturesDir = "tests/fixtures/gltf/";

static bool isNear(float a, float b, float eps = 1e-3F) {
    return std::abs(a - b) <= eps;
}

static bool isNearVec3(const Vec3& a, const Vec3& b, float eps = 1e-3F) {
    return isNear(a.x, b.x, eps) && isNear(a.y, b.y, eps) && isNear(a.z, b.z, eps);
}

// ---------------------------------------------------------------------------
// Scenario 1: MultiNodeEntityCount
// hierarchy_test.gltf has 4 nodes: Root, Child1, Grandchild1, Child2.
// ---------------------------------------------------------------------------
static int testMultiNodeEntityCount() {
    World world;
    auto result = GltfSceneImporter::importScene(kFixturesDir + "hierarchy_test.gltf", world);

    TEST_CHECK(result.success);
    TEST_CHECK(result.nodeCount == 4);
    TEST_CHECK(result.nodes.size() == 4);
    TEST_CHECK(result.rootEntity.isValid());

    for (const auto& node : result.nodes) {
        TEST_CHECK(world.isAlive(node.entity));
    }

    std::cout << "[PASS] testMultiNodeEntityCount\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Scenario 2: ParentChildRelationships
// hierarchy_test.gltf: Root -> Child1 -> Grandchild1, Root -> Child2.
// ---------------------------------------------------------------------------
static int testParentChildRelationships() {
    World world;
    auto result = GltfSceneImporter::importScene(kFixturesDir + "hierarchy_test.gltf", world);
    TEST_CHECK(result.success);
    TEST_CHECK(result.nodes.size() == 4);

    const EntityHandle root = result.nodes[0].entity;
    const EntityHandle child1 = result.nodes[1].entity;
    const EntityHandle grandchild1 = result.nodes[2].entity;
    const EntityHandle child2 = result.nodes[3].entity;

    TEST_CHECK(!world.getParent(root).isValid());
    TEST_CHECK(world.getParent(child1) == root);
    TEST_CHECK(world.getParent(child2) == root);
    TEST_CHECK(world.getParent(grandchild1) == child1);

    auto rootChildren = world.getChildren(root);
    TEST_CHECK(rootChildren.size() == 2);
    TEST_CHECK(std::find(rootChildren.begin(), rootChildren.end(), child1) != rootChildren.end());
    TEST_CHECK(std::find(rootChildren.begin(), rootChildren.end(), child2) != rootChildren.end());

    std::cout << "[PASS] testParentChildRelationships\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Scenario 3: LocalTransforms
// Local TRS must remain unflattened.
// Root = (0,0,0), Child1 = (1,0,0), Grandchild1 = (0,1,0), Child2 = (2,0,0)
// ---------------------------------------------------------------------------
static int testLocalTransforms() {
    World world;
    auto result = GltfSceneImporter::importScene(kFixturesDir + "hierarchy_test.gltf", world);
    TEST_CHECK(result.success);

    const EntityHandle root = result.nodes[0].entity;
    const EntityHandle child1 = result.nodes[1].entity;
    const EntityHandle grandchild1 = result.nodes[2].entity;
    const EntityHandle child2 = result.nodes[3].entity;

    const auto* tcRoot = world.getComponent<TransformComponent>(root);
    TEST_CHECK(tcRoot != nullptr);
    TEST_CHECK(isNearVec3(tcRoot->transform.position, Vec3{0.0F, 0.0F, 0.0F}));

    const auto* tcChild1 = world.getComponent<TransformComponent>(child1);
    TEST_CHECK(tcChild1 != nullptr);
    TEST_CHECK(isNearVec3(tcChild1->transform.position, Vec3{1.0F, 0.0F, 0.0F}));

    const auto* tcGrandchild1 = world.getComponent<TransformComponent>(grandchild1);
    TEST_CHECK(tcGrandchild1 != nullptr);
    TEST_CHECK(isNearVec3(tcGrandchild1->transform.position, Vec3{0.0F, 1.0F, 0.0F}));

    const auto* tcChild2 = world.getComponent<TransformComponent>(child2);
    TEST_CHECK(tcChild2 != nullptr);
    TEST_CHECK(isNearVec3(tcChild2->transform.position, Vec3{2.0F, 0.0F, 0.0F}));

    std::cout << "[PASS] testLocalTransforms\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Scenario 4: WorldTransformPropagation
// ---------------------------------------------------------------------------
static int testWorldTransformPropagation() {
    World world;
    auto result = GltfSceneImporter::importScene(kFixturesDir + "hierarchy_test.gltf", world);
    TEST_CHECK(result.success);

    updateHierarchyTransforms(world);

    const EntityHandle root = result.nodes[0].entity;
    const EntityHandle child1 = result.nodes[1].entity;
    const EntityHandle grandchild1 = result.nodes[2].entity;
    const EntityHandle child2 = result.nodes[3].entity;

    const auto* wtRoot = world.getComponent<WorldTransformComponent>(root);
    TEST_CHECK(wtRoot != nullptr);
    TEST_CHECK(isNearVec3(wtRoot->worldTransform.position, Vec3{0.0F, 0.0F, 0.0F}));

    const auto* wtChild1 = world.getComponent<WorldTransformComponent>(child1);
    TEST_CHECK(wtChild1 != nullptr);
    TEST_CHECK(isNearVec3(wtChild1->worldTransform.position, Vec3{1.0F, 0.0F, 0.0F}));

    const auto* wtGrandchild1 = world.getComponent<WorldTransformComponent>(grandchild1);
    TEST_CHECK(wtGrandchild1 != nullptr);
    TEST_CHECK(isNearVec3(wtGrandchild1->worldTransform.position, Vec3{1.0F, 1.0F, 0.0F}));

    const auto* wtChild2 = world.getComponent<WorldTransformComponent>(child2);
    TEST_CHECK(wtChild2 != nullptr);
    TEST_CHECK(isNearVec3(wtChild2->worldTransform.position, Vec3{2.0F, 0.0F, 0.0F}));

    TEST_CHECK(isNear(wtChild1->worldMatrix.m[12], 1.0F));
    TEST_CHECK(isNear(wtChild1->worldMatrix.m[13], 0.0F));
    TEST_CHECK(isNear(wtGrandchild1->worldMatrix.m[12], 1.0F));
    TEST_CHECK(isNear(wtGrandchild1->worldMatrix.m[13], 1.0F));

    std::cout << "[PASS] testWorldTransformPropagation\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Scenario 5: FiveLevelNesting
// ---------------------------------------------------------------------------
static int testFiveLevelNesting() {
    World world;
    auto result = GltfSceneImporter::importScene(kFixturesDir + "deep_hierarchy.gltf", world);
    TEST_CHECK(result.success);
    TEST_CHECK(result.nodeCount == 5);

    updateHierarchyTransforms(world);

    const EntityHandle l4 = result.nodes[4].entity;
    const auto* wt = world.getComponent<WorldTransformComponent>(l4);
    TEST_CHECK(wt != nullptr);

    // Sum of translations: (1+0+0+1+0, 0+1+0+1+1, 0+0+1+0+1) = (2, 3, 2)
    TEST_CHECK(isNearVec3(wt->worldTransform.position, Vec3{2.0F, 3.0F, 2.0F}));
    TEST_CHECK(isNear(wt->worldMatrix.m[12], 2.0F));
    TEST_CHECK(isNear(wt->worldMatrix.m[13], 3.0F));
    TEST_CHECK(isNear(wt->worldMatrix.m[14], 2.0F));

    std::cout << "[PASS] testFiveLevelNesting\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Scenario 6: LocatorNodesPreserved
// ---------------------------------------------------------------------------
static int testLocatorNodesPreserved() {
    World world;
    auto result = GltfSceneImporter::importScene(kFixturesDir + "hierarchy_test.gltf", world);
    TEST_CHECK(result.success);

    const GltfSceneNode& rootNode = result.nodes[0];
    const GltfSceneNode& grandchildNode = result.nodes[2];

    TEST_CHECK(!rootNode.hasMesh);
    TEST_CHECK(!grandchildNode.hasMesh);

    TEST_CHECK(world.isAlive(rootNode.entity));
    TEST_CHECK(world.isAlive(grandchildNode.entity));
    TEST_CHECK(world.getComponent<TransformComponent>(rootNode.entity) != nullptr);
    TEST_CHECK(world.getComponent<TransformComponent>(grandchildNode.entity) != nullptr);

    TEST_CHECK(result.nodes[1].hasMesh);
    TEST_CHECK(result.nodes[3].hasMesh);

    std::cout << "[PASS] testLocatorNodesPreserved\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Scenario 7: GeometryInstancing
// Child1 and Child2 both reference mesh 0 in hierarchy_test.gltf.
// Both entities must carry MeshComponents referencing the identical meshAsset ID.
// ---------------------------------------------------------------------------
static int testGeometryInstancing() {
    World world;
    const std::string path = kFixturesDir + "hierarchy_test.gltf";
    auto result = GltfSceneImporter::importScene(path, world);
    TEST_CHECK(result.success);

    const EntityHandle child1 = result.nodes[1].entity;
    const EntityHandle child2 = result.nodes[3].entity;

    const auto* mc1 = world.getComponent<MeshComponent>(child1);
    const auto* mc2 = world.getComponent<MeshComponent>(child2);

    TEST_CHECK(mc1 != nullptr);
    TEST_CHECK(mc2 != nullptr);

    // Both must reference the deduplicated URI
    const std::string expectedUri = path + "#mesh_0";
    TEST_CHECK(mc1->assetPath == expectedUri);
    TEST_CHECK(mc2->assetPath == expectedUri);

    // Identical valid 64-bit AssetId
    TEST_CHECK(mc1->meshAsset.isValid());
    TEST_CHECK(mc2->meshAsset.isValid());
    TEST_CHECK(mc1->meshAsset == mc2->meshAsset);

    std::cout << "[PASS] testGeometryInstancing\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Scenario 8: MultiPrimitiveSplits
// multi_submesh.glb has 2 primitives. The node entity should act as parent locator,
// and child entities {NodeName}_Primitive_{i} are spawned for each primitive.
// ---------------------------------------------------------------------------
static int testMultiPrimitiveSplits() {
    World world;
    const std::string path = kFixturesDir + "multi_submesh.glb";
    auto result = GltfSceneImporter::importScene(path, world);
    TEST_CHECK(result.success);

    // Node is parent locator (hasMesh == false)
    // plus 2 spawned child primitive entities (hasMesh == true)
    TEST_CHECK(result.nodes.size() >= 3);
    const auto& parentNode = result.nodes[0];
    TEST_CHECK(!parentNode.hasMesh);

    auto children = world.getChildren(parentNode.entity);
    TEST_CHECK(children.size() == 2);

    const auto* prim0Mc = world.getComponent<MeshComponent>(children[0]);
    const auto* prim1Mc = world.getComponent<MeshComponent>(children[1]);

    TEST_CHECK(prim0Mc != nullptr);
    TEST_CHECK(prim1Mc != nullptr);
    TEST_CHECK(prim0Mc->submeshIndex == 0);
    TEST_CHECK(prim1Mc->submeshIndex == 1);
    TEST_CHECK(prim0Mc->meshAsset == prim1Mc->meshAsset);

    std::cout << "[PASS] testMultiPrimitiveSplits\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Scenario 9: NonexistentFile
// ---------------------------------------------------------------------------
static int testNonexistentFileReturnsFailure() {
    World world;
    auto result = GltfSceneImporter::importScene(kFixturesDir + "does_not_exist_xyz.gltf", world);

    TEST_CHECK(!result.success);
    TEST_CHECK(!result.errorMessage.empty());
    TEST_CHECK(result.nodeCount == 0);
    TEST_CHECK(result.nodes.empty());
    TEST_CHECK(!result.rootEntity.isValid());

    std::cout << "[PASS] testNonexistentFileReturnsFailure\n";
    return 0;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    int failures = 0;

    failures += testMultiNodeEntityCount();
    failures += testParentChildRelationships();
    failures += testLocalTransforms();
    failures += testWorldTransformPropagation();
    failures += testFiveLevelNesting();
    failures += testLocatorNodesPreserved();
    failures += testGeometryInstancing();
    failures += testMultiPrimitiveSplits();
    failures += testNonexistentFileReturnsFailure();

    if (failures == 0) {
        std::cout << "\nAll gltf_scene_importer_test scenarios passed.\n";
    } else {
        std::cerr << '\n' << failures << " test scenario(s) FAILED.\n";
    }
    return failures;
}
