/// @file vulkan_instance.cpp
/// @brief Vulkan instance and debug diagnostics implementation.

#include "vulkan_instance.h"

#include <SDL3/SDL_vulkan.h>
#include <cstring>

namespace eng::rhi::vk {

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
              VkDebugUtilsMessageTypeFlagsEXT /*messageTypes*/,
              const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* /*pUserData*/) {
    if (pCallbackData == nullptr || pCallbackData->pMessage == nullptr) {
        return VK_FALSE;
    }

    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        ENG_LOG_ERROR("Vulkan", pCallbackData->pMessage);
    } else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
        ENG_LOG_WARN("Vulkan", pCallbackData->pMessage);
    } else {
        ENG_LOG_DEBUG("Vulkan", pCallbackData->pMessage);
    }

    return VK_FALSE;
}

} // namespace

VulkanInstance::VulkanInstance(VkInstance instance, bool validationEnabled) noexcept
    : instance_(instance), validationEnabled_(validationEnabled) {}

VulkanInstance::~VulkanInstance() noexcept {
    if (debugMessenger_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
        auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (func != nullptr) {
            func(instance_, debugMessenger_, nullptr);
        }
        debugMessenger_ = VK_NULL_HANDLE;
    }

    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

Result<std::unique_ptr<VulkanInstance>> VulkanInstance::create(bool enableValidation,
                                                               bool requireSurface) {
    // 1. Enumerate available layers
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    if (layerCount > 0) {
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
    }

    bool hasKhronosValidation = false;
    for (const auto& layer : availableLayers) {
        if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            hasKhronosValidation = true;
            break;
        }
    }

    std::vector<const char*> enabledLayers;
    bool validationActive = false;
    if (enableValidation) {
        if (hasKhronosValidation) {
            enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
            validationActive = true;
            ENG_LOG_INFO("Vulkan", "Validation layer VK_LAYER_KHRONOS_validation enabled");
        } else {
            ENG_LOG_WARN("Vulkan", "Validation requested but VK_LAYER_KHRONOS_validation is not "
                                   "available; continuing without it");
        }
    }

    // 2. Enumerate available extensions
    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    if (extensionCount > 0) {
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount,
                                               availableExtensions.data());
    }

    auto isExtensionSupported = [&](const char* name) {
        for (const auto& ext : availableExtensions) {
            if (std::strcmp(ext.extensionName, name) == 0) {
                return true;
            }
        }
        return false;
    };

    std::vector<const char*> enabledExtensions;

    // Platform surface extensions via SDL3 if surface is required
    if (requireSurface) {
        uint32_t sdlExtCount = 0;
        char const* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
        if (sdlExtensions != nullptr && sdlExtCount > 0) {
            for (uint32_t i = 0; i < sdlExtCount; ++i) {
                enabledExtensions.push_back(sdlExtensions[i]);
            }
        }
    }

    // Debug utils extension if validation is active
    bool hasDebugUtils = isExtensionSupported(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (validationActive && hasDebugUtils) {
        enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    // Check portability enumeration
    VkInstanceCreateFlags createFlags = 0;
    if (isExtensionSupported(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        enabledExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        createFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    VkApplicationInfo appInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "Hikari Engine",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "Hikari",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };

    VkInstanceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = createFlags,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(enabledLayers.size()),
        .ppEnabledLayerNames = enabledLayers.empty() ? nullptr : enabledLayers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size()),
        .ppEnabledExtensionNames = enabledExtensions.empty() ? nullptr : enabledExtensions.data(),
    };

    VkInstance instance = VK_NULL_HANDLE;
    VkResult res = vkCreateInstance(&createInfo, nullptr, &instance);
    if (res != VK_SUCCESS || instance == VK_NULL_HANDLE) {
        ENG_LOG_ERROR("Vulkan", "Failed to create VkInstance");
        return Error{.code = ErrorCode::RhiInitFailed, .message = "vkCreateInstance failed"};
    }

    auto vulkanInstance =
        std::unique_ptr<VulkanInstance>(new VulkanInstance(instance, validationActive));

    // 3. Setup debug messenger if supported
    if (validationActive && hasDebugUtils) {
        auto createMessengerFunc = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (createMessengerFunc != nullptr) {
            VkDebugUtilsMessengerCreateInfoEXT messengerInfo{
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                .pNext = nullptr,
                .flags = 0,
                .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
                .pfnUserCallback = debugCallback,
                .pUserData = nullptr,
            };

            createMessengerFunc(instance, &messengerInfo, nullptr,
                                &vulkanInstance->debugMessenger_);
        }
    }

    return vulkanInstance;
}

} // namespace eng::rhi::vk
