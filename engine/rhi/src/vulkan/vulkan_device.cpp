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

    for (auto& slot : pipelines_) {
        if (slot.alive) {
            if (slot.pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device_, slot.pipeline, nullptr);
            }
            slot.alive = false;
        }
    }
    pipelines_.clear();

    for (auto& slot : pipelineLayouts_) {
        if (slot.alive) {
            if (slot.layout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device_, slot.layout, nullptr);
            }
            for (auto dsl : slot.descriptorSetLayouts) {
                if (dsl != VK_NULL_HANDLE) {
                    vkDestroyDescriptorSetLayout(device_, dsl, nullptr);
                }
            }
            slot.descriptorSetLayouts.clear();
            slot.alive = false;
        }
    }
    pipelineLayouts_.clear();

    for (auto& slot : shaders_) {
        if (slot.alive) {
            if (slot.module != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device_, slot.module, nullptr);
            }
            slot.alive = false;
        }
    }
    shaders_.clear();

    for (auto& slot : descriptorPools_) {
        if (slot.alive) {
            if (slot.pool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device_, slot.pool, nullptr);
            }
            slot.alive = false;
        }
    }
    descriptorPools_.clear();
    descriptorSets_.clear();

    for (auto& slot : descriptorSetLayouts_) {
        if (slot.alive) {
            if (slot.layout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device_, slot.layout, nullptr);
            }
            slot.alive = false;
        }
    }
    descriptorSetLayouts_.clear();

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
    if (physicalDevice->getCaps().features.samplerAnisotropy) {
        deviceFeatures.samplerAnisotropy = VK_TRUE;
    }

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

Result<void> VulkanDevice::writeBuffer(BufferHandle handle, size_t offset, const void* data,
                                       size_t size) {
    if (data == nullptr && size > 0) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Null data pointer in writeBuffer"};
    }
    if (!isBufferValid(handle)) {
        return Error{.code = ErrorCode::HandleResolutionFailed,
                     .message = "Invalid buffer handle in writeBuffer"};
    }
    const auto& slot = buffers_[handle.index()];
    if (offset + size > slot.sizeInBytes) {
        return Error{.code = ErrorCode::OutOfBounds,
                     .message = "Buffer write exceeds allocated storage bounds"};
    }
    if (slot.mappedData == nullptr) {
        return Error{.code = ErrorCode::NotSupported,
                     .message = "Buffer is not host-visible or mapped"};
    }
    if (size > 0) {
        std::memcpy(static_cast<uint8_t*>(slot.mappedData) + offset, data, size);
    }
    return Result<void>::success();
}

Result<void> VulkanDevice::readBuffer(BufferHandle handle, size_t offset, void* outData,
                                      size_t size) {
    if (outData == nullptr && size > 0) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Null destination pointer in readBuffer"};
    }
    if (!isBufferValid(handle)) {
        return Error{.code = ErrorCode::HandleResolutionFailed,
                     .message = "Invalid buffer handle in readBuffer"};
    }
    const auto& slot = buffers_[handle.index()];
    if (offset + size > slot.sizeInBytes) {
        return Error{.code = ErrorCode::OutOfBounds,
                     .message = "Buffer read exceeds allocated storage bounds"};
    }
    if (slot.mappedData == nullptr) {
        return Error{.code = ErrorCode::NotSupported,
                     .message = "Buffer is not host-visible or mapped"};
    }
    if (size > 0) {
        std::memcpy(outData, static_cast<const uint8_t*>(slot.mappedData) + offset, size);
    }
    return Result<void>::success();
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
    slot.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
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
    const bool enableAniso =
        desc.anisotropyEnable && physicalDevice_->getCaps().features.samplerAnisotropy;
    const float maxAniso =
        enableAniso
            ? std::min(desc.maxAnisotropy, physicalDevice_->getCaps().limits.maxSamplerAnisotropy)
            : 1.0F;

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
        .mipLodBias = desc.mipLodBias,
        .anisotropyEnable = enableAniso ? VK_TRUE : VK_FALSE,
        .maxAnisotropy = maxAniso,
        .compareEnable = desc.compareEnable ? VK_TRUE : VK_FALSE,
        .compareOp = toVkCompareOp(desc.compareOp),
        .minLod = desc.minLod,
        .maxLod = desc.maxLod,
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

Result<ShaderHandle> VulkanDevice::createShader(const ShaderDesc& desc) {
    auto valRes = validateShaderDesc(desc);
    if (!valRes.hasValue()) {
        return valRes.error();
    }

    VkShaderModuleCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .codeSize = desc.spirvCode.size_bytes(),
        .pCode = desc.spirvCode.data(),
    };

    VkShaderModule module = VK_NULL_HANDLE;
    VkResult res = vkCreateShaderModule(device_, &createInfo, nullptr, &module);
    if (res != VK_SUCCESS) {
        return Error{.code = ErrorCode::ResourceCreationFailed,
                     .message = "vkCreateShaderModule failed"};
    }

    uint32_t slotIdx = UINT32_MAX;
    for (size_t i = 0; i < shaders_.size(); ++i) {
        if (!shaders_[i].alive) {
            slotIdx = static_cast<uint32_t>(i);
            break;
        }
    }

    if (slotIdx == UINT32_MAX) {
        slotIdx = static_cast<uint32_t>(shaders_.size());
        shaders_.push_back(VulkanShaderSlot{});
    }

    auto& slot = shaders_[slotIdx];
    slot.module = module;
    slot.desc = desc;
    slot.entryPoint = std::string(desc.entryPoint.empty() ? "main" : desc.entryPoint);
    slot.desc.entryPoint = slot.entryPoint;
    slot.code.assign(desc.spirvCode.begin(), desc.spirvCode.end());
    slot.desc.spirvCode = slot.code;
    slot.alive = true;

    return ShaderHandle(GenerationalHandle{slotIdx, slot.generation});
}

void VulkanDevice::destroyShader(ShaderHandle handle) {
    if (!isShaderValid(handle)) {
        return;
    }
    auto& slot = shaders_[handle.index()];
    if (slot.module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, slot.module, nullptr);
        slot.module = VK_NULL_HANDLE;
    }
    slot.alive = false;
    slot.code.clear();
    slot.entryPoint.clear();
    ++slot.generation;
}

Result<PipelineLayoutHandle> VulkanDevice::createPipelineLayout(const PipelineLayoutDesc& desc) {
    auto valRes = validatePipelineLayoutDesc(desc);
    if (!valRes.hasValue()) {
        return valRes.error();
    }

    std::vector<VkDescriptorSetLayout> allVkLayouts;
    allVkLayouts.reserve(desc.descriptorSetLayoutHandles.size() + desc.descriptorSetLayouts.size());

    for (const auto& layoutHandle : desc.descriptorSetLayoutHandles) {
        if (!isDescriptorSetLayoutValid(layoutHandle)) {
            return Error{.code = ErrorCode::HandleResolutionFailed,
                         .message =
                             "Invalid descriptor set layout handle in pipeline layout creation"};
        }
        allVkLayouts.push_back(resolveDescriptorSetLayout(layoutHandle));
    }

    std::vector<VkDescriptorSetLayout> createdDsls;
    createdDsls.reserve(desc.descriptorSetLayouts.size());

    for (const auto& setDesc : desc.descriptorSetLayouts) {
        std::vector<VkDescriptorSetLayoutBinding> vkBindings;
        vkBindings.reserve(setDesc.bindings.size());

        for (const auto& b : setDesc.bindings) {
            if (b.descriptorCount == 0) {
                for (auto dsl : createdDsls) {
                    vkDestroyDescriptorSetLayout(device_, dsl, nullptr);
                }
                return Error{.code = ErrorCode::InvalidArgument,
                             .message = "Descriptor binding count cannot be 0"};
            }
            vkBindings.push_back(VkDescriptorSetLayoutBinding{
                .binding = b.binding,
                .descriptorType = toVkDescriptorType(b.descriptorType),
                .descriptorCount = b.descriptorCount,
                .stageFlags = toVkShaderStageFlags(b.stageFlags),
                .pImmutableSamplers = nullptr,
            });
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .bindingCount = static_cast<uint32_t>(vkBindings.size()),
            .pBindings = vkBindings.empty() ? nullptr : vkBindings.data(),
        };

        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        VkResult res = vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &dsl);
        if (res != VK_SUCCESS) {
            for (auto prevDsl : createdDsls) {
                vkDestroyDescriptorSetLayout(device_, prevDsl, nullptr);
            }
            return Error{.code = ErrorCode::ResourceCreationFailed,
                         .message = "vkCreateDescriptorSetLayout failed"};
        }
        createdDsls.push_back(dsl);
        allVkLayouts.push_back(dsl);
    }

    std::vector<VkPushConstantRange> vkPushConstants;
    vkPushConstants.reserve(desc.pushConstants.size());
    for (const auto& pc : desc.pushConstants) {
        vkPushConstants.push_back(VkPushConstantRange{
            .stageFlags = toVkShaderStageFlags(pc.stageFlags),
            .offset = pc.offset,
            .size = pc.size,
        });
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = static_cast<uint32_t>(allVkLayouts.size()),
        .pSetLayouts = allVkLayouts.empty() ? nullptr : allVkLayouts.data(),
        .pushConstantRangeCount = static_cast<uint32_t>(vkPushConstants.size()),
        .pPushConstantRanges = vkPushConstants.empty() ? nullptr : vkPushConstants.data(),
    };

    VkPipelineLayout vkPipelineLayout = VK_NULL_HANDLE;
    VkResult res = vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &vkPipelineLayout);
    if (res != VK_SUCCESS) {
        for (auto dsl : createdDsls) {
            vkDestroyDescriptorSetLayout(device_, dsl, nullptr);
        }
        return Error{.code = ErrorCode::ResourceCreationFailed,
                     .message = "vkCreatePipelineLayout failed"};
    }

    uint32_t slotIdx = UINT32_MAX;
    for (size_t i = 0; i < pipelineLayouts_.size(); ++i) {
        if (!pipelineLayouts_[i].alive) {
            slotIdx = static_cast<uint32_t>(i);
            break;
        }
    }

    if (slotIdx == UINT32_MAX) {
        slotIdx = static_cast<uint32_t>(pipelineLayouts_.size());
        pipelineLayouts_.push_back(VulkanPipelineLayoutSlot{});
    }

    auto& slot = pipelineLayouts_[slotIdx];
    slot.layout = vkPipelineLayout;
    slot.descriptorSetLayouts = std::move(createdDsls);
    slot.desc = desc;
    slot.alive = true;

    return PipelineLayoutHandle(GenerationalHandle{slotIdx, slot.generation});
}

void VulkanDevice::destroyPipelineLayout(PipelineLayoutHandle handle) {
    if (!isPipelineLayoutValid(handle)) {
        return;
    }
    auto& slot = pipelineLayouts_[handle.index()];
    if (slot.layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, slot.layout, nullptr);
        slot.layout = VK_NULL_HANDLE;
    }
    for (auto dsl : slot.descriptorSetLayouts) {
        if (dsl != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, dsl, nullptr);
        }
    }
    slot.descriptorSetLayouts.clear();
    slot.alive = false;
    ++slot.generation;
}

Result<PipelineHandle> VulkanDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    auto valRes = validateGraphicsPipelineDesc(desc);
    if (!valRes.hasValue()) {
        return valRes.error();
    }

    if (!isShaderValid(desc.vertexShader)) {
        return Error{.code = ErrorCode::HandleResolutionFailed,
                     .message = "Invalid vertex shader handle in graphics pipeline creation"};
    }
    const auto* vsDesc = getShaderDesc(desc.vertexShader);
    if (vsDesc == nullptr || !hasFlag(vsDesc->stage, ShaderStageFlags::Vertex)) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Shader bound to vertex stage does not have Vertex stage flag"};
    }
    VkShaderModule vsModule = resolveShader(desc.vertexShader);

    VkShaderModule fsModule = VK_NULL_HANDLE;
    const ShaderDesc* fsDesc = nullptr;
    if (desc.fragmentShader.isValid()) {
        if (!isShaderValid(desc.fragmentShader)) {
            return Error{.code = ErrorCode::HandleResolutionFailed,
                         .message = "Invalid fragment shader handle in graphics pipeline creation"};
        }
        fsDesc = getShaderDesc(desc.fragmentShader);
        if (fsDesc == nullptr || !hasFlag(fsDesc->stage, ShaderStageFlags::Fragment)) {
            return Error{.code = ErrorCode::InvalidArgument,
                         .message =
                             "Shader bound to fragment stage does not have Fragment stage flag"};
        }
        fsModule = resolveShader(desc.fragmentShader);
    }

    if (!isPipelineLayoutValid(desc.layout)) {
        return Error{.code = ErrorCode::HandleResolutionFailed,
                     .message = "Invalid pipeline layout handle in graphics pipeline creation"};
    }
    VkPipelineLayout vkLayout = resolvePipelineLayout(desc.layout);

    // 1. Shader Stages
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    shaderStages.push_back(VkPipelineShaderStageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vsModule,
        .pName = shaders_[desc.vertexShader.index()].entryPoint.c_str(),
        .pSpecializationInfo = nullptr,
    });
    if (fsModule != VK_NULL_HANDLE && fsDesc != nullptr) {
        shaderStages.push_back(VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fsModule,
            .pName = shaders_[desc.fragmentShader.index()].entryPoint.c_str(),
            .pSpecializationInfo = nullptr,
        });
    }

    // 2. Vertex Input
    std::vector<VkVertexInputBindingDescription> vkBindings;
    vkBindings.reserve(desc.vertexInput.bindings.size());
    for (const auto& b : desc.vertexInput.bindings) {
        vkBindings.push_back(VkVertexInputBindingDescription{
            .binding = b.binding,
            .stride = b.stride,
            .inputRate = toVkVertexInputRate(b.inputRate),
        });
    }

    std::vector<VkVertexInputAttributeDescription> vkAttributes;
    vkAttributes.reserve(desc.vertexInput.attributes.size());
    for (const auto& a : desc.vertexInput.attributes) {
        vkAttributes.push_back(VkVertexInputAttributeDescription{
            .location = a.location,
            .binding = a.binding,
            .format = toVkVertexFormat(a.format),
            .offset = a.offset,
        });
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount = static_cast<uint32_t>(vkBindings.size()),
        .pVertexBindingDescriptions = vkBindings.empty() ? nullptr : vkBindings.data(),
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vkAttributes.size()),
        .pVertexAttributeDescriptions = vkAttributes.empty() ? nullptr : vkAttributes.data(),
    };

    // 3. Input Assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .topology = toVkPrimitiveTopology(desc.topology),
        .primitiveRestartEnable = VK_FALSE,
    };

    // 4. Viewport & Scissor (Dynamic)
    VkViewport dummyViewport{
        .x = 0.0F, .y = 0.0F, .width = 1.0F, .height = 1.0F, .minDepth = 0.0F, .maxDepth = 1.0F};
    VkRect2D dummyScissor{.offset = {0, 0}, .extent = {1, 1}};

    VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = &dummyViewport,
        .scissorCount = 1,
        .pScissors = &dummyScissor,
    };

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamicStates,
    };

    // 5. Rasterization
    VkPipelineRasterizationStateCreateInfo rasterizer{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = toVkPolygonMode(desc.rasterization.polygonMode),
        .cullMode = toVkCullModeFlags(desc.rasterization.cullMode),
        .frontFace = toVkFrontFace(desc.rasterization.frontFace),
        .depthBiasEnable = desc.rasterization.depthBiasEnable ? VK_TRUE : VK_FALSE,
        .depthBiasConstantFactor = desc.rasterization.depthBiasConstantFactor,
        .depthBiasClamp = 0.0F,
        .depthBiasSlopeFactor = desc.rasterization.depthBiasSlopeFactor,
        .lineWidth = desc.rasterization.lineWidth,
    };

    // 6. Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0F,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };

    // 7. Depth Stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthTestEnable = desc.depthStencil.depthTestEnable ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = desc.depthStencil.depthWriteEnable ? VK_TRUE : VK_FALSE,
        .depthCompareOp = toVkCompareOp(desc.depthStencil.depthCompareOp),
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .front = {},
        .back = {},
        .minDepthBounds = 0.0F,
        .maxDepthBounds = 1.0F,
    };

    // 8. Color Blending
    std::vector<VkPipelineColorBlendAttachmentState> vkBlendAttachments;
    vkBlendAttachments.reserve(desc.colorBlendAttachments.size());
    for (const auto& ba : desc.colorBlendAttachments) {
        vkBlendAttachments.push_back(VkPipelineColorBlendAttachmentState{
            .blendEnable = ba.blendEnable ? VK_TRUE : VK_FALSE,
            .srcColorBlendFactor = toVkBlendFactor(ba.srcColorBlendFactor),
            .dstColorBlendFactor = toVkBlendFactor(ba.dstColorBlendFactor),
            .colorBlendOp = toVkBlendOp(ba.colorBlendOp),
            .srcAlphaBlendFactor = toVkBlendFactor(ba.srcAlphaBlendFactor),
            .dstAlphaBlendFactor = toVkBlendFactor(ba.dstAlphaBlendFactor),
            .alphaBlendOp = toVkBlendOp(ba.alphaBlendOp),
            .colorWriteMask = toVkColorComponentFlags(ba.colorWriteMask),
        });
    }

    if (vkBlendAttachments.empty() && !desc.colorAttachmentFormats.empty()) {
        for (size_t i = 0; i < desc.colorAttachmentFormats.size(); ++i) {
            vkBlendAttachments.push_back(VkPipelineColorBlendAttachmentState{
                .blendEnable = VK_FALSE,
                .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
                .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
                .colorBlendOp = VK_BLEND_OP_ADD,
                .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                .alphaBlendOp = VK_BLEND_OP_ADD,
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
            });
        }
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = static_cast<uint32_t>(vkBlendAttachments.size()),
        .pAttachments = vkBlendAttachments.empty() ? nullptr : vkBlendAttachments.data(),
        .blendConstants = {0.0F, 0.0F, 0.0F, 0.0F},
    };

    // 9. Vulkan 1.3 Dynamic Rendering Attachments Info
    std::vector<VkFormat> colorFormats;
    colorFormats.reserve(desc.colorAttachmentFormats.size());
    for (auto f : desc.colorAttachmentFormats) {
        colorFormats.push_back(toVkFormat(f));
    }
    VkFormat depthFormat = toVkFormat(desc.depthAttachmentFormat);

    VkPipelineRenderingCreateInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext = nullptr,
        .viewMask = 0,
        .colorAttachmentCount = static_cast<uint32_t>(colorFormats.size()),
        .pColorAttachmentFormats = colorFormats.empty() ? nullptr : colorFormats.data(),
        .depthAttachmentFormat = depthFormat,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
    };

    // 10. Create Graphics Pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingInfo,
        .flags = 0,
        .stageCount = static_cast<uint32_t>(shaderStages.size()),
        .pStages = shaderStages.data(),
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pTessellationState = nullptr,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = vkLayout,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };

    VkPipeline vkPipeline = VK_NULL_HANDLE;
    VkResult res =
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vkPipeline);
    if (res != VK_SUCCESS) {
        return Error{.code = ErrorCode::ResourceCreationFailed,
                     .message = "vkCreateGraphicsPipelines failed"};
    }

    uint32_t slotIdx = UINT32_MAX;
    for (size_t i = 0; i < pipelines_.size(); ++i) {
        if (!pipelines_[i].alive) {
            slotIdx = static_cast<uint32_t>(i);
            break;
        }
    }

    if (slotIdx == UINT32_MAX) {
        slotIdx = static_cast<uint32_t>(pipelines_.size());
        pipelines_.push_back(VulkanPipelineSlot{});
    }

    auto& slot = pipelines_[slotIdx];
    slot.pipeline = vkPipeline;
    slot.layoutHandle = desc.layout;
    slot.desc = desc;
    slot.alive = true;

    return PipelineHandle(GenerationalHandle{slotIdx, slot.generation});
}

void VulkanDevice::destroyGraphicsPipeline(PipelineHandle handle) {
    if (!isPipelineValid(handle)) {
        return;
    }
    auto& slot = pipelines_[handle.index()];
    if (slot.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, slot.pipeline, nullptr);
        slot.pipeline = VK_NULL_HANDLE;
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

VkImage VulkanDevice::resolveTextureImage(TextureHandle handle) const noexcept {
    if (isTextureValid(handle)) {
        return textures_[handle.index()].image;
    }
    return VK_NULL_HANDLE;
}

VkImageView VulkanDevice::resolveTextureView(TextureHandle handle) const noexcept {
    if (isTextureValid(handle)) {
        return textures_[handle.index()].view;
    }
    return VK_NULL_HANDLE;
}

VkImageLayout VulkanDevice::getTextureLayout(TextureHandle handle) const noexcept {
    if (isTextureValid(handle)) {
        return textures_[handle.index()].currentLayout;
    }
    return VK_IMAGE_LAYOUT_UNDEFINED;
}

void VulkanDevice::setTextureLayout(TextureHandle handle, VkImageLayout layout) noexcept {
    if (isTextureValid(handle)) {
        textures_[handle.index()].currentLayout = layout;
    }
}

const TextureDesc* VulkanDevice::getTextureDesc(TextureHandle handle) const noexcept {
    if (isTextureValid(handle)) {
        return &textures_[handle.index()].desc;
    }
    return nullptr;
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

bool VulkanDevice::isShaderValid(ShaderHandle handle) const noexcept {
    if (!handle.isValid() || handle.index() >= shaders_.size()) {
        return false;
    }
    const auto& slot = shaders_[handle.index()];
    return slot.alive && slot.generation == handle.generation();
}

bool VulkanDevice::isPipelineLayoutValid(PipelineLayoutHandle handle) const noexcept {
    if (!handle.isValid() || handle.index() >= pipelineLayouts_.size()) {
        return false;
    }
    const auto& slot = pipelineLayouts_[handle.index()];
    return slot.alive && slot.generation == handle.generation();
}

bool VulkanDevice::isPipelineValid(PipelineHandle handle) const noexcept {
    if (!handle.isValid() || handle.index() >= pipelines_.size()) {
        return false;
    }
    const auto& slot = pipelines_[handle.index()];
    return slot.alive && slot.generation == handle.generation();
}

VkShaderModule VulkanDevice::resolveShader(ShaderHandle handle) const noexcept {
    if (isShaderValid(handle)) {
        return shaders_[handle.index()].module;
    }
    return VK_NULL_HANDLE;
}

VkPipelineLayout VulkanDevice::resolvePipelineLayout(PipelineLayoutHandle handle) const noexcept {
    if (isPipelineLayoutValid(handle)) {
        return pipelineLayouts_[handle.index()].layout;
    }
    return VK_NULL_HANDLE;
}

VkPipeline VulkanDevice::resolvePipeline(PipelineHandle handle) const noexcept {
    if (isPipelineValid(handle)) {
        return pipelines_[handle.index()].pipeline;
    }
    return VK_NULL_HANDLE;
}

const ShaderDesc* VulkanDevice::getShaderDesc(ShaderHandle handle) const noexcept {
    if (isShaderValid(handle)) {
        auto& slot = const_cast<VulkanShaderSlot&>(shaders_[handle.index()]);
        slot.desc.entryPoint = slot.entryPoint;
        slot.desc.spirvCode = slot.code;
        return &slot.desc;
    }
    return nullptr;
}

const PipelineLayoutDesc*
VulkanDevice::getPipelineLayoutDesc(PipelineLayoutHandle handle) const noexcept {
    if (isPipelineLayoutValid(handle)) {
        return &pipelineLayouts_[handle.index()].desc;
    }
    return nullptr;
}

const GraphicsPipelineDesc* VulkanDevice::getPipelineDesc(PipelineHandle handle) const noexcept {
    if (isPipelineValid(handle)) {
        return &pipelines_[handle.index()].desc;
    }
    return nullptr;
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

Result<void> VulkanDevice::resize(uint32_t width, uint32_t height) {
    if (!swapchain_) {
        return Result<void>::success();
    }
    return swapchain_->recreate(width, height);
}

NativeDeviceHandles VulkanDevice::getNativeDeviceHandles() const noexcept {
    return NativeDeviceHandles{
        .instance = instance_ ? instance_->handle() : VK_NULL_HANDLE,
        .physicalDevice = physicalDevice_ ? physicalDevice_->handle() : VK_NULL_HANDLE,
        .device = device_,
        .graphicsQueue = graphicsQueue_,
        .graphicsQueueFamily = graphicsQueueFamily_,
        .colorAttachmentFormat =
            swapchain_ ? static_cast<uint32_t>(swapchain_->getImageFormat()) : 0,
        .swapchainImageCount = swapchain_ ? static_cast<uint32_t>(swapchain_->getImageCount()) : 0,
    };
}

void* VulkanDevice::getNativeTextureView(TextureHandle handle) const noexcept {
    return static_cast<void*>(resolveTextureView(handle));
}

void* VulkanDevice::getNativeSampler(SamplerHandle handle) const noexcept {
    return static_cast<void*>(resolveSampler(handle));
}

Result<DescriptorSetLayoutHandle>
VulkanDevice::createDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) {
    auto valRes = validateDescriptorSetLayoutDesc(desc);
    if (!valRes.hasValue()) {
        return valRes.error();
    }

    std::vector<VkDescriptorSetLayoutBinding> vkBindings;
    vkBindings.reserve(desc.bindings.size());

    for (const auto& b : desc.bindings) {
        vkBindings.push_back(VkDescriptorSetLayoutBinding{
            .binding = b.binding,
            .descriptorType = toVkDescriptorType(b.descriptorType),
            .descriptorCount = b.descriptorCount,
            .stageFlags = toVkShaderStageFlags(b.stageFlags),
            .pImmutableSamplers = nullptr,
        });
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .bindingCount = static_cast<uint32_t>(vkBindings.size()),
        .pBindings = vkBindings.empty() ? nullptr : vkBindings.data(),
    };

    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkResult res = vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &dsl);
    if (res != VK_SUCCESS) {
        return Error{.code = ErrorCode::ResourceCreationFailed,
                     .message = "vkCreateDescriptorSetLayout failed"};
    }

    uint32_t slotIdx = UINT32_MAX;
    for (size_t i = 0; i < descriptorSetLayouts_.size(); ++i) {
        if (!descriptorSetLayouts_[i].alive) {
            slotIdx = static_cast<uint32_t>(i);
            break;
        }
    }

    if (slotIdx == UINT32_MAX) {
        slotIdx = static_cast<uint32_t>(descriptorSetLayouts_.size());
        descriptorSetLayouts_.push_back(VulkanDescriptorSetLayoutSlot{});
    }

    auto& slot = descriptorSetLayouts_[slotIdx];
    slot.layout = dsl;
    slot.desc = desc;
    slot.alive = true;

    return DescriptorSetLayoutHandle(GenerationalHandle{slotIdx, slot.generation});
}

void VulkanDevice::destroyDescriptorSetLayout(DescriptorSetLayoutHandle handle) {
    if (!isDescriptorSetLayoutValid(handle)) {
        return;
    }
    auto& slot = descriptorSetLayouts_[handle.index()];
    if (slot.layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, slot.layout, nullptr);
        slot.layout = VK_NULL_HANDLE;
    }
    slot.alive = false;
    ++slot.generation;
}

Result<DescriptorPoolHandle> VulkanDevice::createDescriptorPool(const DescriptorPoolDesc& desc) {
    auto valRes = validateDescriptorPoolDesc(desc);
    if (!valRes.hasValue()) {
        return valRes.error();
    }

    std::vector<VkDescriptorPoolSize> poolSizes;
    poolSizes.reserve(desc.poolSizes.size());
    for (const auto& ps : desc.poolSizes) {
        poolSizes.push_back(VkDescriptorPoolSize{
            .type = toVkDescriptorType(ps.type),
            .descriptorCount = ps.count,
        });
    }

    VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .maxSets = desc.maxSets,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };

    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkResult res = vkCreateDescriptorPool(device_, &poolInfo, nullptr, &pool);
    if (res != VK_SUCCESS) {
        return Error{.code = ErrorCode::ResourceCreationFailed,
                     .message = "vkCreateDescriptorPool failed"};
    }

    uint32_t slotIdx = UINT32_MAX;
    for (size_t i = 0; i < descriptorPools_.size(); ++i) {
        if (!descriptorPools_[i].alive) {
            slotIdx = static_cast<uint32_t>(i);
            break;
        }
    }

    if (slotIdx == UINT32_MAX) {
        slotIdx = static_cast<uint32_t>(descriptorPools_.size());
        descriptorPools_.push_back(VulkanDescriptorPoolSlot{});
    }

    auto& slot = descriptorPools_[slotIdx];
    slot.pool = pool;
    slot.desc = desc;
    slot.allocatedCount = 0;
    slot.alive = true;

    return DescriptorPoolHandle(GenerationalHandle{slotIdx, slot.generation});
}

void VulkanDevice::destroyDescriptorPool(DescriptorPoolHandle handle) {
    if (!isDescriptorPoolValid(handle)) {
        return;
    }
    auto& poolSlot = descriptorPools_[handle.index()];
    if (poolSlot.pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, poolSlot.pool, nullptr);
        poolSlot.pool = VK_NULL_HANDLE;
    }
    poolSlot.alive = false;
    ++poolSlot.generation;

    for (auto& setSlot : descriptorSets_) {
        if (setSlot.alive && setSlot.poolHandle == handle) {
            setSlot.alive = false;
            setSlot.set = VK_NULL_HANDLE;
            ++setSlot.generation;
        }
    }
}

Result<void> VulkanDevice::resetDescriptorPool(DescriptorPoolHandle handle) {
    if (!isDescriptorPoolValid(handle)) {
        return Error{.code = ErrorCode::HandleResolutionFailed,
                     .message = "Invalid descriptor pool handle in resetDescriptorPool"};
    }

    auto& poolSlot = descriptorPools_[handle.index()];
    VkResult res = vkResetDescriptorPool(device_, poolSlot.pool, 0);
    if (res != VK_SUCCESS) {
        return Error{.code = ErrorCode::ResourceCreationFailed,
                     .message = "vkResetDescriptorPool failed"};
    }
    poolSlot.allocatedCount = 0;

    for (auto& setSlot : descriptorSets_) {
        if (setSlot.alive && setSlot.poolHandle == handle) {
            setSlot.alive = false;
            setSlot.set = VK_NULL_HANDLE;
            ++setSlot.generation;
        }
    }

    return Result<void>::success();
}

Result<DescriptorSetHandle> VulkanDevice::allocateDescriptorSet(DescriptorPoolHandle pool,
                                                                DescriptorSetLayoutHandle layout) {
    if (!isDescriptorPoolValid(pool)) {
        return Error{.code = ErrorCode::HandleResolutionFailed,
                     .message = "Invalid descriptor pool handle in allocateDescriptorSet"};
    }
    if (!isDescriptorSetLayoutValid(layout)) {
        return Error{.code = ErrorCode::HandleResolutionFailed,
                     .message = "Invalid descriptor set layout handle in allocateDescriptorSet"};
    }

    auto& poolSlot = descriptorPools_[pool.index()];
    if (poolSlot.allocatedCount >= poolSlot.desc.maxSets) {
        return Error{.code = ErrorCode::OutOfMemory,
                     .message = "Descriptor pool capacity exhausted"};
    }

    VkDescriptorSetLayout vkLayout = resolveDescriptorSetLayout(layout);
    VkDescriptorSetAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorPool = poolSlot.pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &vkLayout,
    };

    VkDescriptorSet vkSet = VK_NULL_HANDLE;
    VkResult res = vkAllocateDescriptorSets(device_, &allocInfo, &vkSet);
    if (res != VK_SUCCESS) {
        return Error{.code = ErrorCode::OutOfMemory,
                     .message = "vkAllocateDescriptorSets failed (out of pool memory)"};
    }

    uint32_t slotIdx = UINT32_MAX;
    for (size_t i = 0; i < descriptorSets_.size(); ++i) {
        if (!descriptorSets_[i].alive) {
            slotIdx = static_cast<uint32_t>(i);
            break;
        }
    }

    if (slotIdx == UINT32_MAX) {
        slotIdx = static_cast<uint32_t>(descriptorSets_.size());
        descriptorSets_.push_back(VulkanDescriptorSetSlot{});
    }

    auto& slot = descriptorSets_[slotIdx];
    slot.set = vkSet;
    slot.poolHandle = pool;
    slot.layoutHandle = layout;
    slot.alive = true;

    ++poolSlot.allocatedCount;
    return DescriptorSetHandle(GenerationalHandle{slotIdx, slot.generation});
}

Result<void> VulkanDevice::updateDescriptorSet(DescriptorSetHandle set,
                                               std::span<const DescriptorWrite> writes) {
    if (!isDescriptorSetValid(set)) {
        return Error{.code = ErrorCode::HandleResolutionFailed,
                     .message = "Invalid descriptor set handle in updateDescriptorSet"};
    }

    const auto& setSlot = descriptorSets_[set.index()];
    const auto* layoutDesc = getDescriptorSetLayoutDesc(setSlot.layoutHandle);
    if (layoutDesc == nullptr) {
        return Error{.code = ErrorCode::HandleResolutionFailed,
                     .message = "Descriptor set has invalid layout reference"};
    }

    constexpr size_t kMaxStackWrites = 8;
    constexpr size_t kMaxStackInfos = 16;

    std::array<VkWriteDescriptorSet, kMaxStackWrites> stackWrites{};
    std::array<VkDescriptorBufferInfo, kMaxStackInfos> stackBufferInfos{};
    std::array<VkDescriptorImageInfo, kMaxStackInfos> stackImageInfos{};

    std::vector<VkWriteDescriptorSet> heapWrites;
    std::vector<VkDescriptorBufferInfo> heapBufferInfos;
    std::vector<VkDescriptorImageInfo> heapImageInfos;

    size_t totalBufferInfos = 0;
    size_t totalImageInfos = 0;
    for (const auto& w : writes) {
        totalBufferInfos += w.bufferInfos.size();
        totalImageInfos += w.imageInfos.size();
    }

    const bool useHeap = (writes.size() > kMaxStackWrites) || (totalBufferInfos > kMaxStackInfos) ||
                         (totalImageInfos > kMaxStackInfos);

    if (useHeap) {
        heapWrites.resize(writes.size());
        heapBufferInfos.resize(totalBufferInfos);
        heapImageInfos.resize(totalImageInfos);
    }

    VkWriteDescriptorSet* pWrites = useHeap ? heapWrites.data() : stackWrites.data();
    VkDescriptorBufferInfo* pBufferInfos =
        useHeap ? heapBufferInfos.data() : stackBufferInfos.data();
    VkDescriptorImageInfo* pImageInfos = useHeap ? heapImageInfos.data() : stackImageInfos.data();

    size_t currentBufferInfoIdx = 0;
    size_t currentImageInfoIdx = 0;

    for (size_t wIdx = 0; wIdx < writes.size(); ++wIdx) {
        const auto& write = writes[wIdx];
        const DescriptorBindingDesc* matchedBinding = nullptr;
        for (const auto& b : layoutDesc->bindings) {
            if (b.binding == write.dstBinding) {
                matchedBinding = &b;
                break;
            }
        }
        if (matchedBinding == nullptr) {
            return Error{.code = ErrorCode::InvalidArgument,
                         .message = "Descriptor write binding index not declared in layout"};
        }
        if (matchedBinding->descriptorType != write.descriptorType) {
            return Error{.code = ErrorCode::InvalidArgument,
                         .message = "Descriptor write type does not match layout binding type"};
        }

        VkWriteDescriptorSet& vkWrite = pWrites[wIdx];
        vkWrite = VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = setSlot.set,
            .dstBinding = write.dstBinding,
            .dstArrayElement = write.dstArrayElement,
            .descriptorCount = 0,
            .descriptorType = toVkDescriptorType(write.descriptorType),
            .pImageInfo = nullptr,
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr,
        };

        if (write.descriptorType == DescriptorType::UniformBuffer ||
            write.descriptorType == DescriptorType::StorageBuffer) {
            if (write.bufferInfos.empty()) {
                return Error{.code = ErrorCode::InvalidArgument,
                             .message = "Descriptor buffer write has no buffer infos"};
            }
            vkWrite.descriptorCount = static_cast<uint32_t>(write.bufferInfos.size());
            vkWrite.pBufferInfo = &pBufferInfos[currentBufferInfoIdx];

            for (const auto& bufInfo : write.bufferInfos) {
                if (!bufInfo.buffer.isValid() || !isBufferValid(bufInfo.buffer)) {
                    return Error{.code = ErrorCode::HandleResolutionFailed,
                                 .message = "Invalid buffer handle in descriptor buffer write"};
                }
                const auto& bSlot = buffers_[bufInfo.buffer.index()];
                if (bufInfo.offset > bSlot.sizeInBytes) {
                    return Error{.code = ErrorCode::OutOfBounds,
                                 .message = "Descriptor buffer offset exceeds buffer size"};
                }
                if (bufInfo.range > 0 && (bufInfo.offset + bufInfo.range > bSlot.sizeInBytes)) {
                    return Error{.code = ErrorCode::OutOfBounds,
                                 .message = "Descriptor buffer range exceeds buffer size"};
                }

                pBufferInfos[currentBufferInfoIdx++] = VkDescriptorBufferInfo{
                    .buffer = bSlot.buffer,
                    .offset = bufInfo.offset,
                    .range = (bufInfo.range == kWholeBufferSize) ? VK_WHOLE_SIZE : bufInfo.range,
                };
            }
        } else if (write.descriptorType == DescriptorType::CombinedImageSampler) {
            if (write.imageInfos.empty()) {
                return Error{.code = ErrorCode::InvalidArgument,
                             .message = "Descriptor image write has no image infos"};
            }
            vkWrite.descriptorCount = static_cast<uint32_t>(write.imageInfos.size());
            vkWrite.pImageInfo = &pImageInfos[currentImageInfoIdx];

            for (const auto& imgInfo : write.imageInfos) {
                if (!imgInfo.texture.isValid() || !isTextureValid(imgInfo.texture)) {
                    return Error{.code = ErrorCode::HandleResolutionFailed,
                                 .message = "Invalid texture handle in CombinedImageSampler write"};
                }
                if (!imgInfo.sampler.isValid() || !isSamplerValid(imgInfo.sampler)) {
                    return Error{.code = ErrorCode::HandleResolutionFailed,
                                 .message = "Invalid sampler handle in CombinedImageSampler write"};
                }
                pImageInfos[currentImageInfoIdx++] = VkDescriptorImageInfo{
                    .sampler = resolveSampler(imgInfo.sampler),
                    .imageView = resolveTextureView(imgInfo.texture),
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                };
            }
        } else if (write.descriptorType == DescriptorType::SampledImage) {
            if (write.imageInfos.empty()) {
                return Error{.code = ErrorCode::InvalidArgument,
                             .message = "Descriptor image write has no image infos"};
            }
            vkWrite.descriptorCount = static_cast<uint32_t>(write.imageInfos.size());
            vkWrite.pImageInfo = &pImageInfos[currentImageInfoIdx];

            for (const auto& imgInfo : write.imageInfos) {
                if (!imgInfo.texture.isValid() || !isTextureValid(imgInfo.texture)) {
                    return Error{.code = ErrorCode::HandleResolutionFailed,
                                 .message = "Invalid texture handle in SampledImage write"};
                }
                pImageInfos[currentImageInfoIdx++] = VkDescriptorImageInfo{
                    .sampler = VK_NULL_HANDLE,
                    .imageView = resolveTextureView(imgInfo.texture),
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                };
            }
        } else if (write.descriptorType == DescriptorType::Sampler) {
            if (write.imageInfos.empty()) {
                return Error{.code = ErrorCode::InvalidArgument,
                             .message = "Descriptor image write has no image infos"};
            }
            vkWrite.descriptorCount = static_cast<uint32_t>(write.imageInfos.size());
            vkWrite.pImageInfo = &pImageInfos[currentImageInfoIdx];

            for (const auto& imgInfo : write.imageInfos) {
                if (!imgInfo.sampler.isValid() || !isSamplerValid(imgInfo.sampler)) {
                    return Error{.code = ErrorCode::HandleResolutionFailed,
                                 .message = "Invalid sampler handle in Sampler write"};
                }
                pImageInfos[currentImageInfoIdx++] = VkDescriptorImageInfo{
                    .sampler = resolveSampler(imgInfo.sampler),
                    .imageView = VK_NULL_HANDLE,
                    .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                };
            }
        }
    }

    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), pWrites, 0, nullptr);
    return Result<void>::success();
}

VkDescriptorSetLayout
VulkanDevice::resolveDescriptorSetLayout(DescriptorSetLayoutHandle handle) const noexcept {
    if (isDescriptorSetLayoutValid(handle)) {
        return descriptorSetLayouts_[handle.index()].layout;
    }
    return VK_NULL_HANDLE;
}

VkDescriptorPool VulkanDevice::resolveDescriptorPool(DescriptorPoolHandle handle) const noexcept {
    if (isDescriptorPoolValid(handle)) {
        return descriptorPools_[handle.index()].pool;
    }
    return VK_NULL_HANDLE;
}

VkDescriptorSet VulkanDevice::resolveDescriptorSet(DescriptorSetHandle handle) const noexcept {
    if (isDescriptorSetValid(handle)) {
        return descriptorSets_[handle.index()].set;
    }
    return VK_NULL_HANDLE;
}

bool VulkanDevice::isDescriptorSetLayoutValid(DescriptorSetLayoutHandle handle) const noexcept {
    if (handle.index() < descriptorSetLayouts_.size()) {
        const auto& slot = descriptorSetLayouts_[handle.index()];
        return slot.alive && slot.generation == handle.generation();
    }
    return false;
}

bool VulkanDevice::isDescriptorPoolValid(DescriptorPoolHandle handle) const noexcept {
    if (handle.index() < descriptorPools_.size()) {
        const auto& slot = descriptorPools_[handle.index()];
        return slot.alive && slot.generation == handle.generation();
    }
    return false;
}

bool VulkanDevice::isDescriptorSetValid(DescriptorSetHandle handle) const noexcept {
    if (handle.index() < descriptorSets_.size()) {
        const auto& slot = descriptorSets_[handle.index()];
        return slot.alive && slot.generation == handle.generation();
    }
    return false;
}

const DescriptorSetLayoutDesc*
VulkanDevice::getDescriptorSetLayoutDesc(DescriptorSetLayoutHandle handle) const noexcept {
    if (isDescriptorSetLayoutValid(handle)) {
        return &descriptorSetLayouts_[handle.index()].desc;
    }
    return nullptr;
}

const DescriptorPoolDesc*
VulkanDevice::getDescriptorPoolDesc(DescriptorPoolHandle handle) const noexcept {
    if (isDescriptorPoolValid(handle)) {
        return &descriptorPools_[handle.index()].desc;
    }
    return nullptr;
}

VkSampler VulkanDevice::resolveSampler(SamplerHandle handle) const noexcept {
    if (isSamplerValid(handle)) {
        return samplers_[handle.index()].sampler;
    }
    return VK_NULL_HANDLE;
}

} // namespace eng::rhi::vk
