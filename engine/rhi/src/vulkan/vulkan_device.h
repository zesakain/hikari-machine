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
#include <mutex>
#include <vector>

namespace eng::rhi::vk {

class VulkanCommandList;

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
    Result<void> writeBuffer(BufferHandle handle, size_t offset, const void* data,
                             size_t size) override;
    Result<void> readBuffer(BufferHandle handle, size_t offset, void* outData,
                            size_t size) override;

    [[nodiscard]] Result<TextureHandle> createTexture(const TextureDesc& desc) override;
    void destroyTexture(TextureHandle handle) override;
    [[nodiscard]] bool isTextureFormatSupported(TextureFormat format,
                                                TextureUsageFlags usage) const noexcept override;
    [[nodiscard]] bool isTextureFormatFilterable(TextureFormat format) const noexcept override;

    [[nodiscard]] Result<SamplerHandle> createSampler(const SamplerDesc& desc) override;
    void destroySampler(SamplerHandle handle) override;

    [[nodiscard]] Result<ShaderHandle> createShader(const ShaderDesc& desc) override;
    void destroyShader(ShaderHandle handle) override;

    [[nodiscard]] Result<PipelineLayoutHandle>
    createPipelineLayout(const PipelineLayoutDesc& desc) override;
    void destroyPipelineLayout(PipelineLayoutHandle handle) override;

    [[nodiscard]] Result<PipelineHandle>
    createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    void destroyGraphicsPipeline(PipelineHandle handle) override;

    [[nodiscard]] Result<DescriptorSetLayoutHandle>
    createDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) override;
    void destroyDescriptorSetLayout(DescriptorSetLayoutHandle handle) override;

    [[nodiscard]] Result<DescriptorPoolHandle>
    createDescriptorPool(const DescriptorPoolDesc& desc) override;
    void destroyDescriptorPool(DescriptorPoolHandle handle) override;
    Result<void> resetDescriptorPool(DescriptorPoolHandle handle) override;

    [[nodiscard]] Result<DescriptorSetHandle>
    allocateDescriptorSet(DescriptorPoolHandle pool, DescriptorSetLayoutHandle layout) override;
    Result<void> updateDescriptorSet(DescriptorSetHandle set,
                                     std::span<const DescriptorWrite> writes) override;

    [[nodiscard]] Result<std::unique_ptr<RhiCommandList>> createCommandList() override;
    Result<void> submit(RhiCommandList* commandList) override;
    void waitIdle() override;

    [[nodiscard]] Result<uint32_t> acquireNextImage() override;
    Result<void> present() override;
    Result<void> resize(uint32_t width, uint32_t height) override;
    Result<void> setVsync(bool enabled) override;
    [[nodiscard]] bool isVsyncEnabled() const noexcept override;

    [[nodiscard]] NativeDeviceHandles getNativeDeviceHandles() const noexcept override;
    [[nodiscard]] void* getNativeTextureView(TextureHandle handle) const noexcept override;
    [[nodiscard]] void* getNativeSampler(SamplerHandle handle) const noexcept override;

    [[nodiscard]] QueueTimingStats getQueueTimingStats() const noexcept override {
        return queueTimingStats_;
    }
    [[nodiscard]] std::optional<float> getQueryTimeMs(uint32_t startQueryIndex,
                                                      uint32_t endQueryIndex) override;
    bool getQueryResults(uint32_t firstQuery, uint32_t queryCount,
                         uint64_t* outTimestamps) override;

    // Internal backend helpers
    [[nodiscard]] VkDevice getLogicalDevice() const noexcept { return device_; }
    [[nodiscard]] VkQueryPool getQueryPool() const noexcept { return queryPool_; }
    [[nodiscard]] VulkanPhysicalDevice* getPhysicalDevice() noexcept {
        return physicalDevice_.get();
    }
    [[nodiscard]] const VulkanPhysicalDevice* getPhysicalDevice() const noexcept {
        return physicalDevice_.get();
    }
    [[nodiscard]] VkBuffer resolveBuffer(BufferHandle handle) const noexcept;
    [[nodiscard]] VkImage resolveTextureImage(TextureHandle handle) const noexcept;
    [[nodiscard]] VkImageView resolveTextureView(TextureHandle handle) const noexcept;
    [[nodiscard]] VkSampler resolveSampler(SamplerHandle handle) const noexcept;
    [[nodiscard]] VkImageLayout getTextureLayout(TextureHandle handle) const noexcept;
    void setTextureLayout(TextureHandle handle, VkImageLayout layout) noexcept;
    [[nodiscard]] const TextureDesc* getTextureDesc(TextureHandle handle) const noexcept override;
    [[nodiscard]] VkShaderModule resolveShader(ShaderHandle handle) const noexcept;
    [[nodiscard]] VkPipelineLayout
    resolvePipelineLayout(PipelineLayoutHandle handle) const noexcept;
    [[nodiscard]] VkPipeline resolvePipeline(PipelineHandle handle) const noexcept;
    [[nodiscard]] VkDescriptorSetLayout
    resolveDescriptorSetLayout(DescriptorSetLayoutHandle handle) const noexcept;
    [[nodiscard]] VkDescriptorPool
    resolveDescriptorPool(DescriptorPoolHandle handle) const noexcept;
    [[nodiscard]] VkDescriptorSet resolveDescriptorSet(DescriptorSetHandle handle) const noexcept;
    [[nodiscard]] const ShaderDesc* getShaderDesc(ShaderHandle handle) const noexcept;
    [[nodiscard]] const PipelineLayoutDesc*
    getPipelineLayoutDesc(PipelineLayoutHandle handle) const noexcept;
    [[nodiscard]] const DescriptorSetLayoutDesc*
    getDescriptorSetLayoutDesc(DescriptorSetLayoutHandle handle) const noexcept;
    [[nodiscard]] const DescriptorPoolDesc*
    getDescriptorPoolDesc(DescriptorPoolHandle handle) const noexcept;
    [[nodiscard]] const GraphicsPipelineDesc* getPipelineDesc(PipelineHandle handle) const noexcept;
    [[nodiscard]] VulkanMemoryManager& getMemoryManager() noexcept { return *memoryManager_; }
    [[nodiscard]] VulkanSwapchain* getSwapchain() noexcept { return swapchain_.get(); }
    [[nodiscard]] uint32_t getCurrentSwapchainImageIndex() const noexcept {
        return currentSwapchainImageIndex_;
    }
    [[nodiscard]] bool isBufferValid(BufferHandle handle) const noexcept;
    [[nodiscard]] bool isTextureValid(TextureHandle handle) const noexcept;
    [[nodiscard]] bool isSamplerValid(SamplerHandle handle) const noexcept;
    [[nodiscard]] bool isShaderValid(ShaderHandle handle) const noexcept;
    [[nodiscard]] bool isPipelineLayoutValid(PipelineLayoutHandle handle) const noexcept;
    [[nodiscard]] bool isPipelineValid(PipelineHandle handle) const noexcept;
    [[nodiscard]] bool isDescriptorSetLayoutValid(DescriptorSetLayoutHandle handle) const noexcept;
    [[nodiscard]] bool isDescriptorPoolValid(DescriptorPoolHandle handle) const noexcept;
    [[nodiscard]] bool isDescriptorSetValid(DescriptorSetHandle handle) const noexcept;

    void registerCommandList(VulkanCommandList* cmdList);
    void unregisterCommandList(VulkanCommandList* cmdList);

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
    VkQueryPool queryPool_{VK_NULL_HANDLE};
    static constexpr uint32_t kMaxTimestampQueries = 64;
    QueueTimingStats queueTimingStats_{};

    std::vector<VulkanBufferSlot> buffers_;
    std::vector<VulkanTextureSlot> textures_;
    std::vector<VulkanSamplerSlot> samplers_;
    std::vector<VulkanShaderSlot> shaders_;
    std::vector<VulkanPipelineLayoutSlot> pipelineLayouts_;
    std::vector<VulkanPipelineSlot> pipelines_;
    std::vector<VulkanDescriptorSetLayoutSlot> descriptorSetLayouts_;
    std::vector<VulkanDescriptorPoolSlot> descriptorPools_;
    std::vector<VulkanDescriptorSetSlot> descriptorSets_;

    std::vector<VulkanCommandList*> commandLists_;
    mutable std::mutex commandListMutex_;
};

} // namespace eng::rhi::vk
