/// @file vulkan_types.h
/// @brief Internal Vulkan backend types, conversions, and resource slot definitions.
///
/// Purpose:
/// Provides mappings between hardware-neutral RHI descriptors and Vulkan API enumerations,
/// along with internal slot definitions for generational handle resolution.
///
/// Ownership and Lifetime:
/// Private to engine/rhi/src/vulkan. Never exposed in public headers.
///
/// Thread-safety:
/// Free conversion functions are pure and thread-safe.

#pragma once

#include "core/assert.h"
#include "core/log.h"
#include "core/result.h"
#include "rhi/buffer.h"
#include "rhi/rhi_types.h"
#include "rhi/sampler.h"
#include "rhi/texture.h"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace eng::rhi::vk {

/// Converts engine TextureFormat to corresponding VkFormat.
[[nodiscard]] constexpr VkFormat toVkFormat(TextureFormat format) noexcept {
    switch (format) {
    case TextureFormat::R8_UNORM:
        return VK_FORMAT_R8_UNORM;
    case TextureFormat::RGBA8_UNORM:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::RGBA8_SRGB:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case TextureFormat::BGRA8_UNORM:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case TextureFormat::BGRA8_SRGB:
        return VK_FORMAT_B8G8R8A8_SRGB;
    case TextureFormat::RGBA16_FLOAT:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case TextureFormat::RGBA16_UNORM:
        return VK_FORMAT_R16G16B16A16_UNORM;
    case TextureFormat::R32_FLOAT:
        return VK_FORMAT_R32_SFLOAT;
    case TextureFormat::RG32_FLOAT:
        return VK_FORMAT_R32G32_SFLOAT;
    case TextureFormat::RGB32_FLOAT:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case TextureFormat::RGBA32_FLOAT:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case TextureFormat::D32_FLOAT:
        return VK_FORMAT_D32_SFLOAT;
    case TextureFormat::D24_UNORM_S8_UINT:
        return VK_FORMAT_D24_UNORM_S8_UINT;
    case TextureFormat::D32_FLOAT_S8_UINT:
        return VK_FORMAT_D32_SFLOAT_S8_UINT;
    case TextureFormat::Undefined:
        break;
    }
    return VK_FORMAT_UNDEFINED;
}

/// Converts engine BufferUsageFlags bitmask to VkBufferUsageFlags.
[[nodiscard]] constexpr VkBufferUsageFlags toVkBufferUsage(BufferUsageFlags flags) noexcept {
    VkBufferUsageFlags vkFlags = 0;
    if (hasFlag(flags, BufferUsageFlags::Vertex)) {
        vkFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if (hasFlag(flags, BufferUsageFlags::Index)) {
        vkFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if (hasFlag(flags, BufferUsageFlags::Uniform)) {
        vkFlags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    if (hasFlag(flags, BufferUsageFlags::Storage)) {
        vkFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
    if (hasFlag(flags, BufferUsageFlags::TransferSrc)) {
        vkFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    if (hasFlag(flags, BufferUsageFlags::TransferDst)) {
        vkFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    return vkFlags;
}

/// Converts engine TextureUsageFlags bitmask to VkImageUsageFlags.
[[nodiscard]] constexpr VkImageUsageFlags toVkImageUsage(TextureUsageFlags flags) noexcept {
    VkImageUsageFlags vkFlags = 0;
    if (hasFlag(flags, TextureUsageFlags::Sampled)) {
        vkFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (hasFlag(flags, TextureUsageFlags::Storage)) {
        vkFlags |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    if (hasFlag(flags, TextureUsageFlags::ColorAttachment)) {
        vkFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if (hasFlag(flags, TextureUsageFlags::DepthStencilAttachment)) {
        vkFlags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (hasFlag(flags, TextureUsageFlags::TransferSrc)) {
        vkFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if (hasFlag(flags, TextureUsageFlags::TransferDst)) {
        vkFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    return vkFlags;
}

/// Converts Filter to VkFilter.
[[nodiscard]] constexpr VkFilter toVkFilter(Filter filter) noexcept {
    return (filter == Filter::Nearest) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

/// Converts Filter to VkSamplerMipmapMode.
[[nodiscard]] constexpr VkSamplerMipmapMode toVkSamplerMipmapMode(Filter filter) noexcept {
    return (filter == Filter::Nearest) ? VK_SAMPLER_MIPMAP_MODE_NEAREST
                                       : VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

/// Converts SamplerAddressMode to VkSamplerAddressMode.
[[nodiscard]] constexpr VkSamplerAddressMode
toVkSamplerAddressMode(SamplerAddressMode mode) noexcept {
    switch (mode) {
    case SamplerAddressMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case SamplerAddressMode::MirroredRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case SamplerAddressMode::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case SamplerAddressMode::ClampToBorder:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

/// Converts CompareOp to VkCompareOp.
[[nodiscard]] constexpr VkCompareOp toVkCompareOp(CompareOp op) noexcept {
    switch (op) {
    case CompareOp::Never:
        return VK_COMPARE_OP_NEVER;
    case CompareOp::Less:
        return VK_COMPARE_OP_LESS;
    case CompareOp::Equal:
        return VK_COMPARE_OP_EQUAL;
    case CompareOp::LessOrEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Greater:
        return VK_COMPARE_OP_GREATER;
    case CompareOp::NotEqual:
        return VK_COMPARE_OP_NOT_EQUAL;
    case CompareOp::GreaterOrEqual:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Always:
        return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_ALWAYS;
}

/// @struct VulkanBufferSlot
/// @brief Internal tracking slot for a Vulkan buffer resource.
struct VulkanBufferSlot {
    VkBuffer buffer{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    size_t sizeInBytes{0};
    BufferUsageFlags usage{BufferUsageFlags::None};
    MemoryUsage memoryUsage{MemoryUsage::GpuOnly};
    uint32_t generation{1};
    bool alive{false};
    void* mappedData{nullptr};
};

/// @struct VulkanTextureSlot
/// @brief Internal tracking slot for a Vulkan texture/image resource.
struct VulkanTextureSlot {
    VkImage image{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    TextureDesc desc{};
    uint32_t generation{1};
    bool alive{false};
};

/// @struct VulkanSamplerSlot
/// @brief Internal tracking slot for a Vulkan sampler resource.
struct VulkanSamplerSlot {
    VkSampler sampler{VK_NULL_HANDLE};
    SamplerDesc desc{};
    uint32_t generation{1};
    bool alive{false};
};

} // namespace eng::rhi::vk
