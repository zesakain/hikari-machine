/// @file vulkan_command_list.h
/// @brief Vulkan RhiCommandList implementation with synchronization2 barrier support.
///
/// Purpose:
/// Records draw calls, pipeline bindings, memory copies, and resource transitions into
/// an underlying VkCommandBuffer.
///
/// Ownership and Lifetime:
/// Created by VulkanDevice::createCommandList() via std::unique_ptr<RhiCommandList>.
///
/// Thread-safety:
/// Single-threaded during recording.

#pragma once

#include "rhi/command_list.h"
#include "vulkan_types.h"

#include <memory>
#include <vector>

namespace eng::rhi::vk {

class VulkanDevice;

/// @class VulkanCommandList
/// @brief Concrete implementation of RhiCommandList for the Vulkan backend.
class VulkanCommandList final : public RhiCommandList {
public:
    VulkanCommandList(VulkanDevice& device, VkCommandPool pool, VkCommandBuffer cmdBuffer) noexcept;
    ~VulkanCommandList() noexcept override;

    VulkanCommandList(const VulkanCommandList&) = delete;
    VulkanCommandList& operator=(const VulkanCommandList&) = delete;
    VulkanCommandList(VulkanCommandList&&) noexcept = delete;
    VulkanCommandList& operator=(VulkanCommandList&&) noexcept = delete;

    void begin() override;
    void end() override;

    void beginRenderPass(const RenderPassBeginInfo& info) override;
    void endRenderPass() override;
    void clearRect(int32_t x, int32_t y, uint32_t width, uint32_t height, float r, float g, float b,
                   float a) override;

    void setViewport(float x, float y, float width, float height, float minDepth = 0.0F,
                     float maxDepth = 1.0F) override;
    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) override;

    void bindPipeline(PipelineHandle pipeline) override;
    void pushConstants(PipelineLayoutHandle layout, ShaderStageFlags stages, uint32_t offset,
                       uint32_t size, const void* data) override;

    void bindVertexBuffer(uint32_t binding, BufferHandle buffer, size_t offset = 0) override;
    void bindIndexBuffer(BufferHandle buffer, size_t offset = 0, bool is32Bit = false) override;

    void draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0,
              uint32_t firstInstance = 0) override;
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0,
                     int32_t vertexOffset = 0, uint32_t firstInstance = 0) override;

    void copyBuffer(BufferHandle src, BufferHandle dst, size_t srcOffset, size_t dstOffset,
                    size_t size) override;
    void copyTextureToBuffer(TextureHandle srcTexture, BufferHandle dstBuffer) override;
    void updateBuffer(BufferHandle dst, size_t dstOffset, const void* data, size_t size) override;

    [[nodiscard]] NativeCommandListHandles getNativeCommandListHandles() const noexcept override;

    [[nodiscard]] VkCommandBuffer handle() const noexcept { return cmdBuffer_; }
    [[nodiscard]] PipelineHandle getBoundPipeline() const noexcept { return boundPipelineHandle_; }
    [[nodiscard]] bool isPipelineBound() const noexcept { return boundPipeline_ != VK_NULL_HANDLE; }
    [[nodiscard]] bool hasUnboundDraw() const noexcept { return hasUnboundDraw_; }
    [[nodiscard]] bool isInsideRenderPass() const noexcept { return isInsideRenderPass_; }
    [[nodiscard]] TextureHandle getActiveColorTarget() const noexcept { return activeColorTarget_; }
    [[nodiscard]] TextureHandle getActiveDepthTarget() const noexcept { return activeDepthTarget_; }
    [[nodiscard]] TextureFormat getActiveColorFormat() const noexcept { return activeColorFormat_; }
    [[nodiscard]] TextureFormat getActiveDepthFormat() const noexcept { return activeDepthFormat_; }
    [[nodiscard]] bool hasFormatMismatch() const noexcept { return hasFormatMismatch_; }
    [[nodiscard]] uint32_t getPipelineBindCount() const noexcept { return pipelineBindCount_; }
    [[nodiscard]] uint32_t getPushConstantCount() const noexcept { return pushConstantCount_; }

    void cleanupStagingResources() noexcept;
    void onDeviceDestroyed() noexcept;

private:
    VulkanDevice* device_{nullptr};
    VkCommandPool pool_{VK_NULL_HANDLE};
    VkCommandBuffer cmdBuffer_{VK_NULL_HANDLE};
    bool isRecording_{false};
    bool isInsideRenderPass_{false};
    VkImage currentRenderImage_{VK_NULL_HANDLE};
    TextureHandle activeColorTarget_{};
    TextureHandle activeDepthTarget_{};
    TextureFormat activeColorFormat_{TextureFormat::Undefined};
    TextureFormat activeDepthFormat_{TextureFormat::Undefined};
    bool hasFormatMismatch_{false};

    PipelineHandle boundPipelineHandle_{};
    VkPipeline boundPipeline_{VK_NULL_HANDLE};
    PipelineLayoutHandle boundLayoutHandle_{};
    VkPipelineLayout boundLayout_{VK_NULL_HANDLE};
    bool hasUnboundDraw_{false};
    uint32_t pipelineBindCount_{0};
    uint32_t pushConstantCount_{0};

    struct StagingBuffer {
        VkBuffer buffer{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
    };
    std::vector<StagingBuffer> stagingBuffers_;
};

} // namespace eng::rhi::vk
