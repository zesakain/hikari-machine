/// @file rhi_device.h
/// @brief Abstract GPU device interface managing resources, command lists, and queue submission.
///
/// Purpose:
/// Defines the graphics device abstraction for resource allocation, hardware capabilities,
/// and command submission. Completely isolates graphics driver APIs behind engine interfaces.
///
/// Ownership and Lifetime:
/// Created at application boot and owned as std::unique_ptr<RhiDevice>.
/// Outlives all allocated GPU resources and command lists.
///
/// Thread-safety:
/// Resource creation and destruction are internally synchronized or thread-safe where documented.
/// Queue submission is serialized by the caller or backend worker.
///
/// Performance:
/// Resource allocation should be pre-warmed during standby mode preparation.
/// Avoid runtime allocations in the hot path.

#pragma once

#include "core/result.h"
#include "rhi/buffer.h"
#include "rhi/command_list.h"
#include "rhi/descriptor.h"
#include "rhi/gpu_caps.h"
#include "rhi/pipeline.h"
#include "rhi/sampler.h"
#include "rhi/shader.h"
#include "rhi/texture.h"

#include <memory>
#include <optional>
#include <span>

namespace eng::rhi {

/// @struct QueueTimingStats
/// @brief Microsecond-resolution CPU queue synchronization and dispatch telemetry.
struct QueueTimingStats {
    /// Wall-clock CPU time spent executing queue submission (e.g. vkQueueSubmit).
    float submitTimeMs{0.0F};
    /// Wall-clock CPU time spent waiting on GPU fence synchronization (e.g. vkWaitForFences).
    float fenceWaitTimeMs{0.0F};
    /// Wall-clock CPU time spent acquiring next swapchain image (e.g. vkAcquireNextImageKHR).
    float acquireTimeMs{0.0F};
    /// Wall-clock CPU time spent presenting rendered swapchain image (e.g. vkQueuePresentKHR).
    float presentTimeMs{0.0F};
};

/// @struct NativeDeviceHandles
/// @brief Opaque container for backend-specific native graphics device handles.
struct NativeDeviceHandles {
    void* instance{nullptr};
    void* physicalDevice{nullptr};
    void* device{nullptr};
    void* graphicsQueue{nullptr};
    uint32_t graphicsQueueFamily{0};
    uint32_t colorAttachmentFormat{0};
    uint32_t swapchainImageCount{0};
};

/// @class RhiDevice
/// @brief Abstract rendering hardware interface device.
class RhiDevice {
public:
    virtual ~RhiDevice() = default;

    /// Returns hardware capabilities, limits, and feature flags.
    [[nodiscard]] virtual const GpuCaps& getCaps() const noexcept = 0;

    /// Allocates a new GPU buffer resource.
    ///
    /// @param desc Buffer allocation and binding specification.
    /// @return Result containing valid BufferHandle or error.
    [[nodiscard]] virtual Result<BufferHandle> createBuffer(const BufferDesc& desc) = 0;

    /// Releases a GPU buffer resource and invalidates its generational handle.
    virtual void destroyBuffer(BufferHandle handle) = 0;

    /// Updates host-visible GPU buffer contents directly from CPU memory.
    ///
    /// @param handle Generational handle referencing the allocated buffer.
    /// @param offset Byte offset within the destination buffer.
    /// @param data Pointer to source host memory.
    /// @param size Number of bytes to transfer.
    /// @return Result indicating success or buffer access failure.
    virtual Result<void> writeBuffer(BufferHandle handle, size_t offset, const void* data,
                                     size_t size) {
        (void)handle;
        (void)offset;
        (void)data;
        (void)size;
        return Error{.code = ErrorCode::NotSupported, .message = "writeBuffer not implemented"};
    }

    /// Reads host-visible GPU buffer contents into CPU memory.
    ///
    /// @param handle Generational handle referencing the allocated buffer.
    /// @param offset Byte offset within the source buffer.
    /// @param outData Pointer to destination host memory.
    /// @param size Number of bytes to transfer.
    /// @return Result indicating success or buffer access failure.
    virtual Result<void> readBuffer(BufferHandle handle, size_t offset, void* outData,
                                    size_t size) {
        (void)handle;
        (void)offset;
        (void)outData;
        (void)size;
        return Error{.code = ErrorCode::NotSupported, .message = "readBuffer not implemented"};
    }

    /// Allocates a new GPU texture resource.
    ///
    /// @param desc Texture dimensions, format, and usage flags.
    /// @return Result containing valid TextureHandle or error.
    [[nodiscard]] virtual Result<TextureHandle> createTexture(const TextureDesc& desc) = 0;

    /// Releases a GPU texture resource and invalidates its generational handle.
    virtual void destroyTexture(TextureHandle handle) = 0;

    /// Retrieves the descriptor of a live texture handle, or nullptr if invalid.
    [[nodiscard]] virtual const TextureDesc* getTextureDesc(TextureHandle handle) const noexcept {
        (void)handle;
        return nullptr;
    }

    /// Queries whether a given texture format is supported by the device for specific usage flags.
    ///
    /// @param format Texture format to inspect.
    /// @param usage Intended usage flags (e.g. ColorAttachment, Sampled).
    /// @return True if hardware supports the format for the requested usages; false otherwise.
    [[nodiscard]] virtual bool isTextureFormatSupported(TextureFormat format,
                                                        TextureUsageFlags usage) const noexcept {
        (void)format;
        (void)usage;
        return format != TextureFormat::Undefined;
    }

    /// Queries whether a given texture format supports linear sampler filtering.
    ///
    /// @param format Texture format to inspect.
    /// @return True if hardware supports linear sampler filtering on the format; false otherwise.
    [[nodiscard]] virtual bool isTextureFormatFilterable(TextureFormat format) const noexcept {
        (void)format;
        return format != TextureFormat::Undefined;
    }

    /// Creates a texture sampler state.
    ///
    /// @param desc Filtering and coordinate wrapping configuration.
    /// @return Result containing valid SamplerHandle or error.
    [[nodiscard]] virtual Result<SamplerHandle> createSampler(const SamplerDesc& desc) = 0;

    /// Releases a texture sampler and invalidates its generational handle.
    virtual void destroySampler(SamplerHandle handle) = 0;

    /// Compiles a new shader module from SPIR-V bytecode.
    ///
    /// @param desc Shader descriptor containing stage and SPIR-V code span.
    /// @return Result containing valid ShaderHandle or error.
    [[nodiscard]] virtual Result<ShaderHandle> createShader(const ShaderDesc& desc) = 0;

    /// Releases a shader module and invalidates its generational handle.
    virtual void destroyShader(ShaderHandle handle) = 0;

    /// Creates a pipeline layout defining push constants and descriptor set bindings.
    ///
    /// @param desc Pipeline layout descriptor.
    /// @return Result containing valid PipelineLayoutHandle or error.
    [[nodiscard]] virtual Result<PipelineLayoutHandle>
    createPipelineLayout(const PipelineLayoutDesc& desc) = 0;

    /// Releases a pipeline layout and invalidates its generational handle.
    virtual void destroyPipelineLayout(PipelineLayoutHandle handle) = 0;

    /// Creates a graphics pipeline state object.
    ///
    /// @param desc Graphics pipeline descriptor with shader stages, vertex layout, and states.
    /// @return Result containing valid PipelineHandle or error.
    [[nodiscard]] virtual Result<PipelineHandle>
    createGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;

    /// Releases a graphics pipeline state object and invalidates its generational handle.
    virtual void destroyGraphicsPipeline(PipelineHandle handle) = 0;

    /// Creates a new descriptor set layout.
    ///
    /// @param desc Specification of binding indices, types, and stage visibility.
    /// @return Generational handle to new descriptor set layout or error.
    [[nodiscard]] virtual Result<DescriptorSetLayoutHandle>
    createDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) {
        (void)desc;
        return Error{.code = ErrorCode::NotSupported,
                     .message = "Descriptor set layouts not implemented on this backend"};
    }

    /// Destroys a descriptor set layout and invalidates its generational handle.
    virtual void destroyDescriptorSetLayout(DescriptorSetLayoutHandle handle) { (void)handle; }

    /// Creates a descriptor pool with configured capacity budgets.
    ///
    /// @param desc Maximum set count and per-type allocation limits.
    /// @return Generational handle to new descriptor pool or error.
    [[nodiscard]] virtual Result<DescriptorPoolHandle>
    createDescriptorPool(const DescriptorPoolDesc& desc) {
        (void)desc;
        return Error{.code = ErrorCode::NotSupported,
                     .message = "Descriptor pools not implemented on this backend"};
    }

    /// Destroys a descriptor pool and automatically invalidates all allocated descriptor sets.
    virtual void destroyDescriptorPool(DescriptorPoolHandle handle) { (void)handle; }

    /// Resets all descriptor sets allocated from the pool, invalidating existing set handles.
    ///
    /// @param handle Descriptor pool to reset.
    /// @return Result indicating success or error.
    virtual Result<void> resetDescriptorPool(DescriptorPoolHandle handle) {
        (void)handle;
        return Error{.code = ErrorCode::NotSupported,
                     .message = "resetDescriptorPool not implemented on this backend"};
    }

    /// Allocates a single descriptor set from a pool using the specified layout.
    ///
    /// @param pool Pool with available budget for the layout's required descriptors.
    /// @param layout Layout defining the set's binding interface.
    /// @return Generational handle to the allocated descriptor set or error.
    [[nodiscard]] virtual Result<DescriptorSetHandle>
    allocateDescriptorSet(DescriptorPoolHandle pool, DescriptorSetLayoutHandle layout) {
        (void)pool;
        (void)layout;
        return Error{.code = ErrorCode::NotSupported,
                     .message = "allocateDescriptorSet not implemented on this backend"};
    }

    /// Updates one or more descriptor binding slots on an allocated descriptor set.
    ///
    /// @param set Destination descriptor set.
    /// @param writes Batched descriptor resource write descriptors.
    /// @return Result indicating success or validation error.
    virtual Result<void> updateDescriptorSet(DescriptorSetHandle set,
                                             std::span<const DescriptorWrite> writes) {
        (void)set;
        (void)writes;
        return Error{.code = ErrorCode::NotSupported,
                     .message = "updateDescriptorSet not implemented on this backend"};
    }

    /// Creates an independent command recording list.
    ///
    /// @return Result containing unique pointer to RhiCommandList.
    [[nodiscard]] virtual Result<std::unique_ptr<RhiCommandList>> createCommandList() = 0;

    /// Submits a recorded command list to the GPU execution queue.
    ///
    /// @param commandList Pointer to recorded command list (must have called end()).
    /// @return Result indicating success or submission failure error.
    virtual Result<void> submit(RhiCommandList* commandList) = 0;

    /// Blocks CPU thread until all in-flight GPU commands complete execution.
    virtual void waitIdle() = 0;

    /// Acquires the next swapchain backbuffer image index for rendering.
    ///
    /// @return Result containing 0-based image index, or 0 in headless/null mode.
    [[nodiscard]] virtual Result<uint32_t> acquireNextImage() { return 0; }

    /// Presents the current frame buffer to the presentation surface.
    ///
    /// @return Result indicating presentation success or failure (no-op in headless/null mode).
    virtual Result<void> present() { return Result<void>::success(); }

    /// Recreates presentation swapchain on window resize or maximization.
    ///
    /// @param width New surface width in physical pixels.
    /// @param height New surface height in physical pixels.
    /// @return Result indicating resize success or failure (no-op in headless/null mode).
    virtual Result<void> resize(uint32_t width, uint32_t height) {
        (void)width;
        (void)height;
        return Result<void>::success();
    }

    /// Returns opaque pointers to underlying backend device handles (or null in headless/null
    /// mode).
    [[nodiscard]] virtual NativeDeviceHandles getNativeDeviceHandles() const noexcept { return {}; }

    /// Returns the native image view pointer (e.g. VkImageView) for a texture, or nullptr.
    [[nodiscard]] virtual void* getNativeTextureView(TextureHandle handle) const noexcept {
        (void)handle;
        return nullptr;
    }

    /// Returns the native sampler pointer (e.g. VkSampler) for a sampler, or nullptr.
    [[nodiscard]] virtual void* getNativeSampler(SamplerHandle handle) const noexcept {
        (void)handle;
        return nullptr;
    }

    /// Returns queue synchronization, fence wait, and presentation timing telemetry for the last
    /// frame.
    [[nodiscard]] virtual QueueTimingStats getQueueTimingStats() const noexcept { return {}; }

    /// Retrieves elapsed duration in milliseconds between two recorded GPU timestamps.
    ///
    /// Contract:
    /// - startQueryIndex and endQueryIndex must have been recorded within the active query pool.
    /// - Non-blocking: returns std::nullopt if the GPU has not finished executing the queries.
    ///
    /// Cost:
    /// Zero heap allocations. Non-blocking host query check. Allowed in hot path.
    ///
    /// @param startQueryIndex Query slot marking start of timed operation.
    /// @param endQueryIndex Query slot marking end of timed operation.
    /// @return Elapsed duration in milliseconds, or std::nullopt if not ready or unsupported.
    [[nodiscard]] virtual std::optional<float> getQueryTimeMs(uint32_t startQueryIndex,
                                                              uint32_t endQueryIndex) {
        (void)startQueryIndex;
        (void)endQueryIndex;
        return std::nullopt;
    }

    /// Asynchronously fetches raw 64-bit GPU query timestamps without blocking CPU execution.
    ///
    /// @param firstQuery Starting query slot index.
    /// @param queryCount Number of consecutive queries to query.
    /// @param outTimestamps Destination buffer of capacity at least queryCount.
    /// @return True if all queries were ready and populated, false if pending or unsupported.
    virtual bool getQueryResults(uint32_t firstQuery, uint32_t queryCount,
                                 uint64_t* outTimestamps) {
        (void)firstQuery;
        (void)queryCount;
        (void)outTimestamps;
        return false;
    }
};

/// Instantiates the in-memory headless Null RHI device.
///
/// @return Owning pointer to headless RhiDevice or error.
[[nodiscard]] Result<std::unique_ptr<RhiDevice>> createNullDevice();

/// @struct VulkanDeviceDesc
/// @brief Configuration settings for Vulkan RHI device instantiation.
struct VulkanDeviceDesc {
    /// Non-owning native window pointer (e.g. SDL_Window*) for swapchain presentation.
    void* windowHandle{nullptr};

    /// Whether to request API validation layers in development builds.
    bool enableValidation{true};
};

/// Instantiates the hardware-accelerated Vulkan RHI device.
///
/// @param desc Optional window handle and validation layer settings.
/// @return Owning pointer to Vulkan RhiDevice or error.
[[nodiscard]] Result<std::unique_ptr<RhiDevice>>
createVulkanDevice(const VulkanDeviceDesc& desc = {});

} // namespace eng::rhi
