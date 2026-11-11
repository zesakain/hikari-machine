/// @file mesh_test.cpp
/// @brief Comprehensive unit tests for MeshData, ObjImporter, AssetId, and GpuMesh.

#include "core/assert.h"
#include "render/asset_types.h"
#include "render/gpu_mesh.h"
#include "render/mesh.h"
#include "render/obj_importer.h"
#include "rhi/rhi_device.h"

#include <cmath>
#include <iostream>
#include <string>

using namespace eng;
using namespace eng::render;

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << '\n'; \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

static int testAssetIdHashing() {
    const AssetId id1 = makeAssetIdFromPath("assets/models/cube.obj");
    const AssetId id2 = makeAssetIdFromPath("assets/models/cube.obj");
    const AssetId id3 = makeAssetIdFromPath("assets\\models\\cube.obj"); // Backslash normalization
    const AssetId id4 = makeAssetIdFromPath("./assets/models/cube.obj"); // Leading ./ normalization
    const AssetId idDiff = makeAssetIdFromPath("assets/models/prism.obj");

    TEST_CHECK(id1.isValid());
    TEST_CHECK(id1 == id2);
    TEST_CHECK(id1 == id3);
    TEST_CHECK(id1 == id4);
    TEST_CHECK(id1 != idDiff);
    return 0;
}

static int testMeshDataValidation() {
    // 1. Empty mesh fails validation
    MeshData emptyMesh;
    auto resEmpty = emptyMesh.validate();
    TEST_CHECK(!resEmpty.hasValue());
    TEST_CHECK(resEmpty.error().code == ErrorCode::InvalidArgument);

    // 2. Vertices without indices fails validation
    MeshData vertOnlyMesh;
    vertOnlyMesh.vertices.push_back(Vertex{Vec3::zero(), Vec3::unitY(), Vec2::zero()});
    auto resVertOnly = vertOnlyMesh.validate();
    TEST_CHECK(!resVertOnly.hasValue());

    // 3. Non-triangulated index count (e.g. 2 indices) fails validation
    MeshData nonTriMesh;
    nonTriMesh.vertices.push_back(Vertex{Vec3::zero(), Vec3::unitY(), Vec2::zero()});
    nonTriMesh.vertices.push_back(Vertex{Vec3::unitX(), Vec3::unitY(), Vec2::zero()});
    nonTriMesh.indices = {0, 1};
    auto resNonTri = nonTriMesh.validate();
    TEST_CHECK(!resNonTri.hasValue());

    // 4. Out of bounds index references fail validation
    MeshData oobMesh;
    oobMesh.vertices.push_back(Vertex{Vec3::zero(), Vec3::unitY(), Vec2::zero()});
    oobMesh.indices = {0, 1, 2}; // 1 and 2 don't exist
    auto resOob = oobMesh.validate();
    TEST_CHECK(!resOob.hasValue());
    TEST_CHECK(resOob.error().code == ErrorCode::OutOfBounds);

    // 5. Valid triangle mesh passes validation
    MeshData validMesh;
    validMesh.vertices.push_back(Vertex{Vec3::zero(), Vec3::unitY(), Vec2::zero()});
    validMesh.vertices.push_back(Vertex{Vec3::unitX(), Vec3::unitY(), Vec2::zero()});
    validMesh.vertices.push_back(Vertex{Vec3::unitY(), Vec3::unitY(), Vec2::zero()});
    validMesh.indices = {0, 1, 2};
    auto resValid = validMesh.validate();
    TEST_CHECK(resValid.hasValue());

    return 0;
}

static int testMeshDataBoundsAndNormals() {
    MeshData mesh;
    mesh.vertices.push_back(Vertex{Vec3(-1.0F, -2.0F, -3.0F), Vec3::zero(), Vec2::zero()});
    mesh.vertices.push_back(Vertex{Vec3(4.0F, 5.0F, 6.0F), Vec3::zero(), Vec2::zero()});
    mesh.vertices.push_back(Vertex{Vec3(0.0F, 1.0F, 2.0F), Vec3::zero(), Vec2::zero()});
    mesh.indices = {0, 1, 2};

    mesh.recalculateBounds();
    TEST_CHECK(mesh.bounds.isValid());
    TEST_CHECK(std::abs(mesh.bounds.minBound.x - (-1.0F)) < 1e-5F);
    TEST_CHECK(std::abs(mesh.bounds.maxBound.x - 4.0F) < 1e-5F);
    TEST_CHECK(std::abs(mesh.bounds.getSize().x - 5.0F) < 1e-5F);

    mesh.generateNormals();
    for (const auto& v : mesh.vertices) {
        TEST_CHECK(std::abs(v.normal.length() - 1.0F) < 1e-4F);
    }

    return 0;
}

static int testMeshDataTangents() {
    MeshData mesh;
    // Standard quad in XY plane facing +Z
    mesh.vertices = {
        Vertex(Vec3(-1.0F, -1.0F, 0.0F), Vec3(0.0F, 0.0F, 1.0F), Vec2(0.0F, 1.0F)),
        Vertex(Vec3(1.0F, -1.0F, 0.0F), Vec3(0.0F, 0.0F, 1.0F), Vec2(1.0F, 1.0F)),
        Vertex(Vec3(1.0F, 1.0F, 0.0F), Vec3(0.0F, 0.0F, 1.0F), Vec2(1.0F, 0.0F)),
        Vertex(Vec3(-1.0F, 1.0F, 0.0F), Vec3(0.0F, 0.0F, 1.0F), Vec2(0.0F, 0.0F)),
    };
    mesh.indices = {0, 1, 2, 2, 3, 0};

    mesh.generateTangents();

    for (const auto& v : mesh.vertices) {
        // Tangent should be aligned with +X
        TEST_CHECK(std::abs(v.tangent.x - 1.0F) < 1e-4F);
        TEST_CHECK(std::abs(v.tangent.y - 0.0F) < 1e-4F);
        TEST_CHECK(std::abs(v.tangent.z - 0.0F) < 1e-4F);
        // Handedness w should be +1 or -1
        TEST_CHECK(std::abs(std::abs(v.tangent.w) - 1.0F) < 1e-4F);
        // Orthogonal to normal
        const Vec3 tanVec{v.tangent.x, v.tangent.y, v.tangent.z};
        TEST_CHECK(std::abs(Vec3::dot(v.normal, tanVec)) < 1e-4F);
    }

    return 0;
}

static int testObjImporterFromString() {
    const std::string_view objQuad = R"(
# Test Quad
v 0.0 0.0 0.0
v 1.0 0.0 0.0
v 1.0 1.0 0.0
v 0.0 1.0 0.0

vt 0.0 0.0
vt 1.0 0.0
vt 1.0 1.0
vt 0.0 1.0

vn 0.0 0.0 1.0

# Quad face with fan triangulation
f 1/1/1 2/2/1 3/3/1 4/4/1
)";

    auto res = ObjImporter::importFromString(objQuad, "TestQuad");
    TEST_CHECK(res.hasValue());
    const auto& mesh = res.value();

    TEST_CHECK(mesh.vertices.size() == 4);
    TEST_CHECK(mesh.indices.size() == 6); // 2 triangles
    TEST_CHECK(mesh.getTriangleCount() == 2);
    TEST_CHECK(mesh.bounds.isValid());

    return 0;
}

static int testObjImporterFromFile() {
    // 1. Test standard cube
    auto cubeRes = ObjImporter::importFromFile("assets/models/cube.obj");
    TEST_CHECK(cubeRes.hasValue());
    const auto& cubeMesh = cubeRes.value();
    TEST_CHECK(cubeMesh.validate().hasValue());
    TEST_CHECK(cubeMesh.vertices.size() >= 8);
    TEST_CHECK(cubeMesh.indices.size() == 36); // 12 triangles * 3
    TEST_CHECK(cubeMesh.getTriangleCount() == 12);
    TEST_CHECK(cubeMesh.bounds.isValid());
    TEST_CHECK(std::abs(cubeMesh.bounds.getSize().x - 1.0F) < 1e-4F);
    TEST_CHECK(std::abs(cubeMesh.bounds.getSize().y - 1.0F) < 1e-4F);
    TEST_CHECK(std::abs(cubeMesh.bounds.getSize().z - 1.0F) < 1e-4F);

    // 2. Test triangular prism
    auto prismRes = ObjImporter::importFromFile("assets/models/prism.obj");
    TEST_CHECK(prismRes.hasValue());
    const auto& prismMesh = prismRes.value();
    TEST_CHECK(prismMesh.validate().hasValue());
    TEST_CHECK(prismMesh.indices.size() == 24); // 8 triangles * 3
    TEST_CHECK(prismMesh.getTriangleCount() == 8);

    // 3. Test non-existent file
    auto nonExistent = ObjImporter::importFromFile("assets/models/does_not_exist.obj");
    TEST_CHECK(!nonExistent.hasValue());
    TEST_CHECK(nonExistent.error().code == ErrorCode::InvalidArgument);

    return 0;
}

static int testGpuMeshCreationAndRAII() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto& device = *devRes.value();

    auto cubeRes = ObjImporter::importFromFile("assets/models/cube.obj");
    TEST_CHECK(cubeRes.hasValue());

    auto gpuRes = GpuMesh::create(device, cubeRes.value());
    TEST_CHECK(gpuRes.hasValue());
    auto gpuMesh = std::move(gpuRes.value());

    TEST_CHECK(gpuMesh->isValid());
    TEST_CHECK(gpuMesh->getVertexBuffer().isValid());
    TEST_CHECK(gpuMesh->getIndexBuffer().isValid());
    TEST_CHECK(gpuMesh->getIndexCount() == 36);
    TEST_CHECK(gpuMesh->getBounds().isValid());

    // Test move constructor
    GpuMesh movedMesh = std::move(*gpuMesh);
    TEST_CHECK(movedMesh.isValid());
    TEST_CHECK(!gpuMesh->isValid());

    return 0;
}

int main() {
    std::cout << "[Running Mesh Tests]\n";
    if (testAssetIdHashing() != 0)
        return 1;
    if (testMeshDataValidation() != 0)
        return 2;
    if (testMeshDataBoundsAndNormals() != 0)
        return 3;
    if (testMeshDataTangents() != 0)
        return 7;
    if (testObjImporterFromString() != 0)
        return 4;
    if (testObjImporterFromFile() != 0)
        return 5;
    if (testGpuMeshCreationAndRAII() != 0)
        return 6;
    std::cout << "[Mesh Tests Passed: 7/7]\n";
    return 0;
}
