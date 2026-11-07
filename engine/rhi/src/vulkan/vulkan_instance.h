/// @file vulkan_instance.h
/// @brief Vulkan instance lifecycle, extension loading, and debug messenger management.
///
/// Purpose:
/// Creates and manages the global VkInstance, queries platform surface extensions via SDL3,
/// and dynamically hooks validation layer diagnostics when available.
///
/// Ownership and Lifetime:
/// Owned by VulkanDevice. Destroyed after all physical/logical devices are destroyed.
///
/// Thread-safety:
/// Created and destroyed on the main thread.

#pragma once

#include "core/result.h"
#include "vulkan_types.h"

#include <memory>
#include <string>
#include <vector>

namespace eng::rhi::vk {

/// @class VulkanInstance
/// @brief Wrapper managing VkInstance and debug messenger lifecycle.
class VulkanInstance {
public:
    static Result<std::unique_ptr<VulkanInstance>> create(bool enableValidation,
                                                          bool requireSurface);
    ~VulkanInstance() noexcept;

    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;
    VulkanInstance(VulkanInstance&&) noexcept = delete;
    VulkanInstance& operator=(VulkanInstance&&) noexcept = delete;

    [[nodiscard]] VkInstance handle() const noexcept { return instance_; }
    [[nodiscard]] bool hasValidation() const noexcept { return validationEnabled_; }

private:
    explicit VulkanInstance(VkInstance instance, bool validationEnabled) noexcept;

    VkInstance instance_{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT debugMessenger_{VK_NULL_HANDLE};
    bool validationEnabled_{false};
};

} // namespace eng::rhi::vk
