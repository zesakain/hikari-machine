/// @file vulkan_swapchain.cpp
/// @brief SDL3 Vulkan swapchain implementation.

#include "vulkan_swapchain.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>

namespace eng::rhi::vk {

VulkanSwapchain::VulkanSwapchain(VkInstance instance, VkPhysicalDevice physicalDevice,
                                 VkDevice device, VkSurfaceKHR surface,
                                 uint32_t graphicsQueueFamily, uint32_t presentQueueFamily) noexcept
    : instance_(instance), physicalDevice_(physicalDevice), device_(device), surface_(surface),
      graphicsQueueFamily_(graphicsQueueFamily), presentQueueFamily_(presentQueueFamily) {}

VulkanSwapchain::~VulkanSwapchain() noexcept {
    cleanup();
    if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
        SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
}

void VulkanSwapchain::cleanup() noexcept {
    for (VkImageView view : imageViews_) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, view, nullptr);
        }
    }
    imageViews_.clear();
    images_.clear();

    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

Result<std::unique_ptr<VulkanSwapchain>>
VulkanSwapchain::create(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                        void* windowHandle, uint32_t graphicsQueueFamily,
                        uint32_t presentQueueFamily) {
    if (windowHandle == nullptr) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Cannot create VulkanSwapchain without valid window handle"};
    }

    auto* sdlWindow = static_cast<SDL_Window*>(windowHandle);
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(sdlWindow, instance, nullptr, &surface) ||
        surface == VK_NULL_HANDLE) {
        ENG_LOG_ERROR("Vulkan", "Failed to create SDL_Vulkan surface");
        return Error{.code = ErrorCode::SwapchainCreationFailed,
                     .message = "SDL_Vulkan_CreateSurface failed"};
    }

    auto swapchainObj = std::unique_ptr<VulkanSwapchain>(new VulkanSwapchain(
        instance, physicalDevice, device, surface, graphicsQueueFamily, presentQueueFamily));

    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(sdlWindow, &w, &h);
    auto initRes = swapchainObj->recreate(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    if (!initRes.hasValue()) {
        return initRes.error();
    }

    return swapchainObj;
}

Result<void> VulkanSwapchain::recreate(uint32_t newWidth, uint32_t newHeight) {
    if (newWidth == 0 || newHeight == 0) {
        // Window is minimized; pause swapchain operations safely
        return Result<void>::success();
    }

    vkDeviceWaitIdle(device_);

    // 1. Surface capabilities
    // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization)
    VkSurfaceCapabilitiesKHR capabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &capabilities);

    // 2. Surface formats
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    if (formatCount > 0) {
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount,
                                             formats.data());
    }

    VkSurfaceFormatKHR selectedFormat =
        formats.empty() ? VkSurfaceFormatKHR{.format = VK_FORMAT_B8G8R8A8_UNORM,
                                             .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                        : formats[0];
    for (const auto& fmt : formats) {
        if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM &&
            fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            selectedFormat = fmt;
            break;
        }
    }
    imageFormat_ = selectedFormat.format;

    // 3. Present modes
    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(modeCount);
    if (modeCount > 0) {
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &modeCount,
                                                  presentModes.data());
    }

    VkPresentModeKHR selectedMode = VK_PRESENT_MODE_FIFO_KHR; // Guaranteed support by spec
    for (const auto& mode : presentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            selectedMode = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        }
    }

    // 4. Extent
    if (capabilities.currentExtent.width != UINT32_MAX) {
        extent_ = capabilities.currentExtent;
    } else {
        extent_.width = std::clamp(newWidth, capabilities.minImageExtent.width,
                                   capabilities.maxImageExtent.width);
        extent_.height = std::clamp(newHeight, capabilities.minImageExtent.height,
                                    capabilities.maxImageExtent.height);
    }

    // 5. Image count (triple buffering if supported)
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .surface = surface_,
        .minImageCount = imageCount,
        .imageFormat = selectedFormat.format,
        .imageColorSpace = selectedFormat.colorSpace,
        .imageExtent = extent_,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = selectedMode,
        .clipped = VK_TRUE,
        .oldSwapchain = swapchain_,
    };

    uint32_t queueFamilyIndices[] = {graphicsQueueFamily_, presentQueueFamily_};
    if (graphicsQueueFamily_ != presentQueueFamily_) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }

    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    VkResult res = vkCreateSwapchainKHR(device_, &createInfo, nullptr, &newSwapchain);
    if (res != VK_SUCCESS) {
        ENG_LOG_ERROR("Vulkan", "Failed to create VkSwapchainKHR");
        return Error{.code = ErrorCode::SwapchainCreationFailed,
                     .message = "vkCreateSwapchainKHR failed"};
    }

    cleanup();
    swapchain_ = newSwapchain;

    // 6. Retrieve images and create views
    uint32_t actualImageCount = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &actualImageCount, nullptr);
    images_.resize(actualImageCount);
    vkGetSwapchainImagesKHR(device_, swapchain_, &actualImageCount, images_.data());

    imageViews_.resize(actualImageCount, VK_NULL_HANDLE);
    for (size_t i = 0; i < actualImageCount; ++i) {
        VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = images_[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = imageFormat_,
            .components =
                {
                    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                },
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };

        res = vkCreateImageView(device_, &viewInfo, nullptr, &imageViews_[i]);
        if (res != VK_SUCCESS) {
            ENG_LOG_ERROR("Vulkan", "Failed to create swapchain image view");
            return Error{.code = ErrorCode::SwapchainCreationFailed,
                         .message = "vkCreateImageView failed for swapchain image"};
        }
    }

    ENG_LOG_INFO("Vulkan", "Swapchain successfully created/recreated");
    return Result<void>::success();
}

Result<uint32_t> VulkanSwapchain::acquireNextImage(VkSemaphore semaphore) noexcept {
    uint32_t imageIndex = 0;
    VkResult res = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, semaphore, VK_NULL_HANDLE,
                                         &imageIndex);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        return Error{.code = ErrorCode::SwapchainCreationFailed,
                     .message = "Swapchain out of date or suboptimal"};
    }
    if (res != VK_SUCCESS) {
        return Error{.code = ErrorCode::SwapchainCreationFailed,
                     .message = "vkAcquireNextImageKHR failed"};
    }
    return imageIndex;
}

Result<void> VulkanSwapchain::present(VkQueue queue, uint32_t imageIndex,
                                      VkSemaphore waitSemaphore) noexcept {
    VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = (waitSemaphore != VK_NULL_HANDLE) ? 1U : 0U,
        .pWaitSemaphores = (waitSemaphore != VK_NULL_HANDLE) ? &waitSemaphore : nullptr,
        .swapchainCount = 1,
        .pSwapchains = &swapchain_,
        .pImageIndices = &imageIndex,
        .pResults = nullptr,
    };

    VkResult res = vkQueuePresentKHR(queue, &presentInfo);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        return Error{.code = ErrorCode::SwapchainCreationFailed,
                     .message = "Swapchain out of date during present"};
    }
    if (res != VK_SUCCESS) {
        return Error{.code = ErrorCode::SwapchainCreationFailed,
                     .message = "vkQueuePresentKHR failed"};
    }
    return Result<void>::success();
}

} // namespace eng::rhi::vk
