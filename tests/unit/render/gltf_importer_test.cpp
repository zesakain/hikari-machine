/// @file gltf_importer_test.cpp
/// @brief Comprehensive regression test suite for Milestone 8B.5 glTF 2.0 and GLB 3D asset import pipeline.

#include "core/assert.h"
#include "framework/scene_serializer.h"
#include "physics/physics_components.h"
#include "render/asset_manager.h"
#include "render/gltf_importer.h"
#include "render/material.h"
#include "render/mesh.h"
#include "render/mesh_component.h"
#include "render/obj_importer.h"
#include "rhi/rhi_device.h"
#include "world/world.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__         \
                      << std::endl;                                                                \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)

namespace {

const std::string kFixturesDir = "tests/fixtures/gltf/";

// -----------------------------------------------------------------------------
// Format Handling Tests (1 - 8)
// -----------------------------------------------------------------------------

void testValidGlbImport() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "cube.glb");
    TEST_CHECK(res.hasValue());
    const auto& mesh = res.value();
    TEST_CHECK(mesh.getVertexCount() == 24);
    TEST_CHECK(mesh.getIndexCount() == 36);
    TEST_CHECK(mesh.getTriangleCount() == 12);
    TEST_CHECK(!mesh.submeshes.empty());
    std::cout << "[PASS] testValidGlbImport\n";
}

void testValidGltfImport() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "triangle.gltf");
    TEST_CHECK(res.hasValue());
    const auto& mesh = res.value();
    TEST_CHECK(mesh.getVertexCount() == 3);
    TEST_CHECK(mesh.getIndexCount() == 3);
    TEST_CHECK(mesh.getTriangleCount() == 1);
    std::cout << "[PASS] testValidGltfImport\n";
}

void testObjImportRegression() {
    auto cubeRes = eng::render::ObjImporter::importFromFile("assets/models/cube.obj");
    TEST_CHECK(cubeRes.hasValue());
    TEST_CHECK(cubeRes.value().getVertexCount() > 0);
    TEST_CHECK(cubeRes.value().getIndexCount() == 36);

    auto prismRes = eng::render::ObjImporter::importFromFile("assets/models/prism.obj");
    TEST_CHECK(prismRes.hasValue());
    TEST_CHECK(prismRes.value().getVertexCount() > 0);
    TEST_CHECK(prismRes.value().getIndexCount() > 0);
    std::cout << "[PASS] testObjImportRegression\n";
}

void testInvalidJson() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "invalid_json.gltf");
    TEST_CHECK(!res.hasValue());
    TEST_CHECK(res.error().code == eng::ErrorCode::InvalidArgument);
    std::cout << "[PASS] testInvalidJson\n";
}

void testInvalidGlbHeader() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "invalid_header.glb");
    TEST_CHECK(!res.hasValue());
    TEST_CHECK(res.error().code == eng::ErrorCode::InvalidArgument);
    std::cout << "[PASS] testInvalidGlbHeader\n";
}

void testUnsupportedGltfVersion() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "unsupported_version.gltf");
    TEST_CHECK(!res.hasValue());
    TEST_CHECK(res.error().code == eng::ErrorCode::InvalidArgument);
    std::cout << "[PASS] testUnsupportedGltfVersion\n";
}

void testMissingExternalBuffer() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "missing_buffer.gltf");
    TEST_CHECK(!res.hasValue());
    std::cout << "[PASS] testMissingExternalBuffer\n";
}

void testMissingTextureSafety() {
    // cube.glb references "textures/albedo_grid.png" which might not exist on disk in the test dir.
    // The importer must safely import material metadata and texture URI without failing the mesh import.
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "cube.glb");
    TEST_CHECK(res.hasValue());
    const auto& mesh = res.value();
    TEST_CHECK(!mesh.materials.empty());
    TEST_CHECK(mesh.materials[0].baseColorTexture.isPresent);
    TEST_CHECK(!mesh.materials[0].baseColorTexture.uri.empty());
    std::cout << "[PASS] testMissingTextureSafety\n";
}

// -----------------------------------------------------------------------------
// Geometry Tests (9 - 18)
// -----------------------------------------------------------------------------

void testIndexedMesh() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "triangle.glb");
    TEST_CHECK(res.hasValue());
    const auto& mesh = res.value();
    TEST_CHECK(mesh.getIndexCount() == 3);
    TEST_CHECK(mesh.indices[0] == 0 && mesh.indices[1] == 1 && mesh.indices[2] == 2);
    std::cout << "[PASS] testIndexedMesh\n";
}

void testNonIndexedMesh() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "non_indexed.gltf");
    TEST_CHECK(res.hasValue());
    const auto& mesh = res.value();
    TEST_CHECK(mesh.getVertexCount() == 6);
    TEST_CHECK(mesh.getIndexCount() == 6); // Auto-generated sequential indices
    TEST_CHECK(mesh.getTriangleCount() == 2);
    std::cout << "[PASS] testNonIndexedMesh\n";
}

void testMultiplePrimitives() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "multi_submesh.glb");
    TEST_CHECK(res.hasValue());
    const auto& mesh = res.value();
    TEST_CHECK(mesh.submeshes.size() == 2);
    TEST_CHECK(mesh.getTriangleCount() == 2);
    std::cout << "[PASS] testMultiplePrimitives\n";
}

void testMultipleSubmeshes() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "multi_submesh.glb");
    TEST_CHECK(res.hasValue());
    const auto& mesh = res.value();
    TEST_CHECK(mesh.submeshes.size() == 2);
    TEST_CHECK(mesh.submeshes[0].materialIndex == 0);
    TEST_CHECK(mesh.submeshes[1].materialIndex == 1);
    TEST_CHECK(mesh.submeshes[0].indexCount == 3);
    TEST_CHECK(mesh.submeshes[1].indexCount == 3);
    std::cout << "[PASS] testMultipleSubmeshes\n";
}

void testMissingNormals() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "missing_normals.gltf");
    TEST_CHECK(res.hasValue());
    const auto& mesh = res.value();
    // Normals should have been generated automatically
    for (const auto& v : mesh.vertices) {
        TEST_CHECK(v.normal.length() > 0.5F);
    }
    std::cout << "[PASS] testMissingNormals\n";
}

void testMissingUv() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "missing_uv.gltf");
    TEST_CHECK(res.hasValue());
    const auto& mesh = res.value();
    for (const auto& v : mesh.vertices) {
        TEST_CHECK(v.uv.isNearlyEqual(eng::Vec2::zero()));
    }
    std::cout << "[PASS] testMissingUv\n";
}

void testVertexColors() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "vertex_color.glb");
    TEST_CHECK(res.hasValue());
    const auto& mesh = res.value();
    TEST_CHECK(mesh.getVertexCount() == 3);
    // Vertex 0 was set to red (1, 0, 0, 1)
    TEST_CHECK(std::abs(mesh.vertices[0].color.x - 1.0F) < 1e-3F);
    TEST_CHECK(std::abs(mesh.vertices[0].color.y - 0.0F) < 1e-3F);
    // Vertex 1 was set to green (0, 1, 0, 1)
    TEST_CHECK(std::abs(mesh.vertices[1].color.y - 1.0F) < 1e-3F);
    std::cout << "[PASS] testVertexColors\n";
}

void testInvalidIndices() {
    // In-memory glTF where index references vertex index 999 when only 3 vertices exist
    const std::string badGltf = R"({
        "asset": {"version": "2.0"},
        "buffers": [{"uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAOcD", "byteLength": 42}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36}, {"buffer": 0, "byteOffset": 36, "byteLength": 6}],
        "accessors": [
            {"bufferView": 0, "byteOffset": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 1, "byteOffset": 0, "componentType": 5123, "count": 3, "type": "SCALAR"}
        ],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "mode": 4}]}],
        "nodes": [{"mesh": 0}]
    })";
    auto res = eng::render::GltfImporter::importFromMemory(badGltf.data(), badGltf.size());
    // Must be rejected safely with OutOfBounds or InvalidArgument
    TEST_CHECK(!res.hasValue());
    std::cout << "[PASS] testInvalidIndices\n";
}

void testBoundingBoxCalculation() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "cube.glb");
    TEST_CHECK(res.hasValue());
    const auto& mesh = res.value();
    TEST_CHECK(mesh.bounds.isValid());
    TEST_CHECK(std::abs(mesh.bounds.minBound.x - (-0.5F)) < 1e-3F);
    TEST_CHECK(std::abs(mesh.bounds.maxBound.x - 0.5F) < 1e-3F);
    TEST_CHECK(std::abs(mesh.bounds.getSize().x - 1.0F) < 1e-3F);
    std::cout << "[PASS] testBoundingBoxCalculation\n";
}

void testLargeGeometryValidation() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "large_mesh.glb");
    TEST_CHECK(res.hasValue());
    const auto& mesh = res.value();
    TEST_CHECK(mesh.getVertexCount() == 441);
    TEST_CHECK(mesh.getIndexCount() == 2400);
    TEST_CHECK(mesh.getTriangleCount() == 800);
    TEST_CHECK(mesh.validate().hasValue());
    std::cout << "[PASS] testLargeGeometryValidation\n";
}

// -----------------------------------------------------------------------------
// Materials and Transforms Tests (19 - 25)
// -----------------------------------------------------------------------------

void testPbrMaterialProperties() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "cube.glb");
    TEST_CHECK(res.hasValue());
    const auto& mesh = res.value();
    TEST_CHECK(!mesh.materials.empty());
    const auto& mat = mesh.materials[0];
    TEST_CHECK(std::abs(mat.baseColor.x - 0.3F) < 1e-2F);
    TEST_CHECK(std::abs(mat.baseColor.y - 0.7F) < 1e-2F);
    TEST_CHECK(std::abs(mat.metallic - 0.8F) < 1e-2F);
    TEST_CHECK(std::abs(mat.roughness - 0.25F) < 1e-2F);
    TEST_CHECK(mat.alphaMode == eng::render::AlphaMode::Opaque);
    std::cout << "[PASS] testPbrMaterialProperties\n";
}

void testTextureReferences() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "cube.glb");
    TEST_CHECK(res.hasValue());
    const auto& mesh = res.value();
    const auto& mat = mesh.materials[0];
    TEST_CHECK(mat.baseColorTexture.isPresent);
    TEST_CHECK(mat.baseColorTexture.uri.find("albedo_grid.png") != std::string::npos);
    std::cout << "[PASS] testTextureReferences\n";
}

void testTextureColorSpaceMetadata() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "cube.glb");
    TEST_CHECK(res.hasValue());
    const auto& mesh = res.value();
    const auto& mat = mesh.materials[0];
    TEST_CHECK(mat.baseColorTexture.colorSpace == eng::render::TextureColorSpace::sRGB);
    std::cout << "[PASS] testTextureColorSpaceMetadata\n";
}

void testTranslationAndRotation() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "nested_nodes.glb");
    TEST_CHECK(res.hasValue());
    const auto& mesh = res.value();
    // Parent node has translation [10, 5, 0]
    // Vertex 0 was at (0, 0, 0) in local space -> must translate to (10, 5, 0) in world space
    TEST_CHECK(std::abs(mesh.vertices[0].position.x - 10.0F) < 1e-3F);
    TEST_CHECK(std::abs(mesh.vertices[0].position.y - 5.0F) < 1e-3F);
    std::cout << "[PASS] testTranslationAndRotation\n";
}

void testNonUniformScale() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "nested_nodes.glb");
    TEST_CHECK(res.hasValue());
    const auto& mesh = res.value();
    // Child node has scale [2, 3, 4]
    // Vertex 1 was at (1, 0, 0) -> in world space with parent translation (10, 5, 0):
    // pos = 10 + (1 * 2) = 12
    TEST_CHECK(std::abs(mesh.vertices[1].position.x - 12.0F) < 1e-3F);
    std::cout << "[PASS] testNonUniformScale\n";
}

void testNegativeScaleBehavior() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "negative_scale.glb");
    TEST_CHECK(res.hasValue());
    const auto& mesh = res.value();
    // Negative scale [-1, 1, 1] flips winding order to preserve counter-clockwise front face
    // Vertex 1 was at (1, 0, 0) -> mirrored to (-1, 0, 0)
    TEST_CHECK(std::abs(mesh.vertices[1].position.x - (-1.0F)) < 1e-3F);
    // Triangles must have indices swapped to maintain front face
    TEST_CHECK(mesh.indices[1] == 2 && mesh.indices[2] == 1);
    std::cout << "[PASS] testNegativeScaleBehavior\n";
}

void testNestedNodeTransformComposition() {
    auto res = eng::render::GltfImporter::importFromFile(kFixturesDir + "nested_nodes.glb");
    TEST_CHECK(res.hasValue());
    const auto& mesh = res.value();
    // Vertex 2 was at (0, 0, 1). With scale [2, 3, 4] and translation [10, 5, 0]:
    // pos = (10, 5, 4)
    TEST_CHECK(std::abs(mesh.vertices[2].position.x - 10.0F) < 1e-3F);
    TEST_CHECK(std::abs(mesh.vertices[2].position.y - 5.0F) < 1e-3F);
    TEST_CHECK(std::abs(mesh.vertices[2].position.z - 4.0F) < 1e-3F);
    std::cout << "[PASS] testNestedNodeTransformComposition\n";
}

// -----------------------------------------------------------------------------
// Integration Tests (26 - 31)
// -----------------------------------------------------------------------------

void testAssetManagerRegistration() {
    eng::render::AssetManager assetMgr;
    auto res = assetMgr.loadMesh(kFixturesDir + "cube.glb");
    TEST_CHECK(res.hasValue());
    const auto id = res.value();
    TEST_CHECK(assetMgr.isLoaded(id));
    const auto* md = assetMgr.getMeshData(id);
    TEST_CHECK(md != nullptr);
    TEST_CHECK(md->getVertexCount() == 24);
    std::cout << "[PASS] testAssetManagerRegistration\n";
}

void testRepeatedImportAndCacheReuse() {
    eng::render::AssetManager assetMgr;
    auto res1 = assetMgr.loadMesh(kFixturesDir + "cube.glb");
    auto res2 = assetMgr.loadMesh(kFixturesDir + "cube.glb");
    TEST_CHECK(res1.hasValue() && res2.hasValue());
    TEST_CHECK(res1.value() == res2.value()); // Identical AssetId returned without re-import
    std::cout << "[PASS] testRepeatedImportAndCacheReuse\n";
}

void testGpuMeshUpload() {
    auto devRes = eng::rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto& device = *devRes.value();

    eng::render::AssetManager assetMgr;
    auto res = assetMgr.loadMesh(kFixturesDir + "cube.glb");
    TEST_CHECK(res.hasValue());

    auto uploadRes = assetMgr.uploadMesh(res.value(), device);
    TEST_CHECK(uploadRes.hasValue());
    auto* gpuMesh = uploadRes.value();
    TEST_CHECK(gpuMesh != nullptr);
    TEST_CHECK(gpuMesh->isValid());
    TEST_CHECK(gpuMesh->getVertexCount() == 24);
    TEST_CHECK(gpuMesh->getIndexCount() == 36);

    // Repeated upload returns cached pointer
    auto uploadRes2 = assetMgr.uploadMesh(res.value(), device);
    TEST_CHECK(uploadRes2.hasValue());
    TEST_CHECK(uploadRes2.value() == gpuMesh);

    // Unload before device destruction
    assetMgr.unloadAll();
    std::cout << "[PASS] testGpuMeshUpload\n";
}

void testMeshComponentAssignment() {
    eng::world::World world;
    eng::render::registerRenderComponents(world);
    auto e = world.createEntity("GlbEntity");

    const std::string path = kFixturesDir + "cube.glb";
    const auto id = eng::render::makeAssetIdFromPath(path);
    world.setComponent(e, eng::render::MeshComponent{
                              .meshAsset = id,
                              .assetPath = path,
                              .submeshIndex = 0,
                              .visible = true,
                              .castShadows = true,
                          });

    const auto* mc = world.getComponent<eng::render::MeshComponent>(e);
    TEST_CHECK(mc != nullptr);
    TEST_CHECK(mc->meshAsset == id);
    TEST_CHECK(mc->assetPath == path);
    std::cout << "[PASS] testMeshComponentAssignment\n";
}

void testHikariSerializationRoundTrip() {
    eng::world::World world;
    eng::render::registerRenderComponents(world);
    eng::physics::registerPhysicsComponents(world);

    auto e = world.createEntity("TestGlbMeshEntity");
    world.setComponent(e, eng::physics::PhysicsTransformComponent{
                              .transform = eng::Transform{eng::Vec3{1.0F, 2.0F, 3.0F}},
                          });

    const std::string path = kFixturesDir + "cube.glb";
    const auto id = eng::render::makeAssetIdFromPath(path);
    world.setComponent(e, eng::render::MeshComponent{
                              .meshAsset = id,
                              .assetPath = path,
                              .submeshIndex = 0,
                              .visible = true,
                              .castShadows = true,
                          });

    eng::framework::SceneSerializer serializer;
    auto serRes = serializer.serializeToString(world);
    TEST_CHECK(serRes.hasValue());
    const std::string jsonText = serRes.value();

    TEST_CHECK(jsonText.find("cube.glb") != std::string::npos);

    eng::world::World loadedWorld;
    eng::render::registerRenderComponents(loadedWorld);
    eng::physics::registerPhysicsComponents(loadedWorld);

    auto deserRes = serializer.deserializeFromString(loadedWorld, jsonText);
    TEST_CHECK(deserRes.hasValue());
    TEST_CHECK(deserRes.value().entitiesLoaded == 1);

    auto entities = loadedWorld.getAllEntities();
    TEST_CHECK(entities.size() == 1);
    auto loadedE = entities[0];
    TEST_CHECK(loadedE.isValid());
    const auto* loadedMc = loadedWorld.getComponent<eng::render::MeshComponent>(loadedE);
    TEST_CHECK(loadedMc != nullptr);
    TEST_CHECK(loadedMc->assetPath == path);
    TEST_CHECK(loadedMc->meshAsset == id);
    std::cout << "[PASS] testHikariSerializationRoundTrip\n";
}

void testImportFailureLeavesAssetManagerUnchanged() {
    eng::render::AssetManager assetMgr;
    const size_t initialCount = assetMgr.getAllMetadata().size();

    auto res = assetMgr.loadMesh(kFixturesDir + "invalid_json.gltf");
    TEST_CHECK(!res.hasValue());

    // Asset manager state must be 100% untouched
    TEST_CHECK(assetMgr.getAllMetadata().size() == initialCount);
    TEST_CHECK(!assetMgr.isLoaded(eng::render::makeAssetIdFromPath(kFixturesDir + "invalid_json.gltf")));
    std::cout << "[PASS] testImportFailureLeavesAssetManagerUnchanged\n";
}

} // namespace

int main() {
    std::cout << "Running glTF 2.0 and 3D Asset Import Regression Test Suite (Milestone 8B.5)...\n";

    // Format handling (1-8)
    testValidGlbImport();
    testValidGltfImport();
    testObjImportRegression();
    testInvalidJson();
    testInvalidGlbHeader();
    testUnsupportedGltfVersion();
    testMissingExternalBuffer();
    testMissingTextureSafety();

    // Geometry (9-18)
    testIndexedMesh();
    testNonIndexedMesh();
    testMultiplePrimitives();
    testMultipleSubmeshes();
    testMissingNormals();
    testMissingUv();
    testVertexColors();
    testInvalidIndices();
    testBoundingBoxCalculation();
    testLargeGeometryValidation();

    // Materials and transforms (19-25)
    testPbrMaterialProperties();
    testTextureReferences();
    testTextureColorSpaceMetadata();
    testTranslationAndRotation();
    testNonUniformScale();
    testNegativeScaleBehavior();
    testNestedNodeTransformComposition();

    // Integration (26-31)
    testAssetManagerRegistration();
    testRepeatedImportAndCacheReuse();
    testGpuMeshUpload();
    testMeshComponentAssignment();
    testHikariSerializationRoundTrip();
    testImportFailureLeavesAssetManagerUnchanged();

    std::cout << "\nAll 31 Milestone 8B.5 glTF Importer tests PASSED successfully!\n";
    return 0;
}
