/// @file descriptor.h
/// @brief Descriptors, pool configurations, and resource write structures for RHI shader binding.
///
/// Purpose:
/// Defines hardware-neutral abstractions for descriptor set layouts, descriptor pools,
/// and descriptor resource writes (uniform/storage buffers, textures, and samplers).
///
/// Ownership and Lifetime:
/// Descriptors are pure POD value types passed by const reference. Handles (layout, pool, set)
/// are 64-bit generational handles owned by the caller. Descriptor sets are bounded by the
/// lifetime of their allocating descriptor pool.
///
/// Thread-safety:
/// Value types are const-thread-safe. Device allocation and updates are serialized per pool.
///
/// Performance:
/// Zero heap allocations during hot-path binding; descriptor writes are batched.

#pragma once

#include "core/error.h"
#include "core/result.h"
#include "rhi/rhi_types.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace eng::rhi {

/// Sentinel constant representing binding the whole buffer range.
inline constexpr size_t kWholeBufferSize = 0;

/// @struct DescriptorBindingDesc
/// @brief Specification of a single binding slot within a descriptor set layout.
struct DescriptorBindingDesc {
    uint32_t binding{0};
    DescriptorType descriptorType{DescriptorType::UniformBuffer};
    uint32_t descriptorCount{1};
    ShaderStageFlags stageFlags{ShaderStageFlags::AllGraphics};
};

/// Backwards compatibility alias for existing code.
using DescriptorSetLayoutBinding = DescriptorBindingDesc;

/// @struct DescriptorSetLayoutDesc
/// @brief Declaration of all binding slots comprising a descriptor set layout.
struct DescriptorSetLayoutDesc {
    std::vector<DescriptorBindingDesc> bindings{};
    std::string_view debugName{};
};

/// Validates that a descriptor set layout declaration has non-overlapping, valid bindings.
[[nodiscard]] constexpr Result<void>
validateDescriptorSetLayoutDesc(const DescriptorSetLayoutDesc& desc) noexcept {
    for (size_t i = 0; i < desc.bindings.size(); ++i) {
        const auto& b1 = desc.bindings[i];
        if (b1.descriptorCount == 0) {
            return Error{.code = ErrorCode::InvalidArgument,
                         .message = "Descriptor binding descriptorCount must be greater than zero"};
        }
        if (b1.stageFlags == ShaderStageFlags::None) {
            return Error{.code = ErrorCode::InvalidArgument,
                         .message = "Descriptor binding stageFlags cannot be None"};
        }
        for (size_t j = i + 1; j < desc.bindings.size(); ++j) {
            if (b1.binding == desc.bindings[j].binding) {
                return Error{.code = ErrorCode::InvalidArgument,
                             .message = "Duplicate binding index found in descriptor set layout"};
            }
        }
    }
    return Result<void>::success();
}

/// @struct DescriptorPoolSize
/// @brief Allocation budget for a specific descriptor type within a pool.
struct DescriptorPoolSize {
    DescriptorType type{DescriptorType::UniformBuffer};
    uint32_t count{1};
};

/// @struct DescriptorPoolDesc
/// @brief Capacity and budget configuration for creating a descriptor pool.
struct DescriptorPoolDesc {
    uint32_t maxSets{1};
    std::vector<DescriptorPoolSize> poolSizes{};
    std::string_view debugName{};
};

/// Validates that a descriptor pool specification contains valid capacities.
[[nodiscard]] constexpr Result<void>
validateDescriptorPoolDesc(const DescriptorPoolDesc& desc) noexcept {
    if (desc.maxSets == 0) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Descriptor pool maxSets must be greater than zero"};
    }
    if (desc.poolSizes.empty()) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Descriptor pool must specify at least one DescriptorPoolSize"};
    }
    for (const auto& size : desc.poolSizes) {
        if (size.count == 0) {
            return Error{.code = ErrorCode::InvalidArgument,
                         .message = "DescriptorPoolSize count must be greater than zero"};
        }
    }
    return Result<void>::success();
}

/// @struct DescriptorBufferInfo
/// @brief Buffer slice bound to a uniform or storage buffer descriptor slot.
struct DescriptorBufferInfo {
    BufferHandle buffer{};
    size_t offset{0};
    size_t range{kWholeBufferSize};
};

/// @struct DescriptorImageInfo
/// @brief Texture and sampler bound to an image or combined image sampler descriptor slot.
struct DescriptorImageInfo {
    SamplerHandle sampler{};
    TextureHandle texture{};
};

/// @struct DescriptorWrite
/// @brief Describes an update to one or more descriptor elements within an allocated set.
struct DescriptorWrite {
    uint32_t dstBinding{0};
    uint32_t dstArrayElement{0};
    DescriptorType descriptorType{DescriptorType::UniformBuffer};
    std::vector<DescriptorBufferInfo> bufferInfos{};
    std::vector<DescriptorImageInfo> imageInfos{};

    /// Helper to construct a single uniform or storage buffer write.
    static DescriptorWrite makeBuffer(uint32_t binding, DescriptorType type, BufferHandle buffer,
                                      size_t offset = 0, size_t range = kWholeBufferSize) {
        DescriptorWrite w{};
        w.dstBinding = binding;
        w.dstArrayElement = 0;
        w.descriptorType = type;
        w.bufferInfos.push_back(DescriptorBufferInfo{buffer, offset, range});
        return w;
    }

    /// Helper to construct a single combined image sampler write.
    static DescriptorWrite makeCombinedImageSampler(uint32_t binding, TextureHandle texture,
                                                    SamplerHandle sampler) {
        DescriptorWrite w{};
        w.dstBinding = binding;
        w.dstArrayElement = 0;
        w.descriptorType = DescriptorType::CombinedImageSampler;
        w.imageInfos.push_back(DescriptorImageInfo{sampler, texture});
        return w;
    }

    /// Helper to construct a single sampled image write.
    static DescriptorWrite makeSampledImage(uint32_t binding, TextureHandle texture) {
        DescriptorWrite w{};
        w.dstBinding = binding;
        w.dstArrayElement = 0;
        w.descriptorType = DescriptorType::SampledImage;
        w.imageInfos.push_back(DescriptorImageInfo{SamplerHandle{}, texture});
        return w;
    }

    /// Helper to construct a single sampler write.
    static DescriptorWrite makeSampler(uint32_t binding, SamplerHandle sampler) {
        DescriptorWrite w{};
        w.dstBinding = binding;
        w.dstArrayElement = 0;
        w.descriptorType = DescriptorType::Sampler;
        w.imageInfos.push_back(DescriptorImageInfo{sampler, TextureHandle{}});
        return w;
    }
};

} // namespace eng::rhi
