/// @file vulkan_command_list.cpp
/// @brief Vulkan RhiCommandList command recording and staging buffer implementation.

#include "vulkan_command_list.h"

#include "vulkan_device.h"

#include <cstring>

namespace eng::rhi::vk {

VulkanCommandList::VulkanCommandList(VulkanDevice& device, VkCommandPool pool,
                                     VkCommandBuffer cmdBuffer) noexcept
    : device_(&device), pool_(pool), cmdBuffer_(cmdBuffer) {
    if (device_ != nullptr) {
        device_->registerCommandList(this);
    }
}

VulkanCommandList::~VulkanCommandList() noexcept {
    if (device_ != nullptr) {
        cleanupStagingResources();
        if (cmdBuffer_ != VK_NULL_HANDLE && pool_ != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device_->getLogicalDevice(), pool_, 1, &cmdBuffer_);
            cmdBuffer_ = VK_NULL_HANDLE;
        }
        device_->unregisterCommandList(this);
        device_ = nullptr;
    }
}

void VulkanCommandList::onDeviceDestroyed() noexcept {
    if (device_ != nullptr) {
        cleanupStagingResources();
        cmdBuffer_ = VK_NULL_HANDLE;
        pool_ = VK_NULL_HANDLE;
        device_ = nullptr;
    }
}

void VulkanCommandList::begin() {
    if (device_ == nullptr || cmdBuffer_ == VK_NULL_HANDLE) {
        return;
    }
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
    if (isRecording_ && device_ != nullptr && cmdBuffer_ != VK_NULL_HANDLE) {
        if (isInsideRenderPass_) {
            endRenderPass();
        }
        vkEndCommandBuffer(cmdBuffer_);
        isRecording_ = false;
    }
}

void VulkanCommandList::beginRenderPass(const RenderPassBeginInfo& info) {
    if (!isRecording_ || device_ == nullptr || cmdBuffer_ == VK_NULL_HANDLE) {
        return;
    }

    auto* swapchain = device_->getSwapchain();
    if (swapchain == nullptr) {
        return;
    }

    const uint32_t imgIndex = device_->getCurrentSwapchainImageIndex();
    VkImage swapImage = swapchain->getImage(imgIndex);
    VkImageView swapView = swapchain->getImageView(imgIndex);
    VkExtent2D extent = swapchain->getExtent();

    if (swapImage == VK_NULL_HANDLE || swapView == VK_NULL_HANDLE) {
        return;
    }

    currentRenderImage_ = swapImage;

    // Transition swapchain image: UNDEFINED/PRESENT_SRC -> COLOR_ATTACHMENT_OPTIMAL
    VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapImage,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    VkDependencyInfo depInfo{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = nullptr,
        .dependencyFlags = 0,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = nullptr,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers = nullptr,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };

    vkCmdPipelineBarrier2(cmdBuffer_, &depInfo);

    VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = swapView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = info.clearTarget ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue =
            {
                .color =
                    {
                        .float32 = {info.clearColor[0], info.clearColor[1], info.clearColor[2],
                                    info.clearColor[3]},
                    },
            },
    };

    VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderArea =
            {
                .offset = {0, 0},
                .extent = extent,
            },
        .layerCount = 1,
        .viewMask = 0,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = nullptr,
        .pStencilAttachment = nullptr,
    };

    vkCmdBeginRendering(cmdBuffer_, &renderingInfo);
    isInsideRenderPass_ = true;

    setViewport(0.0F, 0.0F, static_cast<float>(extent.width), static_cast<float>(extent.height),
                0.0F, 1.0F);
    setScissor(0, 0, extent.width, extent.height);
}

void VulkanCommandList::endRenderPass() {
    if (!isRecording_ || !isInsideRenderPass_ || device_ == nullptr ||
        cmdBuffer_ == VK_NULL_HANDLE) {
        return;
    }

    vkCmdEndRendering(cmdBuffer_);
    isInsideRenderPass_ = false;

    if (currentRenderImage_ != VK_NULL_HANDLE) {
        // Transition swapchain image: COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR
        VkImageMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = currentRenderImage_,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };

        VkDependencyInfo depInfo{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        };

        vkCmdPipelineBarrier2(cmdBuffer_, &depInfo);
        currentRenderImage_ = VK_NULL_HANDLE;
    }
}

void VulkanCommandList::clearRect(int32_t x, int32_t y, uint32_t width, uint32_t height, float r,
                                  float g, float b, float a) {
    if (!isRecording_ || !isInsideRenderPass_ || device_ == nullptr ||
        cmdBuffer_ == VK_NULL_HANDLE || width == 0 || height == 0) {
        return;
    }

    VkClearAttachment clearAttachment{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .colorAttachment = 0,
        .clearValue =
            {
                .color =
                    {
                        .float32 = {r, g, b, a},
                    },
            },
    };

    VkClearRect clearRect{
        .rect =
            {
                .offset =
                    {
                        .x = std::max(0, x),
                        .y = std::max(0, y),
                    },
                .extent =
                    {
                        .width = width,
                        .height = height,
                    },
            },
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    vkCmdClearAttachments(cmdBuffer_, 1, &clearAttachment, 1, &clearRect);
}

void VulkanCommandList::setViewport(float x, float y, float width, float height, float minDepth,
                                    float maxDepth) {
    if (!isRecording_ || device_ == nullptr || cmdBuffer_ == VK_NULL_HANDLE) {
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
    if (!isRecording_ || device_ == nullptr || cmdBuffer_ == VK_NULL_HANDLE) {
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
    if (!isRecording_ || device_ == nullptr || cmdBuffer_ == VK_NULL_HANDLE) {
        return;
    }
    VkBuffer vkBuf = device_->resolveBuffer(buffer);
    if (vkBuf != VK_NULL_HANDLE) {
        VkDeviceSize offsets[] = {static_cast<VkDeviceSize>(offset)};
        vkCmdBindVertexBuffers(cmdBuffer_, binding, 1, &vkBuf, offsets);
    }
}

void VulkanCommandList::bindIndexBuffer(BufferHandle buffer, size_t offset, bool is32Bit) {
    if (!isRecording_ || device_ == nullptr || cmdBuffer_ == VK_NULL_HANDLE) {
        return;
    }
    VkBuffer vkBuf = device_->resolveBuffer(buffer);
    if (vkBuf != VK_NULL_HANDLE) {
        vkCmdBindIndexBuffer(cmdBuffer_, vkBuf, static_cast<VkDeviceSize>(offset),
                             is32Bit ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
    }
}

void VulkanCommandList::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                             uint32_t firstInstance) {
    if (!isRecording_ || device_ == nullptr || cmdBuffer_ == VK_NULL_HANDLE) {
        return;
    }
    vkCmdDraw(cmdBuffer_, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandList::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                    uint32_t firstIndex, int32_t vertexOffset,
                                    uint32_t firstInstance) {
    if (!isRecording_ || device_ == nullptr || cmdBuffer_ == VK_NULL_HANDLE) {
        return;
    }
    vkCmdDrawIndexed(cmdBuffer_, indexCount, instanceCount, firstIndex, vertexOffset,
                     firstInstance);
}

void VulkanCommandList::copyBuffer(BufferHandle src, BufferHandle dst, size_t srcOffset,
                                   size_t dstOffset, size_t size) {
    if (!isRecording_ || device_ == nullptr || cmdBuffer_ == VK_NULL_HANDLE || size == 0) {
        return;
    }
    VkBuffer srcBuf = device_->resolveBuffer(src);
    VkBuffer dstBuf = device_->resolveBuffer(dst);
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
    if (!isRecording_ || device_ == nullptr || cmdBuffer_ == VK_NULL_HANDLE || data == nullptr ||
        size == 0) {
        return;
    }

    VkBuffer dstBuf = device_->resolveBuffer(dst);
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
        vkCreateBuffer(device_->getLogicalDevice(), &stagingInfo, nullptr, &stagingBuffer);
    if (res != VK_SUCCESS) {
        return;
    }

    void* mapped = nullptr;
    auto memRes = device_->getMemoryManager().allocateBufferMemory(
        device_->getLogicalDevice(), stagingBuffer, MemoryUsage::CpuToGpu, &mapped);
    if (!memRes.hasValue() || mapped == nullptr) {
        vkDestroyBuffer(device_->getLogicalDevice(), stagingBuffer, nullptr);
        return;
    }

    std::memcpy(mapped, data, size);
    vkUnmapMemory(device_->getLogicalDevice(), memRes.value());

    VkBufferCopy copyRegion{
        .srcOffset = 0,
        .dstOffset = static_cast<VkDeviceSize>(dstOffset),
        .size = static_cast<VkDeviceSize>(size),
    };
    vkCmdCopyBuffer(cmdBuffer_, stagingBuffer, dstBuf, 1, &copyRegion);

    stagingBuffers_.push_back(StagingBuffer{.buffer = stagingBuffer, .memory = memRes.value()});
}

void VulkanCommandList::cleanupStagingResources() noexcept {
    if (device_ == nullptr || device_->getLogicalDevice() == VK_NULL_HANDLE) {
        stagingBuffers_.clear();
        return;
    }
    for (auto& staging : stagingBuffers_) {
        if (staging.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_->getLogicalDevice(), staging.buffer, nullptr);
        }
        if (staging.memory != VK_NULL_HANDLE) {
            VulkanMemoryManager::freeMemory(device_->getLogicalDevice(), staging.memory);
        }
    }
    stagingBuffers_.clear();
}

NativeCommandListHandles VulkanCommandList::getNativeCommandListHandles() const noexcept {
    return NativeCommandListHandles{
        .commandBuffer = cmdBuffer_,
    };
}

} // namespace eng::rhi::vk
