/// @file vulkan_memory.h
/// @brief Device local and host visible GPU memory allocator and type finder.
///
/// Purpose:
/// Resolves Vulkan memory type indices, allocates dedicated device memory for buffers
/// and textures, maps staging memory, and manages memory reclamation.
///
/// Ownership and Lifetime:
/// Owned by VulkanDevice. Outlives all allocated buffer and texture allocations.
///
/// Thread-safety:
/// Allocation and free calls must be synchronized by the owning VulkanDevice.

#pragma once

#include "core/result.h"
#include "rhi/rhi_types.h"
#include "vulkan_types.h"

namespace eng::rhi::vk {

/// @class VulkanMemoryManager
/// @brief GPU memory allocation and memory type resolution manager.
class VulkanMemoryManager {
public:
    explicit VulkanMemoryManager(VkPhysicalDevice physicalDevice) noexcept;
    ~VulkanMemoryManager() noexcept = default;

    /// Finds matching memory type index for given type filter and property requirements.
    [[nodiscard]] Result<uint32_t> findMemoryType(uint32_t typeFilter,
                                                  VkMemoryPropertyFlags properties) const noexcept;

    /// Allocates and binds device memory for a VkBuffer.
    [[nodiscard]] Result<VkDeviceMemory> allocateBufferMemory(VkDevice device, VkBuffer buffer,
                                                              MemoryUsage usage,
                                                              void** outMappedData = nullptr) const;

    /// Allocates and binds device memory for a VkImage.
    [[nodiscard]] Result<VkDeviceMemory> allocateImageMemory(VkDevice device, VkImage image,
                                                             MemoryUsage usage) const;

    /// Frees allocated device memory.
    static void freeMemory(VkDevice device, VkDeviceMemory memory) noexcept;

private:
    VkPhysicalDeviceMemoryProperties memoryProperties_{};
};

} // namespace eng::rhi::vk
