/// @file vulkan_device.h
/// @brief Concrete Vulkan implementation of RhiDevice.
///
/// Purpose:
/// Coordinates Vulkan instance, physical device, logical device, queues, memory manager,
/// and resource tables under the authoritative RhiDevice contract.
///
/// Ownership and Lifetime:
/// Created by createVulkanDevice() factory. Owns all Vulkan subsystems and resources.
///
/// Thread-safety:
/// Resource allocation and submission are serialized or single-threaded.

#pragma once

#include "rhi/rhi_device.h"
#include "vulkan_instance.h"
#include "vulkan_memory.h"
#include "vulkan_physical_device.h"
#include "vulkan_swapchain.h"
#include "vulkan_types.h"

#include <memory>
#include <vector>

namespace eng::rhi::vk {

/// @class VulkanDevice
/// @brief Vulkan implementation of the RhiDevice graphics hardware interface.
class VulkanDevice final : public RhiDevice {
public:
    static Result<std::unique_ptr<VulkanDevice>> create(const VulkanDeviceDesc& desc);
    ~VulkanDevice() noexcept override;

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;
    VulkanDevice(VulkanDevice&&) noexcept = delete;
    VulkanDevice& operator=(VulkanDevice&&) noexcept = delete;

    [[nodiscard]] const GpuCaps& getCaps() const noexcept override;

    [[nodiscard]] Result<BufferHandle> createBuffer(const BufferDesc& desc) override;
    void destroyBuffer(BufferHandle handle) override;

    [[nodiscard]] Result<TextureHandle> createTexture(const TextureDesc& desc) override;
    void destroyTexture(TextureHandle handle) override;

    [[nodiscard]] Result<SamplerHandle> createSampler(const SamplerDesc& desc) override;
    void destroySampler(SamplerHandle handle) override;

    [[nodiscard]] Result<std::unique_ptr<RhiCommandList>> createCommandList() override;
    Result<void> submit(RhiCommandList* commandList) override;
    void waitIdle() override;

    [[nodiscard]] Result<uint32_t> acquireNextImage() override;
    Result<void> present() override;

    // Internal backend helpers
    [[nodiscard]] VkDevice getLogicalDevice() const noexcept { return device_; }
    [[nodiscard]] VkBuffer resolveBuffer(BufferHandle handle) const noexcept;
    [[nodiscard]] VulkanMemoryManager& getMemoryManager() noexcept { return *memoryManager_; }
    [[nodiscard]] VulkanSwapchain* getSwapchain() noexcept { return swapchain_.get(); }
    [[nodiscard]] bool isBufferValid(BufferHandle handle) const noexcept;
    [[nodiscard]] bool isTextureValid(TextureHandle handle) const noexcept;
    [[nodiscard]] bool isSamplerValid(SamplerHandle handle) const noexcept;

private:
    explicit VulkanDevice(std::unique_ptr<VulkanInstance> instance,
                          std::unique_ptr<VulkanPhysicalDevice> physicalDevice, VkDevice device,
                          VkQueue graphicsQueue, VkQueue presentQueue, uint32_t graphicsQueueFamily,
                          VkCommandPool commandPool,
                          std::unique_ptr<VulkanMemoryManager> memoryManager,
                          std::unique_ptr<VulkanSwapchain> swapchain) noexcept;

    std::unique_ptr<VulkanInstance> instance_;
    std::unique_ptr<VulkanPhysicalDevice> physicalDevice_;
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue graphicsQueue_{VK_NULL_HANDLE};
    VkQueue presentQueue_{VK_NULL_HANDLE};
    uint32_t graphicsQueueFamily_{0};
    VkCommandPool commandPool_{VK_NULL_HANDLE};
    std::unique_ptr<VulkanMemoryManager> memoryManager_;
    std::unique_ptr<VulkanSwapchain> swapchain_;
    uint32_t currentSwapchainImageIndex_{0};

    std::vector<VulkanBufferSlot> buffers_;
    std::vector<VulkanTextureSlot> textures_;
    std::vector<VulkanSamplerSlot> samplers_;
};

} // namespace eng::rhi::vk
