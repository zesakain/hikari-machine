/// @file gpu_material.cpp
/// @brief Concrete implementation of GpuMaterial resource management and descriptor binding.

#include "render/gpu_material.h"

#include "core/log.h"
#include "render/asset_manager.h"

#include <utility>
#include <vector>

namespace eng::render {

GpuMaterial::GpuMaterial(rhi::RhiDevice& device, rhi::DescriptorPoolHandle pool,
                         rhi::DescriptorSetHandle set, rhi::BufferHandle ubo,
                         const MaterialUniformBufferData& uboData) noexcept
    : device_(&device), descriptorPool_(pool), descriptorSet_(set), uniformBuffer_(ubo),
      uboData_(uboData) {}

GpuMaterial::~GpuMaterial() noexcept {
    if (device_ != nullptr) {
        if (uniformBuffer_.isValid()) {
            device_->destroyBuffer(uniformBuffer_);
            uniformBuffer_ = {};
        }
        if (descriptorPool_.isValid()) {
            device_->destroyDescriptorPool(descriptorPool_);
            descriptorPool_ = {};
            descriptorSet_ = {};
        }
        device_ = nullptr;
    }
}

GpuMaterial::GpuMaterial(GpuMaterial&& other) noexcept
    : device_(other.device_), descriptorPool_(other.descriptorPool_),
      descriptorSet_(other.descriptorSet_), uniformBuffer_(other.uniformBuffer_),
      uboData_(other.uboData_) {
    other.device_ = nullptr;
    other.descriptorPool_ = {};
    other.descriptorSet_ = {};
    other.uniformBuffer_ = {};
}

GpuMaterial& GpuMaterial::operator=(GpuMaterial&& other) noexcept {
    if (this != &other) {
        if (device_ != nullptr) {
            if (uniformBuffer_.isValid()) {
                device_->destroyBuffer(uniformBuffer_);
            }
            if (descriptorPool_.isValid()) {
                device_->destroyDescriptorPool(descriptorPool_);
            }
        }
        device_ = other.device_;
        descriptorPool_ = other.descriptorPool_;
        descriptorSet_ = other.descriptorSet_;
        uniformBuffer_ = other.uniformBuffer_;
        uboData_ = other.uboData_;

        other.device_ = nullptr;
        other.descriptorPool_ = {};
        other.descriptorSet_ = {};
        other.uniformBuffer_ = {};
    }
    return *this;
}

Result<std::unique_ptr<GpuMaterial>> GpuMaterial::create(rhi::RhiDevice& device,
                                                         rhi::DescriptorSetLayoutHandle layout,
                                                         const Material& material,
                                                         AssetManager& assetManager) {
    if (!layout.isValid()) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Invalid DescriptorSetLayoutHandle in GpuMaterial::create"};
    }

    // 1. Create dedicated descriptor pool for this material
    rhi::DescriptorPoolDesc poolDesc{};
    poolDesc.maxSets = 1;
    poolDesc.poolSizes = {
        rhi::DescriptorPoolSize{.type = rhi::DescriptorType::CombinedImageSampler, .count = 5},
        rhi::DescriptorPoolSize{.type = rhi::DescriptorType::UniformBuffer, .count = 1},
    };
    poolDesc.debugName = "GpuMaterial_DescriptorPool";

    auto poolRes = device.createDescriptorPool(poolDesc);
    if (!poolRes.hasValue()) {
        return poolRes.error();
    }
    const rhi::DescriptorPoolHandle poolHandle = poolRes.value();

    // 2. Allocate descriptor set
    auto setRes = device.allocateDescriptorSet(poolHandle, layout);
    if (!setRes.hasValue()) {
        device.destroyDescriptorPool(poolHandle);
        return setRes.error();
    }
    const rhi::DescriptorSetHandle setHandle = setRes.value();

    // 3. Resolve texture and sampler slots (falling back to engine 1x1 defaults if absent)
    auto resolveSlot =
        [&](const TextureInfo& info, DefaultTextureType fallbackType,
            uint32_t& outHasTex) -> std::pair<rhi::TextureHandle, rhi::SamplerHandle> {
        if (info.isPresent && !info.uri.empty()) {
            AssetId texId = info.textureAsset;
            if (!assetManager.isLoaded(texId)) {
                auto loadRes =
                    assetManager.loadTexture(info.uri, info.colorSpace == TextureColorSpace::sRGB);
                if (loadRes.hasValue()) {
                    texId = loadRes.value();
                }
            }
            rhi::SamplerDesc smp = toRhiSamplerDesc(info.sampler);
            auto upRes = assetManager.uploadTexture(texId, device, true, &smp);
            if (upRes.hasValue() && upRes.value() != nullptr && upRes.value()->isValid()) {
                outHasTex = 1u;
                return {upRes.value()->getTexture(), upRes.value()->getSampler()};
            }
        }
        outHasTex = 0u;
        auto fallbackRes = assetManager.getOrCreateDefaultTexture(device, fallbackType);
        if (fallbackRes.hasValue() && fallbackRes.value() != nullptr) {
            return {fallbackRes.value()->getTexture(), fallbackRes.value()->getSampler()};
        }
        return {rhi::TextureHandle{}, rhi::SamplerHandle{}};
    };

    MaterialUniformBufferData uboData{};
    uboData.baseColorFactor = material.baseColor;
    uboData.emissiveFactorAndAlphaCutoff =
        Vec4{material.emissiveFactor.x, material.emissiveFactor.y, material.emissiveFactor.z,
             material.alphaCutoff};
    uboData.metallicFactor = material.metallic;
    uboData.roughnessFactor = material.roughness;
    uboData.normalScale = material.normalTexture.scale;
    uboData.occlusionStrength = material.occlusionTexture.strength;
    uboData.alphaMode = static_cast<uint32_t>(material.alphaMode);
    uboData.doubleSided = material.doubleSided ? 1u : 0u;

    const auto baseColorSlot = resolveSlot(material.baseColorTexture, DefaultTextureType::WhiteSRGB,
                                           uboData.hasBaseColorTex);
    const auto mrSlot =
        resolveSlot(material.metallicRoughnessTexture, DefaultTextureType::WhiteLinear,
                    uboData.hasMetallicRoughnessTex);
    const auto normSlot = resolveSlot(material.normalTexture, DefaultTextureType::FlatNormalLinear,
                                      uboData.hasNormalTex);
    const auto occSlot = resolveSlot(material.occlusionTexture, DefaultTextureType::WhiteLinear,
                                     uboData.hasOcclusionTex);
    const auto emissiveSlot = resolveSlot(material.emissiveTexture, DefaultTextureType::BlackSRGB,
                                          uboData.hasEmissiveTex);

    // 4. Create and upload uniform buffer
    rhi::BufferDesc uboDesc{};
    uboDesc.sizeInBytes = sizeof(MaterialUniformBufferData);
    uboDesc.usage = rhi::BufferUsageFlags::Uniform | rhi::BufferUsageFlags::TransferDst;
    uboDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
    uboDesc.debugName = "MaterialUniformBuffer";

    auto uboRes = device.createBuffer(uboDesc);
    if (!uboRes.hasValue()) {
        device.destroyDescriptorPool(poolHandle);
        return uboRes.error();
    }
    const rhi::BufferHandle uboHandle = uboRes.value();

    auto writeRes = device.writeBuffer(uboHandle, 0, &uboData, sizeof(MaterialUniformBufferData));
    if (!writeRes.hasValue()) {
        device.destroyBuffer(uboHandle);
        device.destroyDescriptorPool(poolHandle);
        return writeRes.error();
    }

    // 5. Update descriptor set with textures and uniform buffer
    std::vector<rhi::DescriptorWrite> writes;
    writes.reserve(6);
    writes.push_back(rhi::DescriptorWrite::makeCombinedImageSampler(0, baseColorSlot.first,
                                                                    baseColorSlot.second));
    writes.push_back(
        rhi::DescriptorWrite::makeCombinedImageSampler(1, mrSlot.first, mrSlot.second));
    writes.push_back(
        rhi::DescriptorWrite::makeCombinedImageSampler(2, normSlot.first, normSlot.second));
    writes.push_back(
        rhi::DescriptorWrite::makeCombinedImageSampler(3, occSlot.first, occSlot.second));
    writes.push_back(
        rhi::DescriptorWrite::makeCombinedImageSampler(4, emissiveSlot.first, emissiveSlot.second));
    writes.push_back(rhi::DescriptorWrite::makeBuffer(
        5, rhi::DescriptorType::UniformBuffer, uboHandle, 0, sizeof(MaterialUniformBufferData)));

    device.updateDescriptorSet(setHandle, writes);

    return Result<std::unique_ptr<GpuMaterial>>::success(std::unique_ptr<GpuMaterial>(
        new GpuMaterial(device, poolHandle, setHandle, uboHandle, uboData)));
}

} // namespace eng::render
