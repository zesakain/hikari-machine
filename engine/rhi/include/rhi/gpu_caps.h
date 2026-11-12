/// @file gpu_caps.h
/// @brief GPU capabilities, hardware tiers, limits, and feature query structs.
///
/// Purpose:
/// Provides hardware-neutral capability descriptors. Ensures all rendering and allocation
/// decisions query capability flags and limits rather than matching GPU names or vendor IDs.
///
/// Ownership and Lifetime:
/// Value type with static/const lifetime owned by RhiDevice.
///
/// Thread-safety:
/// Read-only const references; fully thread-safe across all jobs.
///
/// Performance:
/// Standard layout struct, zero allocation overhead.

#pragma once

#include <cstdint>
#include <string_view>

namespace eng::rhi {

/// @enum HardwareTier
/// @brief Standardized engine hardware capability classification.
enum class HardwareTier : uint8_t {
    /// Tier 0 (Baseline): Pascal GTX 10-series, AMD Polaris, Apple M1. Forward+, cluster/tiled, no
    /// RT/mesh shaders.
    Tier0 = 0,

    /// Tier 1: Turing, RDNA2, Apple M2. Variable rate shading, basic descriptor indexing/bindless.
    Tier1 = 1,

    /// Tier 2: Modern architectures. Hardware ray tracing, mesh shaders, work graphs.
    Tier2 = 2
};

/// @struct GpuLimits
/// @brief Physical and architectural size limits imposed by the GPU backend.
struct GpuLimits {
    uint32_t maxTextureDimension1D{4096};
    uint32_t maxTextureDimension2D{4096};
    uint32_t maxTextureDimension3D{2048};
    uint32_t maxTextureArrayLayers{256};
    uint32_t maxUniformBufferBindingSize{65536};
    uint32_t maxStorageBufferBindingSize{128 * 1024 * 1024}; // 128 MB
    uint32_t maxBoundDescriptorSets{4};
    uint32_t maxColorAttachments{8};
    float maxSamplerAnisotropy{16.0F};
    float timestampPeriod{0.0F};
    uint32_t timestampValidBits{0};
};

/// @struct GpuFeatures
/// @brief Optional and mandatory graphics pipeline features.
struct GpuFeatures {
    /// Vulkan 1.3 dynamic rendering (no VkRenderPass / VkFramebuffer boilerplate).
    bool dynamicRendering{true};

    /// Timeline semaphores for unified CPU-GPU queue synchronization.
    bool timelineSemaphores{true};

    /// Hardware-accelerated anisotropic texture filtering.
    bool samplerAnisotropy{true};

    /// Hardware timestamp queries supported on graphics queue.
    bool timestampQueries{false};

    /// Bindless texture and buffer array indexing.
    bool descriptorIndexing{false};

    /// Hardware accelerated ray tracing pipeline and queries.
    bool rayTracing{false};

    /// Task and mesh shader pipeline execution.
    bool meshShaders{false};

    /// Wireframe and point rasterization modes (VK_POLYGON_MODE_LINE / POINT).
    bool fillModeNonSolid{false};
};

/// @struct GpuCaps
/// @brief Complete hardware and driver capability manifest reported by RhiDevice.
struct GpuCaps {
    HardwareTier tier{HardwareTier::Tier0};
    GpuLimits limits{};
    GpuFeatures features{};
    std::string_view deviceName{"Null Mock Device"};
};

} // namespace eng::rhi
