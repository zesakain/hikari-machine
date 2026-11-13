/// @file vulkan_memory.cpp
/// @brief Vulkan memory allocator implementation.

#include "vulkan_memory.h"

namespace eng::rhi::vk {

VulkanMemoryManager::VulkanMemoryManager(VkPhysicalDevice physicalDevice) noexcept {
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties_);
}

Result<uint32_t>
VulkanMemoryManager::findMemoryType(uint32_t typeFilter,
                                    VkMemoryPropertyFlags properties) const noexcept {
    for (uint32_t i = 0; i < memoryProperties_.memoryTypeCount; ++i) {
        if ((typeFilter & (1U << i)) != 0 &&
            (memoryProperties_.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return Error{.code = ErrorCode::OutOfDeviceMemory,
                 .message = "Failed to find suitable Vulkan memory type"};
}

Result<VkDeviceMemory> VulkanMemoryManager::allocateBufferMemory(VkDevice device, VkBuffer buffer,
                                                                 MemoryUsage usage,
                                                                 void** outMappedData) const {
    VkMemoryRequirements memReqs{};
    vkGetBufferMemoryRequirements(device, buffer, &memReqs);

    VkMemoryPropertyFlags propFlags = 0;
    bool triedDeviceLocalHost = false;
    if (usage == MemoryUsage::GpuOnly) {
        propFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    } else if (usage == MemoryUsage::CpuToGpu) {
        // Prioritize device-local host-visible memory (BAR / ReBAR) for high GPU throughput
        auto devLocalHostRes =
            findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (devLocalHostRes.hasValue()) {
            propFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            triedDeviceLocalHost = true;
        } else {
            propFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        }
    } else {
        // Readback / staging buffers (GpuToCpu) avoid exhausting limited BAR memory
        propFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }

    auto typeRes = findMemoryType(memReqs.memoryTypeBits, propFlags);
    if (!typeRes.hasValue()) {
        return typeRes.error();
    }

    VkMemoryAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = typeRes.value(),
    };

    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkResult res = vkAllocateMemory(device, &allocInfo, nullptr, &memory);
    if ((res != VK_SUCCESS || memory == VK_NULL_HANDLE) && triedDeviceLocalHost) {
        // Graceful fallback to system host memory if BAR/ReBAR heap is exhausted
        ENG_LOG_WARN("Vulkan",
                     "BAR device-local memory exhausted; falling back to host system memory");
        propFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        auto fallbackTypeRes = findMemoryType(memReqs.memoryTypeBits, propFlags);
        if (fallbackTypeRes.hasValue()) {
            allocInfo.memoryTypeIndex = fallbackTypeRes.value();
            res = vkAllocateMemory(device, &allocInfo, nullptr, &memory);
        }
    }

    if (res != VK_SUCCESS || memory == VK_NULL_HANDLE) {
        ENG_LOG_ERROR("Vulkan", "Failed to allocate buffer device memory");
        return Error{.code = ErrorCode::OutOfDeviceMemory,
                     .message = "vkAllocateMemory failed for buffer"};
    }

    res = vkBindBufferMemory(device, buffer, memory, 0);
    if (res != VK_SUCCESS) {
        vkFreeMemory(device, memory, nullptr);
        return Error{.code = ErrorCode::ResourceCreationFailed,
                     .message = "vkBindBufferMemory failed"};
    }

    if (outMappedData != nullptr && usage != MemoryUsage::GpuOnly) {
        res = vkMapMemory(device, memory, 0, memReqs.size, 0, outMappedData);
        if (res != VK_SUCCESS) {
            *outMappedData = nullptr;
        }
    }

    return memory;
}

Result<VkDeviceMemory> VulkanMemoryManager::allocateImageMemory(VkDevice device, VkImage image,
                                                                MemoryUsage usage) const {
    VkMemoryRequirements memReqs{};
    vkGetImageMemoryRequirements(device, image, &memReqs);

    VkMemoryPropertyFlags propFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (usage != MemoryUsage::GpuOnly) {
        propFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }

    auto typeRes = findMemoryType(memReqs.memoryTypeBits, propFlags);
    if (!typeRes.hasValue()) {
        return typeRes.error();
    }

    VkMemoryAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = typeRes.value(),
    };

    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkResult res = vkAllocateMemory(device, &allocInfo, nullptr, &memory);
    if (res != VK_SUCCESS || memory == VK_NULL_HANDLE) {
        ENG_LOG_ERROR("Vulkan", "Failed to allocate image device memory");
        return Error{.code = ErrorCode::OutOfDeviceMemory,
                     .message = "vkAllocateMemory failed for image"};
    }

    res = vkBindImageMemory(device, image, memory, 0);
    if (res != VK_SUCCESS) {
        vkFreeMemory(device, memory, nullptr);
        return Error{.code = ErrorCode::ResourceCreationFailed,
                     .message = "vkBindImageMemory failed"};
    }

    return memory;
}

void VulkanMemoryManager::freeMemory(VkDevice device, VkDeviceMemory memory) noexcept {
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory, nullptr);
    }
}

} // namespace eng::rhi::vk
