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

#include <cstddef>
#include <cstdint>

namespace eng::rhi {

/// @class RhiCommandList
/// @brief Abstract command recording buffer interface.
class RhiCommandList {
public:
    virtual ~RhiCommandList() = default;

    /// Begins command recording.
    virtual void begin() = 0;

    /// Ends command recording, sealing the command list for queue submission.
    virtual void end() = 0;

    /// Sets the active viewport transformation.
    virtual void setViewport(float x, float y, float width, float height, float minDepth = 0.0F,
                             float maxDepth = 1.0F) = 0;

    /// Sets the active scissor test rectangle.
    virtual void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) = 0;

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

    /// Updates a destination buffer with host CPU data inline.
    virtual void updateBuffer(BufferHandle dst, size_t dstOffset, const void* data,
                              size_t size) = 0;
};

} // namespace eng::rhi
