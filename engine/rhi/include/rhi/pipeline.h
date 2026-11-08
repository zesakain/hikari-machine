/// @file pipeline.h
/// @brief Graphics pipeline and pipeline layout descriptors for RHI.
///
/// Purpose:
/// Defines hardware-neutral state objects for configuring GPU graphics pipelines,
/// vertex layouts, rasterization, depth/stencil testing, color blending,
/// and pipeline resource layouts (push constants and descriptor sets).
///
/// Ownership and Lifetime:
/// Descriptors are pure value types passed by const reference during pipeline creation.
/// Returned PipelineHandle and PipelineLayoutHandle are generational handles owned by the caller.
/// Pipelines reference shaders and layouts; shaders and layouts must outlive pipeline creation.
///
/// Thread-safety:
/// Value types; thread-safe.
///
/// Performance:
/// Fits in registers/stack; zero allocation during draw-time binding.

#pragma once

#include "core/error.h"
#include "core/result.h"
#include "rhi/rhi_types.h"
#include "rhi/shader.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace eng::rhi {

/// Maximum push constant block size guaranteed across all baseline hardware (128 bytes).
inline constexpr uint32_t kMaxPushConstantSize = 128;

/// @struct VertexAttributeDesc
/// @brief Definition of an individual vertex input attribute.
struct VertexAttributeDesc {
    uint32_t location{0};
    uint32_t binding{0};
    VertexFormat format{VertexFormat::Float3};
    uint32_t offset{0};
};

/// @struct VertexBindingDesc
/// @brief Vertex buffer stream binding slot and stride.
struct VertexBindingDesc {
    uint32_t binding{0};
    uint32_t stride{0};
    VertexInputRate inputRate{VertexInputRate::Vertex};
};

/// @struct VertexInputLayout
/// @brief Complete vertex input specification.
struct VertexInputLayout {
    std::vector<VertexBindingDesc> bindings{};
    std::vector<VertexAttributeDesc> attributes{};
};

/// @struct PipelineRasterizationState
/// @brief Polygon rasterizer and culling configuration.
struct PipelineRasterizationState {
    PolygonMode polygonMode{PolygonMode::Fill};
    CullMode cullMode{CullMode::Back};
    FrontFace frontFace{FrontFace::CounterClockwise};
    bool depthBiasEnable{false};
    float depthBiasConstantFactor{0.0F};
    float depthBiasSlopeFactor{0.0F};
    float lineWidth{1.0F};
};

/// @struct PipelineDepthStencilState
/// @brief Depth testing and write mask configuration.
struct PipelineDepthStencilState {
    bool depthTestEnable{false};
    bool depthWriteEnable{false};
    CompareOp depthCompareOp{CompareOp::Less};
};

/// @struct PipelineColorBlendAttachmentState
/// @brief Color blending configuration per render target attachment.
struct PipelineColorBlendAttachmentState {
    bool blendEnable{false};
    BlendFactor srcColorBlendFactor{BlendFactor::One};
    BlendFactor dstColorBlendFactor{BlendFactor::Zero};
    BlendOp colorBlendOp{BlendOp::Add};
    BlendFactor srcAlphaBlendFactor{BlendFactor::One};
    BlendFactor dstAlphaBlendFactor{BlendFactor::Zero};
    BlendOp alphaBlendOp{BlendOp::Add};
    ColorComponentFlags colorWriteMask{ColorComponentFlags::All};
};

/// @struct PushConstantRange
/// @brief Push constant memory range accessible by specific shader stages.
struct PushConstantRange {
    ShaderStageFlags stageFlags{ShaderStageFlags::None};
    uint32_t offset{0};
    uint32_t size{0};
};

/// @struct DescriptorSetLayoutBinding
/// @brief Resource binding specification within a descriptor set.
struct DescriptorSetLayoutBinding {
    uint32_t binding{0};
    DescriptorType descriptorType{DescriptorType::UniformBuffer};
    uint32_t descriptorCount{1};
    ShaderStageFlags stageFlags{ShaderStageFlags::AllGraphics};
};

/// @struct DescriptorSetLayoutDesc
/// @brief Layout of a single descriptor set.
struct DescriptorSetLayoutDesc {
    std::vector<DescriptorSetLayoutBinding> bindings{};
};

/// @struct PipelineLayoutDesc
/// @brief Complete shader resource interface layout (push constants and descriptor sets).
struct PipelineLayoutDesc {
    std::vector<PushConstantRange> pushConstants{};
    std::vector<DescriptorSetLayoutDesc> descriptorSetLayouts{};
    std::string_view debugName{};
};

/// Validates a PipelineLayoutDesc.
[[nodiscard]] constexpr Result<void>
validatePipelineLayoutDesc(const PipelineLayoutDesc& desc) noexcept {
    for (const auto& pc : desc.pushConstants) {
        if (pc.stageFlags == ShaderStageFlags::None) {
            return Error{.code = ErrorCode::InvalidArgument,
                         .message = "Push constant stage flags cannot be None"};
        }
        if (pc.size == 0) {
            return Error{.code = ErrorCode::InvalidArgument,
                         .message = "Push constant size cannot be zero"};
        }
        if ((pc.offset % 4) != 0 || (pc.size % 4) != 0) {
            return Error{.code = ErrorCode::InvalidArgument,
                         .message = "Push constant offset and size must be 4-byte aligned"};
        }
        if (pc.offset + pc.size > kMaxPushConstantSize) {
            return Error{.code = ErrorCode::InvalidArgument,
                         .message = "Push constant range exceeds maximum guaranteed size (128B)"};
        }
    }
    return Result<void>::success();
}

/// @struct GraphicsPipelineDesc
/// @brief Complete specification for creating a graphics pipeline state object.
struct GraphicsPipelineDesc {
    ShaderHandle vertexShader{};
    ShaderHandle fragmentShader{};
    VertexInputLayout vertexInput{};
    PrimitiveTopology topology{PrimitiveTopology::TriangleList};
    PipelineRasterizationState rasterization{};
    PipelineDepthStencilState depthStencil{};
    std::vector<PipelineColorBlendAttachmentState> colorBlendAttachments{};
    PipelineLayoutHandle layout{};
    std::vector<TextureFormat> colorAttachmentFormats{};
    TextureFormat depthAttachmentFormat{TextureFormat::Undefined};
    std::string_view debugName{};
};

/// Validates a GraphicsPipelineDesc.
[[nodiscard]] inline Result<void>
validateGraphicsPipelineDesc(const GraphicsPipelineDesc& desc) noexcept {
    if (!desc.vertexShader.isValid()) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Graphics pipeline requires a valid vertex shader"};
    }

    if (!desc.layout.isValid()) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Graphics pipeline requires a valid pipeline layout handle"};
    }

    // Validate vertex attributes point to existing bindings
    for (const auto& attr : desc.vertexInput.attributes) {
        bool bindingFound = false;
        for (const auto& bind : desc.vertexInput.bindings) {
            if (bind.binding == attr.binding) {
                bindingFound = true;
                break;
            }
        }
        if (!bindingFound) {
            return Error{.code = ErrorCode::InvalidArgument,
                         .message = "Vertex attribute references unbound vertex binding slot"};
        }
    }

    // Color attachments check
    if (desc.colorAttachmentFormats.empty() &&
        desc.depthAttachmentFormat == TextureFormat::Undefined) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message =
                         "Graphics pipeline must specify at least one color or depth attachment"};
    }

    return Result<void>::success();
}

} // namespace eng::rhi
