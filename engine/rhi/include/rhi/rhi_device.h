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
#include "rhi/gpu_caps.h"
#include "rhi/sampler.h"
#include "rhi/texture.h"

#include <memory>

namespace eng::rhi {

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

    /// Allocates a new GPU texture resource.
    ///
    /// @param desc Texture dimensions, format, and usage flags.
    /// @return Result containing valid TextureHandle or error.
    [[nodiscard]] virtual Result<TextureHandle> createTexture(const TextureDesc& desc) = 0;

    /// Releases a GPU texture resource and invalidates its generational handle.
    virtual void destroyTexture(TextureHandle handle) = 0;

    /// Creates a texture sampler state.
    ///
    /// @param desc Filtering and coordinate wrapping configuration.
    /// @return Result containing valid SamplerHandle or error.
    [[nodiscard]] virtual Result<SamplerHandle> createSampler(const SamplerDesc& desc) = 0;

    /// Releases a texture sampler and invalidates its generational handle.
    virtual void destroySampler(SamplerHandle handle) = 0;

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

    /// Returns opaque pointers to underlying backend device handles (or null in headless/null
    /// mode).
    [[nodiscard]] virtual NativeDeviceHandles getNativeDeviceHandles() const noexcept { return {}; }
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
