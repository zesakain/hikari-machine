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
#include "rhi/pipeline.h"
#include "rhi/rhi_types.h"
#include "rhi/sampler.h"
#include "rhi/shader.h"
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

/// Converts ShaderStageFlags bitmask to VkShaderStageFlags.
[[nodiscard]] constexpr VkShaderStageFlags toVkShaderStageFlags(ShaderStageFlags stages) noexcept {
    VkShaderStageFlags vkStages = 0;
    if (hasFlag(stages, ShaderStageFlags::Vertex)) {
        vkStages |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (hasFlag(stages, ShaderStageFlags::Fragment)) {
        vkStages |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if (hasFlag(stages, ShaderStageFlags::Compute)) {
        vkStages |= VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return vkStages;
}

/// Converts single ShaderStageFlags to primary VkShaderStageFlagBits.
[[nodiscard]] constexpr VkShaderStageFlagBits
toVkShaderStageFlagBits(ShaderStageFlags stage) noexcept {
    if (hasFlag(stage, ShaderStageFlags::Vertex)) {
        return VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (hasFlag(stage, ShaderStageFlags::Fragment)) {
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if (hasFlag(stage, ShaderStageFlags::Compute)) {
        return VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return VK_SHADER_STAGE_ALL;
}

/// Converts VertexFormat to VkFormat.
[[nodiscard]] constexpr VkFormat toVkVertexFormat(VertexFormat format) noexcept {
    switch (format) {
    case VertexFormat::Float:
        return VK_FORMAT_R32_SFLOAT;
    case VertexFormat::Float2:
        return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float4:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VertexFormat::Byte4Norm:
        return VK_FORMAT_R8G8B8A8_SNORM;
    case VertexFormat::UByte4Norm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case VertexFormat::Short2:
        return VK_FORMAT_R16G16_SINT;
    case VertexFormat::Short4:
        return VK_FORMAT_R16G16B16A16_SINT;
    case VertexFormat::Int:
        return VK_FORMAT_R32_SINT;
    case VertexFormat::UInt:
        return VK_FORMAT_R32_UINT;
    }
    return VK_FORMAT_UNDEFINED;
}

/// Converts VertexInputRate to VkVertexInputRate.
[[nodiscard]] constexpr VkVertexInputRate toVkVertexInputRate(VertexInputRate rate) noexcept {
    switch (rate) {
    case VertexInputRate::Vertex:
        return VK_VERTEX_INPUT_RATE_VERTEX;
    case VertexInputRate::Instance:
        return VK_VERTEX_INPUT_RATE_INSTANCE;
    }
    return VK_VERTEX_INPUT_RATE_VERTEX;
}

/// Converts PrimitiveTopology to VkPrimitiveTopology.
[[nodiscard]] constexpr VkPrimitiveTopology
toVkPrimitiveTopology(PrimitiveTopology topology) noexcept {
    switch (topology) {
    case PrimitiveTopology::TriangleList:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case PrimitiveTopology::TriangleStrip:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case PrimitiveTopology::LineList:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case PrimitiveTopology::LineStrip:
        return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case PrimitiveTopology::PointList:
        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

/// Converts PolygonMode to VkPolygonMode.
[[nodiscard]] constexpr VkPolygonMode toVkPolygonMode(PolygonMode mode) noexcept {
    switch (mode) {
    case PolygonMode::Fill:
        return VK_POLYGON_MODE_FILL;
    case PolygonMode::Line:
        return VK_POLYGON_MODE_LINE;
    case PolygonMode::Point:
        return VK_POLYGON_MODE_POINT;
    }
    return VK_POLYGON_MODE_FILL;
}

/// Converts CullMode to VkCullModeFlags.
[[nodiscard]] constexpr VkCullModeFlags toVkCullModeFlags(CullMode mode) noexcept {
    switch (mode) {
    case CullMode::None:
        return VK_CULL_MODE_NONE;
    case CullMode::Front:
        return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back:
        return VK_CULL_MODE_BACK_BIT;
    case CullMode::FrontAndBack:
        return VK_CULL_MODE_FRONT_AND_BACK;
    }
    return VK_CULL_MODE_NONE;
}

/// Converts FrontFace to VkFrontFace.
[[nodiscard]] constexpr VkFrontFace toVkFrontFace(FrontFace face) noexcept {
    switch (face) {
    case FrontFace::CounterClockwise:
        return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    case FrontFace::Clockwise:
        return VK_FRONT_FACE_CLOCKWISE;
    }
    return VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

/// Converts BlendFactor to VkBlendFactor.
[[nodiscard]] constexpr VkBlendFactor toVkBlendFactor(BlendFactor factor) noexcept {
    switch (factor) {
    case BlendFactor::Zero:
        return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::One:
        return VK_BLEND_FACTOR_ONE;
    case BlendFactor::SrcColor:
        return VK_BLEND_FACTOR_SRC_COLOR;
    case BlendFactor::OneMinusSrcColor:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case BlendFactor::DstColor:
        return VK_BLEND_FACTOR_DST_COLOR;
    case BlendFactor::OneMinusDstColor:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case BlendFactor::SrcAlpha:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstAlpha:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case BlendFactor::OneMinusDstAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    }
    return VK_BLEND_FACTOR_ONE;
}

/// Converts BlendOp to VkBlendOp.
[[nodiscard]] constexpr VkBlendOp toVkBlendOp(BlendOp op) noexcept {
    switch (op) {
    case BlendOp::Add:
        return VK_BLEND_OP_ADD;
    case BlendOp::Subtract:
        return VK_BLEND_OP_SUBTRACT;
    case BlendOp::ReverseSubtract:
        return VK_BLEND_OP_REVERSE_SUBTRACT;
    case BlendOp::Min:
        return VK_BLEND_OP_MIN;
    case BlendOp::Max:
        return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}

/// Converts ColorComponentFlags to VkColorComponentFlags.
[[nodiscard]] constexpr VkColorComponentFlags
toVkColorComponentFlags(ColorComponentFlags flags) noexcept {
    VkColorComponentFlags mask = 0;
    if (hasFlag(flags, ColorComponentFlags::R)) {
        mask |= VK_COLOR_COMPONENT_R_BIT;
    }
    if (hasFlag(flags, ColorComponentFlags::G)) {
        mask |= VK_COLOR_COMPONENT_G_BIT;
    }
    if (hasFlag(flags, ColorComponentFlags::B)) {
        mask |= VK_COLOR_COMPONENT_B_BIT;
    }
    if (hasFlag(flags, ColorComponentFlags::A)) {
        mask |= VK_COLOR_COMPONENT_A_BIT;
    }
    return mask;
}

/// Converts DescriptorType to VkDescriptorType.
[[nodiscard]] constexpr VkDescriptorType toVkDescriptorType(DescriptorType type) noexcept {
    switch (type) {
    case DescriptorType::UniformBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case DescriptorType::StorageBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case DescriptorType::SampledImage:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case DescriptorType::Sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case DescriptorType::CombinedImageSampler:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case DescriptorType::StorageImage:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
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

/// @struct VulkanShaderSlot
/// @brief Internal tracking slot for a compiled Vulkan shader module.
struct VulkanShaderSlot {
    VkShaderModule module{VK_NULL_HANDLE};
    ShaderDesc desc{};
    std::string entryPoint{};
    std::vector<uint32_t> code{};
    uint32_t generation{1};
    bool alive{false};
};

/// @struct VulkanPipelineLayoutSlot
/// @brief Internal tracking slot for a Vulkan pipeline layout and its descriptor set layouts.
struct VulkanPipelineLayoutSlot {
    VkPipelineLayout layout{VK_NULL_HANDLE};
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts{};
    PipelineLayoutDesc desc{};
    uint32_t generation{1};
    bool alive{false};
};

/// @struct VulkanPipelineSlot
/// @brief Internal tracking slot for a Vulkan graphics pipeline state object.
struct VulkanPipelineSlot {
    VkPipeline pipeline{VK_NULL_HANDLE};
    PipelineLayoutHandle layoutHandle{};
    GraphicsPipelineDesc desc{};
    uint32_t generation{1};
    bool alive{false};
};

} // namespace eng::rhi::vk
