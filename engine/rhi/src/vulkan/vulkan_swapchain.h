/// @file vulkan_swapchain.h
/// @brief SDL3 surface binding, swapchain recreation, image acquisition, and presentation.
///
/// Purpose:
/// Manages VkSurfaceKHR and VkSwapchainKHR lifecycle, formats, present modes, and frame
/// synchronization.
///
/// Ownership and Lifetime:
/// Owned by VulkanDevice. Destroyed before logical device and instance teardown.
///
/// Thread-safety:
/// Main thread only.

#pragma once

#include "core/result.h"
#include "vulkan_types.h"

#include <memory>
#include <vector>

namespace eng::rhi::vk {

/// @class VulkanSwapchain
/// @brief Surface and swapchain manager integrating with SDL3.
class VulkanSwapchain {
public:
    static Result<std::unique_ptr<VulkanSwapchain>>
    create(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
           void* windowHandle, uint32_t graphicsQueueFamily, uint32_t presentQueueFamily);
    ~VulkanSwapchain() noexcept;

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;
    VulkanSwapchain(VulkanSwapchain&&) noexcept = delete;
    VulkanSwapchain& operator=(VulkanSwapchain&&) noexcept = delete;

    /// Acquires the next swapchain image index using the internal acquisition semaphore.
    [[nodiscard]] Result<uint32_t> acquireNextImage() noexcept;

    /// Acquires the next swapchain image index for rendering with an explicit semaphore.
    [[nodiscard]] Result<uint32_t> acquireNextImage(VkSemaphore semaphore) noexcept;

    /// Queues the rendered image for presentation on the display.
    [[nodiscard]] Result<void> present(VkQueue queue, uint32_t imageIndex,
                                       VkSemaphore waitSemaphore) noexcept;

    /// Recreates swapchain with updated window dimensions.
    [[nodiscard]] Result<void> recreate(uint32_t newWidth, uint32_t newHeight);

    [[nodiscard]] VkSurfaceKHR getSurface() const noexcept { return surface_; }
    [[nodiscard]] VkSwapchainKHR getSwapchain() const noexcept { return swapchain_; }
    [[nodiscard]] VkFormat getImageFormat() const noexcept { return imageFormat_; }
    [[nodiscard]] VkExtent2D getExtent() const noexcept { return extent_; }
    [[nodiscard]] size_t getImageCount() const noexcept { return images_.size(); }
    [[nodiscard]] VkImageView getImageView(size_t index) const noexcept {
        return index < imageViews_.size() ? imageViews_[index] : VK_NULL_HANDLE;
    }

private:
    VulkanSwapchain(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                    VkSurfaceKHR surface, uint32_t graphicsQueueFamily,
                    uint32_t presentQueueFamily) noexcept;

    void cleanup() noexcept;

    VkInstance instance_{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    uint32_t graphicsQueueFamily_{0};
    uint32_t presentQueueFamily_{0};

    VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
    VkFormat imageFormat_{VK_FORMAT_B8G8R8A8_UNORM};
    VkExtent2D extent_{.width = 0, .height = 0};
    VkSemaphore acquireSemaphore_{VK_NULL_HANDLE};
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
};

} // namespace eng::rhi::vk
