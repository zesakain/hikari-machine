/// @file vulkan_command_list.cpp
/// @brief Vulkan RhiCommandList command recording and staging buffer implementation.

#include "vulkan_command_list.h"

#include "vulkan_device.h"

#include <cstring>

namespace eng::rhi::vk {

VulkanCommandList::VulkanCommandList(VulkanDevice& device, VkCommandPool pool,
                                     VkCommandBuffer cmdBuffer) noexcept
    : device_(device), pool_(pool), cmdBuffer_(cmdBuffer) {}

VulkanCommandList::~VulkanCommandList() noexcept {
    cleanupStagingResources();
    if (cmdBuffer_ != VK_NULL_HANDLE && pool_ != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device_.getLogicalDevice(), pool_, 1, &cmdBuffer_);
        cmdBuffer_ = VK_NULL_HANDLE;
    }
}

void VulkanCommandList::begin() {
    cleanupStagingResources();
    vkResetCommandBuffer(cmdBuffer_, 0);

    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };

    vkBeginCommandBuffer(cmdBuffer_, &beginInfo);
    isRecording_ = true;
}

void VulkanCommandList::end() {
    if (isRecording_) {
        vkEndCommandBuffer(cmdBuffer_);
        isRecording_ = false;
    }
}

void VulkanCommandList::setViewport(float x, float y, float width, float height, float minDepth,
                                    float maxDepth) {
    if (!isRecording_) {
        return;
    }
    VkViewport vp{
        .x = x,
        .y = y + height,
        .width = width,
        .height = -height, // Negative height for standard Y-up viewport convention
        .minDepth = minDepth,
        .maxDepth = maxDepth,
    };
    vkCmdSetViewport(cmdBuffer_, 0, 1, &vp);
}

void VulkanCommandList::setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    if (!isRecording_) {
        return;
    }
    VkRect2D scissor{
        .offset =
            {
                .x = x,
                .y = y,
            },
        .extent =
            {
                .width = width,
                .height = height,
            },
    };
    vkCmdSetScissor(cmdBuffer_, 0, 1, &scissor);
}

void VulkanCommandList::bindVertexBuffer(uint32_t binding, BufferHandle buffer, size_t offset) {
    if (!isRecording_) {
        return;
    }
    VkBuffer vkBuf = device_.resolveBuffer(buffer);
    if (vkBuf != VK_NULL_HANDLE) {
        VkDeviceSize offsets[] = {static_cast<VkDeviceSize>(offset)};
        vkCmdBindVertexBuffers(cmdBuffer_, binding, 1, &vkBuf, offsets);
    }
}

void VulkanCommandList::bindIndexBuffer(BufferHandle buffer, size_t offset, bool is32Bit) {
    if (!isRecording_) {
        return;
    }
    VkBuffer vkBuf = device_.resolveBuffer(buffer);
    if (vkBuf != VK_NULL_HANDLE) {
        vkCmdBindIndexBuffer(cmdBuffer_, vkBuf, static_cast<VkDeviceSize>(offset),
                             is32Bit ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
    }
}

void VulkanCommandList::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                             uint32_t firstInstance) {
    if (!isRecording_) {
        return;
    }
    vkCmdDraw(cmdBuffer_, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandList::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                    uint32_t firstIndex, int32_t vertexOffset,
                                    uint32_t firstInstance) {
    if (!isRecording_) {
        return;
    }
    vkCmdDrawIndexed(cmdBuffer_, indexCount, instanceCount, firstIndex, vertexOffset,
                     firstInstance);
}

void VulkanCommandList::copyBuffer(BufferHandle src, BufferHandle dst, size_t srcOffset,
                                   size_t dstOffset, size_t size) {
    if (!isRecording_ || size == 0) {
        return;
    }
    VkBuffer srcBuf = device_.resolveBuffer(src);
    VkBuffer dstBuf = device_.resolveBuffer(dst);
    if (srcBuf != VK_NULL_HANDLE && dstBuf != VK_NULL_HANDLE) {
        VkBufferCopy copyRegion{
            .srcOffset = static_cast<VkDeviceSize>(srcOffset),
            .dstOffset = static_cast<VkDeviceSize>(dstOffset),
            .size = static_cast<VkDeviceSize>(size),
        };
        vkCmdCopyBuffer(cmdBuffer_, srcBuf, dstBuf, 1, &copyRegion);
    }
}

void VulkanCommandList::updateBuffer(BufferHandle dst, size_t dstOffset, const void* data,
                                     size_t size) {
    if (!isRecording_ || data == nullptr || size == 0) {
        return;
    }

    VkBuffer dstBuf = device_.resolveBuffer(dst);
    if (dstBuf == VK_NULL_HANDLE) {
        return;
    }

    // Allocate temporary host-visible staging buffer
    VkBufferCreateInfo stagingInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkResult res =
        vkCreateBuffer(device_.getLogicalDevice(), &stagingInfo, nullptr, &stagingBuffer);
    if (res != VK_SUCCESS) {
        return;
    }

    void* mapped = nullptr;
    auto memRes = device_.getMemoryManager().allocateBufferMemory(
        device_.getLogicalDevice(), stagingBuffer, MemoryUsage::CpuToGpu, &mapped);
    if (!memRes.hasValue() || mapped == nullptr) {
        vkDestroyBuffer(device_.getLogicalDevice(), stagingBuffer, nullptr);
        return;
    }

    std::memcpy(mapped, data, size);
    vkUnmapMemory(device_.getLogicalDevice(), memRes.value());

    VkBufferCopy copyRegion{
        .srcOffset = 0,
        .dstOffset = static_cast<VkDeviceSize>(dstOffset),
        .size = static_cast<VkDeviceSize>(size),
    };
    vkCmdCopyBuffer(cmdBuffer_, stagingBuffer, dstBuf, 1, &copyRegion);

    stagingBuffers_.push_back(StagingBuffer{.buffer = stagingBuffer, .memory = memRes.value()});
}

void VulkanCommandList::cleanupStagingResources() noexcept {
    for (auto& staging : stagingBuffers_) {
        if (staging.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_.getLogicalDevice(), staging.buffer, nullptr);
        }
        if (staging.memory != VK_NULL_HANDLE) {
            VulkanMemoryManager::freeMemory(device_.getLogicalDevice(), staging.memory);
        }
    }
    stagingBuffers_.clear();
}

} // namespace eng::rhi::vk
