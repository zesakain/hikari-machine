/// @file material_descriptor_test.cpp
/// @brief Unit tests for GpuMaterial and descriptor set resource binding.

#include "core/assert.h"
#include "render/asset_manager.h"
#include "render/gpu_material.h"
#include "render/material.h"
#include "rhi/rhi_device.h"

#include <cmath>
#include <iostream>

using namespace eng;
using namespace eng::render;

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__          \
                      << std::endl;                                                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

static int testMaterialDescriptorSetLayoutCreation() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto& device = *devRes.value();

    const auto layoutDesc = createMaterialDescriptorSetLayoutDesc();
    TEST_CHECK(layoutDesc.bindings.size() == 6);

    auto layoutRes = device.createDescriptorSetLayout(layoutDesc);
    TEST_CHECK(layoutRes.hasValue());
    const auto layout = layoutRes.value();
    TEST_CHECK(layout.isValid());

    device.destroyDescriptorSetLayout(layout);
    return 0;
}

static int testGpuMaterialCreationAndUniformPacking() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto& device = *devRes.value();

    const auto layoutDesc = createMaterialDescriptorSetLayoutDesc();
    auto layoutRes = device.createDescriptorSetLayout(layoutDesc);
    TEST_CHECK(layoutRes.hasValue());
    const auto layout = layoutRes.value();

    AssetManager assetManager;

    Material mat{};
    mat.name = "TestPBR";
    mat.baseColor = Vec4{0.7F, 0.2F, 0.1F, 0.9F};
    mat.metallic = 0.85F;
    mat.roughness = 0.15F;
    mat.emissiveFactor = Vec3{0.5F, 0.4F, 0.3F};
    mat.alphaCutoff = 0.6F;
    mat.alphaMode = AlphaMode::Mask;
    mat.doubleSided = true;

    auto gpuMatRes = GpuMaterial::create(device, layout, mat, assetManager);
    TEST_CHECK(gpuMatRes.hasValue());
    auto& gpuMat = *gpuMatRes.value();
    TEST_CHECK(gpuMat.isValid());
    TEST_CHECK(gpuMat.getDescriptorSet().isValid());
    TEST_CHECK(gpuMat.getUniformBuffer().isValid());

    const auto& ubo = gpuMat.getUniformData();
    TEST_CHECK(std::abs(ubo.baseColorFactor.x - 0.7F) < 1e-4F);
    TEST_CHECK(std::abs(ubo.baseColorFactor.y - 0.2F) < 1e-4F);
    TEST_CHECK(std::abs(ubo.baseColorFactor.z - 0.1F) < 1e-4F);
    TEST_CHECK(std::abs(ubo.baseColorFactor.w - 0.9F) < 1e-4F);
    TEST_CHECK(std::abs(ubo.metallicFactor - 0.85F) < 1e-4F);
    TEST_CHECK(std::abs(ubo.roughnessFactor - 0.15F) < 1e-4F);
    TEST_CHECK(std::abs(ubo.emissiveFactorAndAlphaCutoff.x - 0.5F) < 1e-4F);
    TEST_CHECK(std::abs(ubo.emissiveFactorAndAlphaCutoff.y - 0.4F) < 1e-4F);
    TEST_CHECK(std::abs(ubo.emissiveFactorAndAlphaCutoff.z - 0.3F) < 1e-4F);
    TEST_CHECK(std::abs(ubo.emissiveFactorAndAlphaCutoff.w - 0.6F) < 1e-4F);
    TEST_CHECK(ubo.alphaMode == 1u); // Mask
    TEST_CHECK(ubo.doubleSided == 1u);

    // Fallbacks: No textures provided, so all texture flags must be 0
    TEST_CHECK(ubo.hasBaseColorTex == 0u);
    TEST_CHECK(ubo.hasMetallicRoughnessTex == 0u);
    TEST_CHECK(ubo.hasNormalTex == 0u);
    TEST_CHECK(ubo.hasOcclusionTex == 0u);
    TEST_CHECK(ubo.hasEmissiveTex == 0u);

    device.destroyDescriptorSetLayout(layout);
    return 0;
}

static int testGpuMaterialTextureResolution() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto& device = *devRes.value();

    const auto layoutDesc = createMaterialDescriptorSetLayoutDesc();
    auto layoutRes = device.createDescriptorSetLayout(layoutDesc);
    TEST_CHECK(layoutRes.hasValue());
    const auto layout = layoutRes.value();

    AssetManager assetManager;

    // Register a valid host image
    ImageData albedoImg{
        .width = 2,
        .height = 2,
        .channels = 4,
        .format = rhi::TextureFormat::RGBA8_SRGB,
        .pixels = std::vector<uint8_t>(2 * 2 * 4, 255),
    };
    auto regRes = assetManager.registerTexture("textures/albedo.png#srgb", std::move(albedoImg));
    TEST_CHECK(regRes.hasValue());

    Material mat{};
    mat.baseColorTexture.isPresent = true;
    mat.baseColorTexture.uri = "textures/albedo.png";
    mat.baseColorTexture.colorSpace = TextureColorSpace::sRGB;
    mat.baseColorTexture.textureAsset = regRes.value();

    auto gpuMatRes = GpuMaterial::create(device, layout, mat, assetManager);
    TEST_CHECK(gpuMatRes.hasValue());
    const auto& gpuMat = *gpuMatRes.value();
    TEST_CHECK(gpuMat.isValid());

    // Albedo texture slot must be flagged present
    TEST_CHECK(gpuMat.getUniformData().hasBaseColorTex == 1u);
    TEST_CHECK(gpuMat.getUniformData().hasNormalTex == 0u);

    device.destroyDescriptorSetLayout(layout);
    return 0;
}

static int testAssetManagerMaterialIntegration() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto& device = *devRes.value();

    const auto layoutDesc = createMaterialDescriptorSetLayoutDesc();
    auto layoutRes = device.createDescriptorSetLayout(layoutDesc);
    TEST_CHECK(layoutRes.hasValue());
    const auto layout = layoutRes.value();

    AssetManager assetManager;

    MeshData meshData("MultiMatMesh");
    meshData.vertices.push_back(Vertex(Vec3(0, 0, 0), Vec3(0, 1, 0), Vec2(0, 0), Vec4(1)));
    meshData.vertices.push_back(Vertex(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec2(1, 0), Vec4(1)));
    meshData.vertices.push_back(Vertex(Vec3(0, 1, 0), Vec3(0, 1, 0), Vec2(0, 1), Vec4(1)));
    meshData.indices = {0, 1, 2};

    Material mat0 = Material::createDefault();
    mat0.name = "MatZero";
    mat0.baseColor = Vec4(1, 0, 0, 1);

    Material mat1 = Material::createDefault();
    mat1.name = "MatOne";
    mat1.baseColor = Vec4(0, 1, 0, 1);

    meshData.materials = {mat0, mat1};

    Submesh sub0{.firstIndex = 0, .indexCount = 3, .materialIndex = 0};
    Submesh sub1{.firstIndex = 0, .indexCount = 3, .materialIndex = 1};
    meshData.submeshes = {sub0, sub1};

    auto regMeshRes = assetManager.registerMesh("models/test_mesh.gltf", std::move(meshData));
    TEST_CHECK(regMeshRes.hasValue());
    const AssetId meshId = regMeshRes.value();

    // Upload materials
    auto mat0Res = assetManager.uploadMaterial(meshId, 0, device, layout);
    TEST_CHECK(mat0Res.hasValue());
    GpuMaterial* gpuMat0 = mat0Res.value();
    TEST_CHECK(gpuMat0 != nullptr && gpuMat0->isValid());
    TEST_CHECK(std::abs(gpuMat0->getUniformData().baseColorFactor.x - 1.0F) < 1e-4F);

    auto mat1Res = assetManager.uploadMaterial(meshId, 1, device, layout);
    TEST_CHECK(mat1Res.hasValue());
    GpuMaterial* gpuMat1 = mat1Res.value();
    TEST_CHECK(gpuMat1 != nullptr && gpuMat1->isValid());
    TEST_CHECK(std::abs(gpuMat1->getUniformData().baseColorFactor.y - 1.0F) < 1e-4F);

    // Re-query returns cached pointer without reallocation
    auto mat0Res2 = assetManager.uploadMaterial(meshId, 0, device, layout);
    TEST_CHECK(mat0Res2.hasValue());
    TEST_CHECK(mat0Res2.value() == gpuMat0);

    TEST_CHECK(assetManager.getGpuMaterial(meshId, 0) == gpuMat0);
    TEST_CHECK(assetManager.getGpuMaterial(meshId, 1) == gpuMat1);

    // Fallback default material query
    auto defMatRes = assetManager.getOrCreateDefaultMaterial(device, layout);
    TEST_CHECK(defMatRes.hasValue());
    TEST_CHECK(defMatRes.value() != nullptr && defMatRes.value()->isValid());

    assetManager.unloadAll();
    device.destroyDescriptorSetLayout(layout);
    return 0;
}

int main() {
    std::cout << "[Running Material Descriptor & Resource Binding Tests]\n";
    if (testMaterialDescriptorSetLayoutCreation() != 0)
        return 1;
    if (testGpuMaterialCreationAndUniformPacking() != 0)
        return 2;
    if (testGpuMaterialTextureResolution() != 0)
        return 3;
    if (testAssetManagerMaterialIntegration() != 0)
        return 4;
    std::cout << "[Material Descriptor Tests Passed: 4/4]\n";
    return 0;
}
