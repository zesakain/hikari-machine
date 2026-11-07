/// @file vulkan_physical_device.h
/// @brief Physical GPU enumeration, queue family selection, and GpuCaps mapping.
///
/// Purpose:
/// Selects the optimal Vulkan physical device, identifies graphics and presentation queue
/// families, and maps discovered hardware limits and features into engine GpuCaps.
///
/// Ownership and Lifetime:
/// Owned by VulkanDevice.
///
/// Thread-safety:
/// Read-only accessors after device selection are thread-safe.

#pragma once

#include "core/result.h"
#include "rhi/gpu_caps.h"
#include "vulkan_types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eng::rhi::vk {

/// @struct QueueFamilyIndices
/// @brief Selected queue family indices for graphics and presentation.
struct QueueFamilyIndices {
    uint32_t graphicsFamily{UINT32_MAX};
    uint32_t presentFamily{UINT32_MAX};

    [[nodiscard]] bool isComplete(bool requirePresent) const noexcept {
        return graphicsFamily != UINT32_MAX && (!requirePresent || presentFamily != UINT32_MAX);
    }
};

/// @class VulkanPhysicalDevice
/// @brief Encapsulates a selected physical GPU device and its capabilities.
class VulkanPhysicalDevice {
public:
    static Result<std::unique_ptr<VulkanPhysicalDevice>> select(VkInstance instance,
                                                                VkSurfaceKHR surface);
    ~VulkanPhysicalDevice() noexcept = default;

    [[nodiscard]] VkPhysicalDevice handle() const noexcept { return physicalDevice_; }
    [[nodiscard]] const QueueFamilyIndices& getQueueFamilies() const noexcept { return queues_; }
    [[nodiscard]] const GpuCaps& getCaps() const noexcept { return caps_; }

private:
    VulkanPhysicalDevice(VkPhysicalDevice device, QueueFamilyIndices queues, GpuCaps caps,
                         std::string deviceName) noexcept;

    VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
    QueueFamilyIndices queues_{};
    GpuCaps caps_{};
    std::string deviceName_;
};

} // namespace eng::rhi::vk
