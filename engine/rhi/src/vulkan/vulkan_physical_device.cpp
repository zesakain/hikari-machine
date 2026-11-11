/// @file vulkan_physical_device.cpp
/// @brief Vulkan physical device scoring, queue discovery, and GpuCaps mapping implementation.

#include "vulkan_physical_device.h"

#include <algorithm>
#include <cstring>

namespace eng::rhi::vk {

VulkanPhysicalDevice::VulkanPhysicalDevice(VkPhysicalDevice device, QueueFamilyIndices queues,
                                           GpuCaps caps, std::string deviceName) noexcept
    : physicalDevice_(device), queues_(queues), caps_(caps), deviceName_(std::move(deviceName)) {
    // Retain stable pointer to device name in GpuCaps string_view
    caps_.deviceName = std::string_view(deviceName_);
}

Result<std::unique_ptr<VulkanPhysicalDevice>> VulkanPhysicalDevice::select(VkInstance instance,
                                                                           VkSurfaceKHR surface) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        ENG_LOG_ERROR("Vulkan", "No Vulkan-compatible physical devices found");
        return Error{.code = ErrorCode::DeviceCreationFailed,
                     .message = "No Vulkan physical devices available"};
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    const bool requirePresent = (surface != VK_NULL_HANDLE);

    struct Candidate {
        VkPhysicalDevice device{VK_NULL_HANDLE};
        QueueFamilyIndices queues{};
        int score{0};
        VkPhysicalDeviceProperties properties{};
        VkPhysicalDeviceFeatures features{};
        VkPhysicalDeviceMemoryProperties memory{};
    };

    std::vector<Candidate> candidates;

    for (VkPhysicalDevice device : devices) {
        Candidate cand{.device = device};
        vkGetPhysicalDeviceProperties(device, &cand.properties);
        vkGetPhysicalDeviceFeatures(device, &cand.features);
        vkGetPhysicalDeviceMemoryProperties(device, &cand.memory);

        // 1. Check queue families
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                cand.queues.graphicsFamily = i;
            }

            if (requirePresent) {
                VkBool32 presentSupport = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
                if (presentSupport == VK_TRUE) {
                    cand.queues.presentFamily = i;
                }
            }
        }

        if (!cand.queues.isComplete(requirePresent)) {
            continue; // Missing graphics or presentation queue
        }

        // 2. Check required device extensions
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> availableExts(extCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, availableExts.data());

        auto hasExt = [&](const char* name) {
            for (const auto& ext : availableExts) {
                if (std::strcmp(ext.extensionName, name) == 0) {
                    return true;
                }
            }
            return false;
        };

        if (requirePresent && !hasExt(VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }

        // Score based on GPU type and memory
        cand.score = 0;
        if (cand.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            cand.score += 1000;
        } else if (cand.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
            cand.score += 100;
        }

        // Add VRAM size bonus in MB
        for (uint32_t i = 0; i < cand.memory.memoryHeapCount; ++i) {
            if ((cand.memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
                cand.score +=
                    static_cast<int>(cand.memory.memoryHeaps[i].size / (1024ULL * 1024ULL));
            }
        }

        candidates.push_back(cand);
    }

    if (candidates.empty()) {
        ENG_LOG_ERROR("Vulkan", "No suitable GPU found with required queues and extensions");
        return Error{.code = ErrorCode::DeviceCreationFailed,
                     .message = "No suitable Vulkan physical device found"};
    }

    // Select candidate with highest score
    auto bestIt = std::ranges::max_element(
        candidates, [](const Candidate& a, const Candidate& b) { return a.score < b.score; });

    const Candidate& selected = *bestIt;
    std::string deviceName(selected.properties.deviceName);

    // Map capabilities (Pascal baseline is Tier 0)
    GpuCaps caps{};
    caps.tier = HardwareTier::Tier0;

    // Map limits
    caps.limits.maxTextureDimension1D = selected.properties.limits.maxImageDimension1D;
    caps.limits.maxTextureDimension2D = selected.properties.limits.maxImageDimension2D;
    caps.limits.maxTextureDimension3D = selected.properties.limits.maxImageDimension3D;
    caps.limits.maxTextureArrayLayers = selected.properties.limits.maxImageArrayLayers;
    caps.limits.maxUniformBufferBindingSize = selected.properties.limits.maxUniformBufferRange;
    caps.limits.maxStorageBufferBindingSize = static_cast<uint32_t>(
        std::min<uint64_t>(selected.properties.limits.maxStorageBufferRange, UINT32_MAX));
    caps.limits.maxBoundDescriptorSets = selected.properties.limits.maxBoundDescriptorSets;
    caps.limits.maxColorAttachments = selected.properties.limits.maxColorAttachments;
    caps.limits.maxSamplerAnisotropy = selected.properties.limits.maxSamplerAnisotropy;

    // Feature flags
    caps.features.dynamicRendering = true;
    caps.features.timelineSemaphores = true;
    caps.features.samplerAnisotropy = (selected.features.samplerAnisotropy == VK_TRUE);
    caps.features.descriptorIndexing = false;
    caps.features.rayTracing = false;
    caps.features.meshShaders = false;

    ENG_LOG_INFO("Vulkan", "Selected GPU device successfully");

    return std::unique_ptr<VulkanPhysicalDevice>(
        new VulkanPhysicalDevice(selected.device, selected.queues, caps, std::move(deviceName)));
}

} // namespace eng::rhi::vk
