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

#include "rhi/descriptor.h"
#include "rhi/rhi_types.h"
#include "rhi/texture.h"

#include <cstddef>
#include <cstdint>
#include <span>

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

    /// Binds descriptor sets to the active graphics pipeline.
    ///
    /// @param layout Pipeline layout defining descriptor set interface.
    /// @param firstSet Index of the first descriptor set to bind (0-based).
    /// @param descriptorSets Span of valid descriptor set handles to bind sequentially.
    /// @param dynamicOffsets Optional array of dynamic buffer offsets.
    virtual void bindDescriptorSets(PipelineLayoutHandle layout, uint32_t firstSet,
                                    std::span<const DescriptorSetHandle> descriptorSets,
                                    std::span<const uint32_t> dynamicOffsets = {}) {
        (void)layout;
        (void)firstSet;
        (void)descriptorSets;
        (void)dynamicOffsets;
    }

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

    /// Records a transfer from a buffer to a texture subresource level.
    ///
    /// @param srcBuffer Valid handle of the source buffer.
    /// @param dstTexture Valid handle of the destination texture.
    /// @param bufferOffset Byte offset within the source buffer.
    /// @param mipLevel Target mip level (0-based).
    /// @param arrayLayer Target array layer (0-based).
    virtual void copyBufferToTexture(BufferHandle srcBuffer, TextureHandle dstTexture,
                                     size_t bufferOffset = 0, uint32_t mipLevel = 0,
                                     uint32_t arrayLayer = 0) {
        (void)srcBuffer;
        (void)dstTexture;
        (void)bufferOffset;
        (void)mipLevel;
        (void)arrayLayer;
    }

    /// Records an image memory barrier transitioning a texture subresource range between layouts.
    ///
    /// @param texture Valid handle of the target texture.
    /// @param oldLayout Previous texture layout state.
    /// @param newLayout Target texture layout state.
    /// @param baseMipLevel Starting mip level for the transition.
    /// @param mipLevelCount Number of mip levels to transition.
    /// @param baseArrayLayer Starting array layer for the transition.
    /// @param arrayLayerCount Number of array layers to transition.
    virtual void transitionTextureLayout(TextureHandle texture, TextureLayout oldLayout,
                                         TextureLayout newLayout, uint32_t baseMipLevel = 0,
                                         uint32_t mipLevelCount = 1, uint32_t baseArrayLayer = 0,
                                         uint32_t arrayLayerCount = 1) {
        (void)texture;
        (void)oldLayout;
        (void)newLayout;
        (void)baseMipLevel;
        (void)mipLevelCount;
        (void)baseArrayLayer;
        (void)arrayLayerCount;
    }

    /// Updates a destination texture level directly with host CPU memory inline.
    ///
    /// @param dst Valid handle of the destination texture.
    /// @param data Pointer to host CPU pixel data.
    /// @param size Total byte size of the pixel data to upload.
    /// @param mipLevel Destination mip level (0-based).
    /// @param arrayLayer Destination array layer (0-based).
    virtual void updateTexture(TextureHandle dst, const void* data, size_t size,
                               uint32_t mipLevel = 0, uint32_t arrayLayer = 0) {
        (void)dst;
        (void)data;
        (void)size;
        (void)mipLevel;
        (void)arrayLayer;
    }

    /// Generates complete mipmap chain for a texture using GPU blit operations when supported.
    ///
    /// @param texture Valid handle of the target texture with mipLevels > 1.
    virtual void generateMipmaps(TextureHandle texture) { (void)texture; }

    /// Updates a destination buffer with host CPU data inline.
    virtual void updateBuffer(BufferHandle dst, size_t dstOffset, const void* data,
                              size_t size) = 0;

    /// Records a GPU hardware timestamp into the specified query slot.
    ///
    /// Contract:
    /// - queryIndex must fall within allocated query pool capacity.
    /// - Query slot must have been reset prior to recording in this command buffer.
    ///
    /// Cost:
    /// Zero heap allocations; records inline into underlying command buffer. Allowed in hot path.
    ///
    /// @param queryIndex Zero-based query index in the active query pool.
    /// @param stage Pipeline boundary to sample (TopOfPipe or BottomOfPipe).
    virtual void writeTimestamp(uint32_t queryIndex,
                                GpuTimestampStage stage = GpuTimestampStage::BottomOfPipe) {
        (void)queryIndex;
        (void)stage;
    }

    /// Resets a range of query pool slots for subsequent timestamp recording.
    ///
    /// Contract:
    /// - Must be called outside an active render pass.
    ///
    /// Cost:
    /// Zero heap allocations. Allowed in hot path.
    ///
    /// @param firstQuery Starting query slot index to reset.
    /// @param queryCount Number of consecutive query slots to reset.
    virtual void resetQueryPool(uint32_t firstQuery, uint32_t queryCount) {
        (void)firstQuery;
        (void)queryCount;
    }

    /// Returns opaque pointers to underlying backend command buffer handles (or null in
    /// headless/null mode).
    [[nodiscard]] virtual NativeCommandListHandles getNativeCommandListHandles() const noexcept {
        return {};
    }
};

} // namespace eng::rhi
