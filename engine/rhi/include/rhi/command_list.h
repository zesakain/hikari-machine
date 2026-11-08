/// @file command_list.h
/// @brief Abstract graphics and compute command recording interface.
///
/// Purpose:
/// Represents an abstract hardware command buffer. Allows recording drawing commands,
/// bindings, and buffer copy/update transfers without coupling callers to Vulkan, D3D12, or Metal.
///
/// Ownership and Lifetime:
/// Created by RhiDevice via createCommandList() and returned as std::unique_ptr<RhiCommandList>.
/// Must be submitted via RhiDevice::submit() or reset/destroyed before frame boundary.
///
/// Thread-safety:
/// Individual command lists are single-threaded (typically recorded within a job).
/// Multiple command lists can be recorded concurrently on separate worker threads.
///
/// Performance:
/// Methods record commands into contiguous memory buffers with zero heap allocation per call.

#pragma once

#include "rhi/rhi_types.h"
#include "rhi/texture.h"

#include <cstddef>
#include <cstdint>

namespace eng::rhi {

/// @struct RenderPassBeginInfo
/// @brief Initialization parameters for beginning a dynamic render pass.
struct RenderPassBeginInfo {
    /// Target color texture, or invalid handle for default presentation swapchain backbuffer.
    TextureHandle colorTarget{};

    /// Optional target depth texture, or invalid handle if no depth attachment is bound.
    TextureHandle depthTarget{};

    /// Clear color (RGBA float), default deep Hikari Blue.
    float clearColor[4]{0.08F, 0.12F, 0.20F, 1.0F};

    /// Clear depth value (default 1.0F for standard depth buffer).
    float clearDepth{1.0F};

    /// Whether to clear color target on pass entry.
    bool clearTarget{true};

    /// Whether to clear depth target on pass entry.
    bool clearDepthTarget{true};

    /// Optional custom render area width (0 = use target dimensions).
    uint32_t renderWidth{0};

    /// Optional custom render area height (0 = use target dimensions).
    uint32_t renderHeight{0};
};

/// @struct NativeCommandListHandles
/// @brief Opaque container for backend-specific native command buffer handles.
struct NativeCommandListHandles {
    void* commandBuffer{nullptr};
};

/// @class RhiCommandList
/// @brief Abstract command recording buffer interface.
class RhiCommandList {
public:
    virtual ~RhiCommandList() = default;

    /// Begins command recording.
    virtual void begin() = 0;

    /// Ends command recording, sealing the command list for queue submission.
    virtual void end() = 0;

    /// Begins a dynamic rendering pass targeting the active swapchain image or color target.
    virtual void beginRenderPass(const RenderPassBeginInfo& info) = 0;

    /// Ends the current dynamic rendering pass and transitions target for presentation.
    virtual void endRenderPass() = 0;

    /// Clears a rectangular region of the active render target with a color.
    virtual void clearRect(int32_t x, int32_t y, uint32_t width, uint32_t height, float r, float g,
                           float b, float a) = 0;

    /// Sets the active viewport transformation.
    virtual void setViewport(float x, float y, float width, float height, float minDepth = 0.0F,
                             float maxDepth = 1.0F) = 0;

    /// Sets the active scissor test rectangle.
    virtual void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) = 0;

    /// Binds an active graphics pipeline for subsequent draw calls.
    virtual void bindPipeline(PipelineHandle pipeline) = 0;

    /// Updates push constant block data for the active pipeline.
    virtual void pushConstants(PipelineLayoutHandle layout, ShaderStageFlags stages,
                               uint32_t offset, uint32_t size, const void* data) = 0;

    /// Binds a vertex buffer to a specific binding slot.
    virtual void bindVertexBuffer(uint32_t binding, BufferHandle buffer, size_t offset = 0) = 0;

    /// Binds an index buffer for indexed drawing.
    virtual void bindIndexBuffer(BufferHandle buffer, size_t offset = 0, bool is32Bit = false) = 0;

    /// Issues a non-indexed primitive draw call.
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0,
                      uint32_t firstInstance = 0) = 0;

    /// Issues an indexed primitive draw call.
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                             uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                             uint32_t firstInstance = 0) = 0;

    /// Records a GPU-to-GPU memory copy between two buffers.
    virtual void copyBuffer(BufferHandle src, BufferHandle dst, size_t srcOffset, size_t dstOffset,
                            size_t size) = 0;

    /// Records a GPU transfer copying an entire 2D texture level to a destination buffer.
    ///
    /// @param srcTexture Valid handle of the source texture.
    /// @param dstBuffer Valid handle of the destination buffer with sufficient capacity.
    virtual void copyTextureToBuffer(TextureHandle srcTexture, BufferHandle dstBuffer) {
        (void)srcTexture;
        (void)dstBuffer;
    }

    /// Updates a destination buffer with host CPU data inline.
    virtual void updateBuffer(BufferHandle dst, size_t dstOffset, const void* data,
                              size_t size) = 0;

    /// Returns opaque pointers to underlying backend command buffer handles (or null in
    /// headless/null mode).
    [[nodiscard]] virtual NativeCommandListHandles getNativeCommandListHandles() const noexcept {
        return {};
    }
};

} // namespace eng::rhi
