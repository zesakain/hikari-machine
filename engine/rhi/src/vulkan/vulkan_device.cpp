/// @file vulkan_device.cpp
/// @brief Concrete Vulkan RhiDevice implementation.

#include "vulkan_device.h"

#include "vulkan_command_list.h"

#include <SDL3/SDL_vulkan.h>
#include <algorithm>

namespace eng::rhi {

Result<std::unique_ptr<RhiDevice>> createVulkanDevice(const VulkanDeviceDesc& desc) {
    auto devRes = vk::VulkanDevice::create(desc);
    if (!devRes.hasValue()) {
        return devRes.error();
    }
    return std::unique_ptr<RhiDevice>(std::move(devRes.value()));
}

} // namespace eng::rhi

namespace eng::rhi::vk {

VulkanDevice::VulkanDevice(std::unique_ptr<VulkanInstance> instance,
                           std::unique_ptr<VulkanPhysicalDevice> physicalDevice, VkDevice device,
                           VkQueue graphicsQueue, VkQueue presentQueue,
                           uint32_t graphicsQueueFamily, VkCommandPool commandPool,
                           std::unique_ptr<VulkanMemoryManager> memoryManager,
                           std::unique_ptr<VulkanSwapchain> swapchain) noexcept
    : instance_(std::move(instance)), physicalDevice_(std::move(physicalDevice)), device_(device),
      graphicsQueue_(graphicsQueue), presentQueue_(presentQueue),
      graphicsQueueFamily_(graphicsQueueFamily), commandPool_(commandPool),
      memoryManager_(std::move(memoryManager)), swapchain_(std::move(swapchain)) {}

VulkanDevice::~VulkanDevice() noexcept {
    waitIdle();

    // Detach all active command lists and clean up their staging resources
    // while the device is still valid and idle.
    {
        std::lock_guard<std::mutex> lock(commandListMutex_);
        for (auto* cmdList : commandLists_) {
            if (cmdList != nullptr) {
                cmdList->onDeviceDestroyed();
            }
        }
        commandLists_.clear();
    }

    for (auto& slot : buffers_) {
        if (slot.alive) {
            if (slot.buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device_, slot.buffer, nullptr);
            }
            if (slot.memory != VK_NULL_HANDLE) {
                VulkanMemoryManager::freeMemory(device_, slot.memory);
            }
            slot.alive = false;
        }
    }
    buffers_.clear();

    for (auto& slot : textures_) {
        if (slot.alive) {
            if (slot.view != VK_NULL_HANDLE) {
                vkDestroyImageView(device_, slot.view, nullptr);
            }
            if (slot.image != VK_NULL_HANDLE) {
                vkDestroyImage(device_, slot.image, nullptr);
            }
            if (slot.memory != VK_NULL_HANDLE) {
                VulkanMemoryManager::freeMemory(device_, slot.memory);
            }
            slot.alive = false;
        }
    }
    textures_.clear();

    for (auto& slot : samplers_) {
        if (slot.alive) {
            if (slot.sampler != VK_NULL_HANDLE) {
                vkDestroySampler(device_, slot.sampler, nullptr);
            }
            slot.alive = false;
        }
    }
    samplers_.clear();

    // Destroy swapchain before device
    swapchain_.reset();

    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
    }

    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
}

void VulkanDevice::registerCommandList(VulkanCommandList* cmdList) {
    if (cmdList == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(commandListMutex_);
    commandLists_.push_back(cmdList);
}

void VulkanDevice::unregisterCommandList(VulkanCommandList* cmdList) {
    if (cmdList == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(commandListMutex_);
    for (auto it = commandLists_.begin(); it != commandLists_.end(); ++it) {
        if (*it == cmdList) {
            commandLists_.erase(it);
            break;
        }
    }
}

Result<std::unique_ptr<VulkanDevice>> VulkanDevice::create(const VulkanDeviceDesc& desc) {
    // 1. Create VulkanInstance
    auto instRes = VulkanInstance::create(desc.enableValidation, desc.windowHandle != nullptr);
    if (!instRes.hasValue()) {
        return instRes.error();
    }
    auto instance = std::move(instRes.value());

    // 2. Create surface if window provided
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (desc.windowHandle != nullptr) {
        auto* sdlWindow = static_cast<SDL_Window*>(desc.windowHandle);
        if (!SDL_Vulkan_CreateSurface(sdlWindow, instance->handle(), nullptr, &surface) ||
            surface == VK_NULL_HANDLE) {
            ENG_LOG_ERROR("Vulkan", "Failed to create SDL Vulkan surface for device selection");
            return Error{.code = ErrorCode::SwapchainCreationFailed,
                         .message = "SDL_Vulkan_CreateSurface failed"};
        }
    }

    // 3. Select Physical Device
    auto physRes = VulkanPhysicalDevice::select(instance->handle(), surface);
    if (!physRes.hasValue()) {
        if (surface != VK_NULL_HANDLE) {
            SDL_Vulkan_DestroySurface(instance->handle(), surface, nullptr);
        }
        return physRes.error();
    }
    auto physicalDevice = std::move(physRes.value());
    const auto& queueFamilies = physicalDevice->getQueueFamilies();

    // 4. Logical Device and Queues
    float queuePriority = 1.0F;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

    VkDeviceQueueCreateInfo graphicsQueueInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = queueFamilies.graphicsFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };
    queueCreateInfos.push_back(graphicsQueueInfo);

    if (surface != VK_NULL_HANDLE && queueFamilies.presentFamily != queueFamilies.graphicsFamily) {
        VkDeviceQueueCreateInfo presentQueueInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = queueFamilies.presentFamily,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        };
        queueCreateInfos.push_back(presentQueueInfo);
    }

    std::vector<const char*> deviceExtensions;
    deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    if (surface != VK_NULL_HANDLE) {
        deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    // Enable dynamic rendering, timeline semaphore, and sync2 features
    VkPhysicalDeviceSynchronization2Features sync2Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
        .pNext = nullptr,
        .synchronization2 = VK_TRUE,
    };

    VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
        .pNext = &sync2Features,
        .timelineSemaphore = VK_TRUE,
    };

    VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        .pNext = &timelineFeatures,
        .dynamicRendering = VK_TRUE,
    };

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo deviceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &dynamicRenderingFeatures,
        .flags = 0,
        .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
        .pQueueCreateInfos = queueCreateInfos.data(),
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
        .ppEnabledExtensionNames = deviceExtensions.data(),
        .pEnabledFeatures = &deviceFeatures,
    };

    VkDevice device = VK_NULL_HANDLE;
    VkResult res = vkCreateDevice(physicalDevice->handle(), &deviceCreateInfo, nullptr, &device);
    if (res != VK_SUCCESS || device == VK_NULL_HANDLE) {
        if (surface != VK_NULL_HANDLE) {
            SDL_Vulkan_DestroySurface(instance->handle(), surface, nullptr);
        }
        ENG_LOG_ERROR("Vulkan", "Failed to create VkDevice");
        return Error{.code = ErrorCode::DeviceCreationFailed, .message = "vkCreateDevice failed"};
    }

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queueFamilies.graphicsFamily, 0, &graphicsQueue);

    VkQueue presentQueue = VK_NULL_HANDLE;
    if (surface != VK_NULL_HANDLE) {
        vkGetDeviceQueue(device, queueFamilies.presentFamily, 0, &presentQueue);
    }

    // 5. Command Pool
    VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueFamilies.graphicsFamily,
    };
    VkCommandPool commandPool = VK_NULL_HANDLE;
    res = vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);
    if (res != VK_SUCCESS) {
        vkDestroyDevice(device, nullptr);
        if (surface != VK_NULL_HANDLE) {
            SDL_Vulkan_DestroySurface(instance->handle(), surface, nullptr);
        }
        return Error{.code = ErrorCode::DeviceCreationFailed,
                     .message = "vkCreateCommandPool failed"};
    }

    // 6. Memory Manager
    auto memoryManager = std::make_unique<VulkanMemoryManager>(physicalDevice->handle());

    // 7. Swapchain if surface was created
    std::unique_ptr<VulkanSwapchain> swapchain;
    if (surface != VK_NULL_HANDLE) {
        // Destroy temporary surface and re-create it inside VulkanSwapchain cleanly
        SDL_Vulkan_DestroySurface(instance->handle(), surface, nullptr);
        surface = VK_NULL_HANDLE;

        auto swapRes = VulkanSwapchain::create(instance->handle(), physicalDevice->handle(), device,
                                               desc.windowHandle, queueFamilies.graphicsFamily,
                                               queueFamilies.presentFamily);
        if (swapRes.hasValue()) {
            swapchain = std::move(swapRes.value());
        } else {
            ENG_LOG_WARN("Vulkan", "Swapchain creation failed; continuing in headless mode");
        }
    }

    return std::unique_ptr<VulkanDevice>(new VulkanDevice(
        std::move(instance), std::move(physicalDevice), device, graphicsQueue, presentQueue,
        queueFamilies.graphicsFamily, commandPool, std::move(memoryManager), std::move(swapchain)));
}

const GpuCaps& VulkanDevice::getCaps() const noexcept {
    return physicalDevice_->getCaps();
}

Result<BufferHandle> VulkanDevice::createBuffer(const BufferDesc& desc) {
    if (desc.sizeInBytes == 0 ||
        desc.sizeInBytes > physicalDevice_->getCaps().limits.maxStorageBufferBindingSize) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Buffer size exceeds device limits or is zero"};
    }

    VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = desc.sizeInBytes,
        .usage = toVkBufferUsage(desc.usage) | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };

    VkBuffer buffer = VK_NULL_HANDLE;
    VkResult res = vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer);
    if (res != VK_SUCCESS) {
        return Error{.code = ErrorCode::ResourceCreationFailed, .message = "vkCreateBuffer failed"};
    }

    void* mappedData = nullptr;
    auto memRes =
        memoryManager_->allocateBufferMemory(device_, buffer, desc.memoryUsage, &mappedData);
    if (!memRes.hasValue()) {
        vkDestroyBuffer(device_, buffer, nullptr);
        return memRes.error();
    }

    // Allocate slot
    uint32_t slotIdx = UINT32_MAX;
    for (size_t i = 0; i < buffers_.size(); ++i) {
        if (!buffers_[i].alive) {
            slotIdx = static_cast<uint32_t>(i);
            break;
        }
    }

    if (slotIdx == UINT32_MAX) {
        slotIdx = static_cast<uint32_t>(buffers_.size());
        buffers_.push_back(VulkanBufferSlot{});
    }

    auto& slot = buffers_[slotIdx];
    slot.buffer = buffer;
    slot.memory = memRes.value();
    slot.sizeInBytes = desc.sizeInBytes;
    slot.usage = desc.usage;
    slot.memoryUsage = desc.memoryUsage;
    slot.alive = true;
    slot.mappedData = mappedData;

    return BufferHandle(GenerationalHandle{slotIdx, slot.generation});
}

void VulkanDevice::destroyBuffer(BufferHandle handle) {
    if (!isBufferValid(handle)) {
        return;
    }
    auto& slot = buffers_[handle.index()];
    if (slot.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, slot.buffer, nullptr);
        slot.buffer = VK_NULL_HANDLE;
    }
    if (slot.memory != VK_NULL_HANDLE) {
        VulkanMemoryManager::freeMemory(device_, slot.memory);
        slot.memory = VK_NULL_HANDLE;
    }
    slot.alive = false;
    slot.mappedData = nullptr;
    ++slot.generation;
}

Result<TextureHandle> VulkanDevice::createTexture(const TextureDesc& desc) {
    if (desc.width == 0 || desc.height == 0 || desc.depth == 0) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Texture dimensions must be greater than zero"};
    }

    const auto& limits = physicalDevice_->getCaps().limits;
    if (desc.width > limits.maxTextureDimension2D || desc.height > limits.maxTextureDimension2D) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Texture dimension exceeds hardware limits"};
    }

    VkFormat vkFormat = toVkFormat(desc.format);
    if (vkFormat == VK_FORMAT_UNDEFINED) {
        return Error{.code = ErrorCode::InvalidArgument, .message = "Unsupported texture format"};
    }

    VkImageType imageType = VK_IMAGE_TYPE_2D;
    if (desc.type == TextureType::Texture3D) {
        imageType = VK_IMAGE_TYPE_3D;
    }

    VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = static_cast<VkImageCreateFlags>(
            (desc.type == TextureType::TextureCube) ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0),
        .imageType = imageType,
        .format = vkFormat,
        .extent =
            {
                .width = desc.width,
                .height = desc.height,
                .depth = desc.depth,
            },
        .mipLevels = std::max(1U, desc.mipLevels),
        .arrayLayers = std::max(1U, desc.arrayLayers),
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = toVkImageUsage(desc.usage) | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkImage image = VK_NULL_HANDLE;
    VkResult res = vkCreateImage(device_, &imageInfo, nullptr, &image);
    if (res != VK_SUCCESS) {
        return Error{.code = ErrorCode::ResourceCreationFailed, .message = "vkCreateImage failed"};
    }

    auto memRes = memoryManager_->allocateImageMemory(device_, image, MemoryUsage::GpuOnly);
    if (!memRes.hasValue()) {
        vkDestroyImage(device_, image, nullptr);
        return memRes.error();
    }

    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if (desc.format == TextureFormat::D32_FLOAT ||
        desc.format == TextureFormat::D24_UNORM_S8_UINT ||
        desc.format == TextureFormat::D32_FLOAT_S8_UINT) {
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = image,
        .viewType = (desc.type == TextureType::TextureCube) ? VK_IMAGE_VIEW_TYPE_CUBE
                                                            : VK_IMAGE_VIEW_TYPE_2D,
        .format = vkFormat,
        .components =
            {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
        .subresourceRange =
            {
                .aspectMask = aspectMask,
                .baseMipLevel = 0,
                .levelCount = desc.mipLevels,
                .baseArrayLayer = 0,
                .layerCount = desc.arrayLayers,
            },
    };

    VkImageView view = VK_NULL_HANDLE;
    res = vkCreateImageView(device_, &viewInfo, nullptr, &view);
    if (res != VK_SUCCESS) {
        VulkanMemoryManager::freeMemory(device_, memRes.value());
        vkDestroyImage(device_, image, nullptr);
        return Error{.code = ErrorCode::ResourceCreationFailed,
                     .message = "vkCreateImageView failed"};
    }

    // Allocate slot
    uint32_t slotIdx = UINT32_MAX;
    for (size_t i = 0; i < textures_.size(); ++i) {
        if (!textures_[i].alive) {
            slotIdx = static_cast<uint32_t>(i);
            break;
        }
    }

    if (slotIdx == UINT32_MAX) {
        slotIdx = static_cast<uint32_t>(textures_.size());
        textures_.push_back(VulkanTextureSlot{});
    }

    auto& slot = textures_[slotIdx];
    slot.image = image;
    slot.view = view;
    slot.memory = memRes.value();
    slot.desc = desc;
    slot.alive = true;

    return TextureHandle(GenerationalHandle{slotIdx, slot.generation});
}

void VulkanDevice::destroyTexture(TextureHandle handle) {
    if (!isTextureValid(handle)) {
        return;
    }
    auto& slot = textures_[handle.index()];
    if (slot.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, slot.view, nullptr);
        slot.view = VK_NULL_HANDLE;
    }
    if (slot.image != VK_NULL_HANDLE) {
        vkDestroyImage(device_, slot.image, nullptr);
        slot.image = VK_NULL_HANDLE;
    }
    if (slot.memory != VK_NULL_HANDLE) {
        VulkanMemoryManager::freeMemory(device_, slot.memory);
        slot.memory = VK_NULL_HANDLE;
    }
    slot.alive = false;
    ++slot.generation;
}

Result<SamplerHandle> VulkanDevice::createSampler(const SamplerDesc& desc) {
    VkSamplerCreateInfo samplerInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .magFilter = toVkFilter(desc.magFilter),
        .minFilter = toVkFilter(desc.minFilter),
        .mipmapMode = toVkSamplerMipmapMode(desc.mipmapMode),
        .addressModeU = toVkSamplerAddressMode(desc.addressU),
        .addressModeV = toVkSamplerAddressMode(desc.addressV),
        .addressModeW = toVkSamplerAddressMode(desc.addressW),
        .mipLodBias = 0.0F,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0F,
        .compareEnable = desc.compareEnable ? VK_TRUE : VK_FALSE,
        .compareOp = toVkCompareOp(desc.compareOp),
        .minLod = 0.0F,
        .maxLod = VK_LOD_CLAMP_NONE,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };

    VkSampler sampler = VK_NULL_HANDLE;
    VkResult res = vkCreateSampler(device_, &samplerInfo, nullptr, &sampler);
    if (res != VK_SUCCESS) {
        return Error{.code = ErrorCode::ResourceCreationFailed,
                     .message = "vkCreateSampler failed"};
    }

    uint32_t slotIdx = UINT32_MAX;
    for (size_t i = 0; i < samplers_.size(); ++i) {
        if (!samplers_[i].alive) {
            slotIdx = static_cast<uint32_t>(i);
            break;
        }
    }

    if (slotIdx == UINT32_MAX) {
        slotIdx = static_cast<uint32_t>(samplers_.size());
        samplers_.push_back(VulkanSamplerSlot{});
    }

    auto& slot = samplers_[slotIdx];
    slot.sampler = sampler;
    slot.desc = desc;
    slot.alive = true;

    return SamplerHandle(GenerationalHandle{slotIdx, slot.generation});
}

void VulkanDevice::destroySampler(SamplerHandle handle) {
    if (!isSamplerValid(handle)) {
        return;
    }
    auto& slot = samplers_[handle.index()];
    if (slot.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device_, slot.sampler, nullptr);
        slot.sampler = VK_NULL_HANDLE;
    }
    slot.alive = false;
    ++slot.generation;
}

Result<std::unique_ptr<RhiCommandList>> VulkanDevice::createCommandList() {
    VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = commandPool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
    VkResult res = vkAllocateCommandBuffers(device_, &allocInfo, &cmdBuffer);
    if (res != VK_SUCCESS) {
        return Error{.code = ErrorCode::CommandSubmissionFailed,
                     .message = "vkAllocateCommandBuffers failed"};
    }

    return std::unique_ptr<RhiCommandList>(new VulkanCommandList(*this, commandPool_, cmdBuffer));
}

Result<void> VulkanDevice::submit(RhiCommandList* commandList) {
    if (commandList == nullptr) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Cannot submit null command list"};
    }

    auto* vkCmd = static_cast<VulkanCommandList*>(commandList);
    VkCommandBuffer cmd = vkCmd->handle();

    VkFenceCreateInfo fenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };

    VkFence fence = VK_NULL_HANDLE;
    VkResult res = vkCreateFence(device_, &fenceInfo, nullptr, &fence);
    if (res != VK_SUCCESS) {
        return Error{.code = ErrorCode::CommandSubmissionFailed, .message = "vkCreateFence failed"};
    }

    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };

    res = vkQueueSubmit(graphicsQueue_, 1, &submitInfo, fence);
    if (res != VK_SUCCESS) {
        vkDestroyFence(device_, fence, nullptr);
        return Error{.code = ErrorCode::CommandSubmissionFailed, .message = "vkQueueSubmit failed"};
    }

    res = vkWaitForFences(device_, 1, &fence, VK_TRUE, 2000000000ULL); // 2 second timeout
    vkDestroyFence(device_, fence, nullptr);

    vkCmd->cleanupStagingResources();

    if (res != VK_SUCCESS) {
        ENG_LOG_FORMAT(LogLevel::Error, "Vulkan", "vkWaitForFences failed with VkResult: %d",
                       static_cast<int>(res));
        return Error{.code = ErrorCode::CommandSubmissionFailed,
                     .message = "vkWaitForFences failed"};
    }

    return Result<void>::success();
}

void VulkanDevice::waitIdle() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
}

VkBuffer VulkanDevice::resolveBuffer(BufferHandle handle) const noexcept {
    if (!isBufferValid(handle)) {
        return VK_NULL_HANDLE;
    }
    return buffers_[handle.index()].buffer;
}

bool VulkanDevice::isBufferValid(BufferHandle handle) const noexcept {
    if (!handle.isValid() || handle.index() >= buffers_.size()) {
        return false;
    }
    const auto& slot = buffers_[handle.index()];
    return slot.alive && slot.generation == handle.generation();
}

bool VulkanDevice::isTextureValid(TextureHandle handle) const noexcept {
    if (!handle.isValid() || handle.index() >= textures_.size()) {
        return false;
    }
    const auto& slot = textures_[handle.index()];
    return slot.alive && slot.generation == handle.generation();
}

bool VulkanDevice::isSamplerValid(SamplerHandle handle) const noexcept {
    if (!handle.isValid() || handle.index() >= samplers_.size()) {
        return false;
    }
    const auto& slot = samplers_[handle.index()];
    return slot.alive && slot.generation == handle.generation();
}

Result<uint32_t> VulkanDevice::acquireNextImage() {
    if (!swapchain_) {
        return 0;
    }
    auto acqRes = swapchain_->acquireNextImage();
    if (acqRes.hasValue()) {
        currentSwapchainImageIndex_ = acqRes.value();
    }
    return acqRes;
}

Result<void> VulkanDevice::present() {
    if (!swapchain_) {
        return Result<void>::success();
    }
    return swapchain_->present(presentQueue_, currentSwapchainImageIndex_, VK_NULL_HANDLE);
}

} // namespace eng::rhi::vk
