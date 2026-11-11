/// @file gpu_material.h
/// @brief GPU material resource representation and descriptor binding wrapper.
///
/// Purpose:
/// Translates CPU Material surface parameters, texture references, and samplers into
/// GPU resources (uniform buffer, descriptor set, combined image samplers).
///
/// Ownership and Lifetime:
/// Owned by AssetManager per mesh or globally. RAII manages the descriptor pool and uniform buffer.
///
/// Thread Safety:
/// Main thread / render thread only. Const accessors are thread-safe.

#pragma once

#include "core/error.h"
#include "core/math.h"
#include "core/result.h"
#include "render/material.h"
#include "rhi/descriptor.h"
#include "rhi/rhi_device.h"
#include "rhi/sampler.h"

#include <array>
#include <memory>

namespace eng::render {

class AssetManager;

/// @struct MaterialUniformBufferData
/// @brief std140-compliant uniform buffer layout for shader material parameters.
struct alignas(16) MaterialUniformBufferData {
    Vec4 baseColorFactor{1.0F, 1.0F, 1.0F, 1.0F};              // offset 0..15
    Vec4 emissiveFactorAndAlphaCutoff{0.0F, 0.0F, 0.0F, 0.5F}; // offset 16..31
    float metallicFactor{1.0F};                                // offset 32..35
    float roughnessFactor{1.0F};                               // offset 36..39
    float normalScale{1.0F};                                   // offset 40..43
    float occlusionStrength{1.0F};                             // offset 44..47
    uint32_t hasBaseColorTex{0};                               // offset 48..51
    uint32_t hasMetallicRoughnessTex{0};                       // offset 52..55
    uint32_t hasNormalTex{0};                                  // offset 56..59
    uint32_t hasOcclusionTex{0};                               // offset 60..63
    uint32_t hasEmissiveTex{0};                                // offset 64..67
    uint32_t alphaMode{0};                                     // offset 68..71
    uint32_t doubleSided{0};                                   // offset 72..75
    uint32_t padding{0};                                       // offset 76..79
};

static_assert(sizeof(MaterialUniformBufferData) == 80,
              "MaterialUniformBufferData must be 80 bytes (std140 alignment)");

/// @brief Generates standard DescriptorSetLayoutDesc for glTF materials (set 0).
[[nodiscard]] inline rhi::DescriptorSetLayoutDesc createMaterialDescriptorSetLayoutDesc() noexcept {
    rhi::DescriptorSetLayoutDesc desc{};
    desc.debugName = "MaterialDescriptorSetLayout";
    desc.bindings = {
        rhi::DescriptorBindingDesc{
            .binding = 0,
            .descriptorType = rhi::DescriptorType::CombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = rhi::ShaderStageFlags::Fragment | rhi::ShaderStageFlags::Vertex,
        },
        rhi::DescriptorBindingDesc{
            .binding = 1,
            .descriptorType = rhi::DescriptorType::CombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = rhi::ShaderStageFlags::Fragment,
        },
        rhi::DescriptorBindingDesc{
            .binding = 2,
            .descriptorType = rhi::DescriptorType::CombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = rhi::ShaderStageFlags::Fragment,
        },
        rhi::DescriptorBindingDesc{
            .binding = 3,
            .descriptorType = rhi::DescriptorType::CombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = rhi::ShaderStageFlags::Fragment,
        },
        rhi::DescriptorBindingDesc{
            .binding = 4,
            .descriptorType = rhi::DescriptorType::CombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = rhi::ShaderStageFlags::Fragment,
        },
        rhi::DescriptorBindingDesc{
            .binding = 5,
            .descriptorType = rhi::DescriptorType::UniformBuffer,
            .descriptorCount = 1,
            .stageFlags = rhi::ShaderStageFlags::Fragment | rhi::ShaderStageFlags::Vertex,
        },
    };
    return desc;
}

/// @brief Converts high-level TextureSamplerDesc to concrete RHI SamplerDesc.
[[nodiscard]] inline rhi::SamplerDesc toRhiSamplerDesc(const TextureSamplerDesc& desc) noexcept {
    rhi::SamplerDesc rhiDesc{};
    auto mapFilter = [](TextureSamplerDesc::Filter f) noexcept {
        return (f == TextureSamplerDesc::Filter::Nearest) ? rhi::Filter::Nearest
                                                          : rhi::Filter::Linear;
    };
    auto mapWrap = [](TextureSamplerDesc::WrapMode w) noexcept {
        if (w == TextureSamplerDesc::WrapMode::ClampToEdge) {
            return rhi::SamplerAddressMode::ClampToEdge;
        }
        if (w == TextureSamplerDesc::WrapMode::MirroredRepeat) {
            return rhi::SamplerAddressMode::MirroredRepeat;
        }
        return rhi::SamplerAddressMode::Repeat;
    };
    rhiDesc.minFilter = mapFilter(desc.minFilter);
    rhiDesc.magFilter = mapFilter(desc.magFilter);
    rhiDesc.mipmapMode = mapFilter(desc.mipmapMode);
    rhiDesc.addressU = mapWrap(desc.wrapS);
    rhiDesc.addressV = mapWrap(desc.wrapT);
    rhiDesc.addressW = rhi::SamplerAddressMode::Repeat;
    return rhiDesc;
}

/// @class GpuMaterial
/// @brief RAII container for material GPU resources (descriptor pool, set, uniform buffer).
class GpuMaterial {
public:
    /// @brief Factory creating GPU resources and descriptor set for a Material.
    [[nodiscard]] static Result<std::unique_ptr<GpuMaterial>>
    create(rhi::RhiDevice& device, rhi::DescriptorSetLayoutHandle layout, const Material& material,
           AssetManager& assetManager);

    ~GpuMaterial() noexcept;

    GpuMaterial(const GpuMaterial&) = delete;
    GpuMaterial& operator=(const GpuMaterial&) = delete;
    GpuMaterial(GpuMaterial&& other) noexcept;
    GpuMaterial& operator=(GpuMaterial&& other) noexcept;

    [[nodiscard]] rhi::DescriptorSetHandle getDescriptorSet() const noexcept {
        return descriptorSet_;
    }
    [[nodiscard]] rhi::BufferHandle getUniformBuffer() const noexcept { return uniformBuffer_; }
    [[nodiscard]] const MaterialUniformBufferData& getUniformData() const noexcept {
        return uboData_;
    }
    [[nodiscard]] bool isValid() const noexcept {
        return descriptorSet_.isValid() && uniformBuffer_.isValid();
    }

private:
    GpuMaterial(rhi::RhiDevice& device, rhi::DescriptorPoolHandle pool,
                rhi::DescriptorSetHandle set, rhi::BufferHandle ubo,
                const MaterialUniformBufferData& uboData) noexcept;

    rhi::RhiDevice* device_{nullptr};
    rhi::DescriptorPoolHandle descriptorPool_{};
    rhi::DescriptorSetHandle descriptorSet_{};
    rhi::BufferHandle uniformBuffer_{};
    MaterialUniformBufferData uboData_{};
};

} // namespace eng::render
