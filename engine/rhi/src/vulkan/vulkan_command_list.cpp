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

    boundPipelineHandle_ = {};
    boundPipeline_ = VK_NULL_HANDLE;
    boundLayoutHandle_ = {};
    boundLayout_ = VK_NULL_HANDLE;
    hasUnboundDraw_ = false;
    isInsideRenderPass_ = false;
    currentRenderImage_ = VK_NULL_HANDLE;
    activeColorTarget_ = {};
    activeDepthTarget_ = {};
    activeColorFormat_ = TextureFormat::Undefined;
    activeDepthFormat_ = TextureFormat::Undefined;
    hasFormatMismatch_ = false;
    pipelineBindCount_ = 0;
    pushConstantCount_ = 0;

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
    if (isInsideRenderPass_) {
        ENG_LOG_WARN("Vulkan", "beginRenderPass ignored: already inside an active render pass");
        return;
    }

    if (info.colorTarget.isValid()) {
        // Offscreen Render Target path
        if (!device_->isTextureValid(info.colorTarget)) {
            ENG_LOG_ERROR("Vulkan",
                          "beginRenderPass failed: invalid offscreen color target handle");
            return;
        }
        const auto* colorDesc = device_->getTextureDesc(info.colorTarget);
        if (colorDesc == nullptr ||
            !hasFlag(colorDesc->usage, TextureUsageFlags::ColorAttachment) ||
            !isColorFormat(colorDesc->format)) {
            ENG_LOG_ERROR("Vulkan",
                          "beginRenderPass failed: colorTarget lacks ColorAttachment usage or "
                          "is not a color format");
            return;
        }

        bool hasDepth = false;
        const TextureDesc* depthDesc = nullptr;
        if (info.depthTarget.isValid()) {
            if (!device_->isTextureValid(info.depthTarget)) {
                ENG_LOG_ERROR("Vulkan",
                              "beginRenderPass failed: invalid offscreen depth target handle");
                return;
            }
            depthDesc = device_->getTextureDesc(info.depthTarget);
            if (depthDesc == nullptr ||
                !hasFlag(depthDesc->usage, TextureUsageFlags::DepthStencilAttachment) ||
                !isDepthFormat(depthDesc->format)) {
                ENG_LOG_ERROR("Vulkan",
                              "beginRenderPass failed: depthTarget lacks DepthStencilAttachment "
                              "usage or is not a depth format");
                return;
            }
            if (depthDesc->width != colorDesc->width || depthDesc->height != colorDesc->height) {
                ENG_LOG_ERROR("Vulkan",
                              "beginRenderPass failed: color and depth attachment dimensions "
                              "mismatch");
                return;
            }
            hasDepth = true;
        }

        uint32_t width = (info.renderWidth > 0 && info.renderWidth <= colorDesc->width)
                             ? info.renderWidth
                             : colorDesc->width;
        uint32_t height = (info.renderHeight > 0 && info.renderHeight <= colorDesc->height)
                              ? info.renderHeight
                              : colorDesc->height;

        VkImage colorImage = device_->resolveTextureImage(info.colorTarget);
        VkImageView colorView = device_->resolveTextureView(info.colorTarget);
        if (colorImage == VK_NULL_HANDLE || colorView == VK_NULL_HANDLE) {
            ENG_LOG_ERROR("Vulkan", "beginRenderPass failed: resolved color image or view is null");
            return;
        }

        // Transition offscreen color image to COLOR_ATTACHMENT_OPTIMAL
        VkImageLayout oldColorLayout = device_->getTextureLayout(info.colorTarget);
        VkImageMemoryBarrier2 colorBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask = (oldColorLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                                 ? VK_ACCESS_2_SHADER_READ_BIT
                                 : 0,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask =
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
            .oldLayout = oldColorLayout,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = colorImage,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };

        VkImageMemoryBarrier2 depthBarrier{};
        VkImageView depthView = VK_NULL_HANDLE;
        if (hasDepth) {
            VkImage depthImage = device_->resolveTextureImage(info.depthTarget);
            depthView = device_->resolveTextureView(info.depthTarget);
            VkImageLayout oldDepthLayout = device_->getTextureLayout(info.depthTarget);
            depthBarrier = VkImageMemoryBarrier2{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext = nullptr,
                .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .srcAccessMask = (oldDepthLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
                                     ? VK_ACCESS_2_SHADER_READ_BIT
                                     : 0,
                .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                .oldLayout = oldDepthLayout,
                .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = depthImage,
                .subresourceRange =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
            };
        }

        VkImageMemoryBarrier2 barriers[2];
        barriers[0] = colorBarrier;
        uint32_t barrierCount = 1;
        if (hasDepth) {
            barriers[1] = depthBarrier;
            barrierCount = 2;
        }

        VkDependencyInfo depInfo{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = barrierCount,
            .pImageMemoryBarriers = barriers,
        };
        vkCmdPipelineBarrier2(cmdBuffer_, &depInfo);
        device_->setTextureLayout(info.colorTarget, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        if (hasDepth) {
            device_->setTextureLayout(info.depthTarget,
                                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        }

        VkRenderingAttachmentInfo colorAttachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext = nullptr,
            .imageView = colorView,
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

        VkRenderingAttachmentInfo depthAttachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext = nullptr,
            .imageView = depthView,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .resolveMode = VK_RESOLVE_MODE_NONE,
            .resolveImageView = VK_NULL_HANDLE,
            .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .loadOp =
                info.clearDepthTarget ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue =
                {
                    .depthStencil =
                        {
                            .depth = info.clearDepth,
                            .stencil = 0,
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
                    .extent = {width, height},
                },
            .layerCount = 1,
            .viewMask = 0,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
            .pDepthAttachment = hasDepth ? &depthAttachment : nullptr,
            .pStencilAttachment = nullptr,
        };

        vkCmdBeginRendering(cmdBuffer_, &renderingInfo);
        isInsideRenderPass_ = true;
        currentRenderImage_ = VK_NULL_HANDLE; // Offscreen, not swapchain image
        activeColorTarget_ = info.colorTarget;
        activeDepthTarget_ = info.depthTarget;
        activeColorFormat_ = colorDesc->format;
        activeDepthFormat_ = hasDepth ? depthDesc->format : TextureFormat::Undefined;

        setViewport(0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height), 0.0F, 1.0F);
        setScissor(0, 0, width, height);
        return;
    }

    // Default Swapchain Render Pass
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
    activeColorTarget_ = {};
    activeDepthTarget_ = {};
    activeColorFormat_ = TextureFormat::BGRA8_UNORM;
    activeDepthFormat_ = TextureFormat::Undefined;

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

    if (activeColorTarget_.isValid()) {
        // Offscreen Color Target transition: COLOR_ATTACHMENT_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
        VkImage colorImage = device_->resolveTextureImage(activeColorTarget_);
        if (colorImage != VK_NULL_HANDLE) {
            VkImageMemoryBarrier2 colorBarrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext = nullptr,
                .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = colorImage,
                .subresourceRange =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
            };

            VkImageMemoryBarrier2 depthBarrier{};
            bool hasDepth = false;
            if (activeDepthTarget_.isValid()) {
                VkImage depthImage = device_->resolveTextureImage(activeDepthTarget_);
                if (depthImage != VK_NULL_HANDLE) {
                    hasDepth = true;
                    depthBarrier = VkImageMemoryBarrier2{
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                        .pNext = nullptr,
                        .srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                        .srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
                        .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                        .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image = depthImage,
                        .subresourceRange =
                            {
                                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                                .baseMipLevel = 0,
                                .levelCount = 1,
                                .baseArrayLayer = 0,
                                .layerCount = 1,
                            },
                    };
                }
            }

            VkImageMemoryBarrier2 barriers[2];
            barriers[0] = colorBarrier;
            uint32_t count = 1;
            if (hasDepth) {
                barriers[1] = depthBarrier;
                count = 2;
            }

            VkDependencyInfo depInfo{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext = nullptr,
                .dependencyFlags = 0,
                .memoryBarrierCount = 0,
                .pMemoryBarriers = nullptr,
                .bufferMemoryBarrierCount = 0,
                .pBufferMemoryBarriers = nullptr,
                .imageMemoryBarrierCount = count,
                .pImageMemoryBarriers = barriers,
            };
            vkCmdPipelineBarrier2(cmdBuffer_, &depInfo);
            device_->setTextureLayout(activeColorTarget_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            if (hasDepth) {
                device_->setTextureLayout(activeDepthTarget_,
                                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            }
        }
        activeColorTarget_ = {};
        activeDepthTarget_ = {};
    } else if (currentRenderImage_ != VK_NULL_HANDLE) {
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

void VulkanCommandList::bindPipeline(PipelineHandle pipeline) {
    if (!isRecording_ || device_ == nullptr || cmdBuffer_ == VK_NULL_HANDLE) {
        return;
    }
    VkPipeline vkPipeline = device_->resolvePipeline(pipeline);
    if (vkPipeline != VK_NULL_HANDLE) {
        boundPipelineHandle_ = pipeline;
        boundPipeline_ = vkPipeline;
        const auto* pDesc = device_->getPipelineDesc(pipeline);
        if (pDesc != nullptr) {
            boundLayoutHandle_ = pDesc->layout;
            boundLayout_ = device_->resolvePipelineLayout(pDesc->layout);
        }
        vkCmdBindPipeline(cmdBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline);
        ++pipelineBindCount_;
    } else {
        boundPipelineHandle_ = {};
        boundPipeline_ = VK_NULL_HANDLE;
        boundLayoutHandle_ = {};
        boundLayout_ = VK_NULL_HANDLE;
    }
}

void VulkanCommandList::pushConstants(PipelineLayoutHandle layout, ShaderStageFlags stages,
                                      uint32_t offset, uint32_t size, const void* data) {
    if (!isRecording_ || device_ == nullptr || cmdBuffer_ == VK_NULL_HANDLE || data == nullptr ||
        size == 0) {
        return;
    }
    VkPipelineLayout vkLayout = device_->resolvePipelineLayout(layout);
    if (vkLayout != VK_NULL_HANDLE) {
        vkCmdPushConstants(cmdBuffer_, vkLayout, toVkShaderStageFlags(stages), offset, size, data);
        ++pushConstantCount_;
    }
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
    if (boundPipeline_ == VK_NULL_HANDLE) {
        hasUnboundDraw_ = true;
        ENG_LOG_WARN("Vulkan", "draw skipped: no graphics pipeline bound");
        return;
    }
    if (isInsideRenderPass_) {
        const auto* pDesc = device_->getPipelineDesc(boundPipelineHandle_);
        if (pDesc != nullptr) {
            if (!pDesc->colorAttachmentFormats.empty() &&
                pDesc->colorAttachmentFormats[0] != activeColorFormat_) {
                hasFormatMismatch_ = true;
                ENG_LOG_WARN("Vulkan",
                             "draw skipped: pipeline color format does not match active render "
                             "target format");
                return;
            }
            if (pDesc->depthAttachmentFormat != TextureFormat::Undefined &&
                pDesc->depthAttachmentFormat != activeDepthFormat_) {
                hasFormatMismatch_ = true;
                ENG_LOG_WARN("Vulkan",
                             "draw skipped: pipeline depth format does not match active render "
                             "target depth format");
                return;
            }
        }
    }
    vkCmdDraw(cmdBuffer_, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandList::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                    uint32_t firstIndex, int32_t vertexOffset,
                                    uint32_t firstInstance) {
    if (!isRecording_ || device_ == nullptr || cmdBuffer_ == VK_NULL_HANDLE) {
        return;
    }
    if (boundPipeline_ == VK_NULL_HANDLE) {
        hasUnboundDraw_ = true;
        ENG_LOG_WARN("Vulkan", "drawIndexed skipped: no graphics pipeline bound");
        return;
    }
    if (isInsideRenderPass_) {
        const auto* pDesc = device_->getPipelineDesc(boundPipelineHandle_);
        if (pDesc != nullptr) {
            if (!pDesc->colorAttachmentFormats.empty() &&
                pDesc->colorAttachmentFormats[0] != activeColorFormat_) {
                hasFormatMismatch_ = true;
                ENG_LOG_WARN("Vulkan",
                             "drawIndexed skipped: pipeline color format does not match active "
                             "render target format");
                return;
            }
            if (pDesc->depthAttachmentFormat != TextureFormat::Undefined &&
                pDesc->depthAttachmentFormat != activeDepthFormat_) {
                hasFormatMismatch_ = true;
                ENG_LOG_WARN("Vulkan",
                             "drawIndexed skipped: pipeline depth format does not match active "
                             "render target depth format");
                return;
            }
        }
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

void VulkanCommandList::copyTextureToBuffer(TextureHandle srcTexture, BufferHandle dstBuffer) {
    if (!isRecording_ || device_ == nullptr || cmdBuffer_ == VK_NULL_HANDLE) {
        return;
    }
    if (!device_->isTextureValid(srcTexture) || !device_->isBufferValid(dstBuffer)) {
        ENG_LOG_ERROR("Vulkan", "copyTextureToBuffer failed: invalid texture or buffer handle");
        return;
    }

    const auto* texDesc = device_->getTextureDesc(srcTexture);
    if (texDesc == nullptr) {
        return;
    }

    VkImage srcImage = device_->resolveTextureImage(srcTexture);
    VkBuffer dstBuf = device_->resolveBuffer(dstBuffer);
    if (srcImage == VK_NULL_HANDLE || dstBuf == VK_NULL_HANDLE) {
        return;
    }

    // Transition image to TRANSFER_SRC_OPTIMAL
    VkImageLayout oldLayout = device_->getTextureLayout(srcTexture);
    VkImageMemoryBarrier2 barrierToSrc{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask =
            (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                ? VK_ACCESS_2_SHADER_READ_BIT
                : (VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT),
        .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout = oldLayout,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = srcImage,
        .subresourceRange =
            {
                .aspectMask = isDepthFormat(texDesc->format) ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                             : VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    VkDependencyInfo depInfoToSrc{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = nullptr,
        .dependencyFlags = 0,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = nullptr,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers = nullptr,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrierToSrc,
    };
    vkCmdPipelineBarrier2(cmdBuffer_, &depInfoToSrc);

    VkBufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource =
            {
                .aspectMask = isDepthFormat(texDesc->format) ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                             : VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        .imageOffset = {0, 0, 0},
        .imageExtent = {texDesc->width, texDesc->height, 1},
    };
    vkCmdCopyImageToBuffer(cmdBuffer_, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstBuf, 1,
                           &region);

    // Transition back to oldLayout (or SHADER_READ_ONLY_OPTIMAL)
    VkImageLayout returnLayout = (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                     ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                     : oldLayout;
    VkImageMemoryBarrier2 barrierBack{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout = returnLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = srcImage,
        .subresourceRange = barrierToSrc.subresourceRange,
    };

    VkDependencyInfo depInfoBack{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = nullptr,
        .dependencyFlags = 0,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = nullptr,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers = nullptr,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrierBack,
    };
    vkCmdPipelineBarrier2(cmdBuffer_, &depInfoBack);
    device_->setTextureLayout(srcTexture, returnLayout);
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
