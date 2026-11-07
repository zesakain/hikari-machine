/// @file texture.h
/// @brief GPU texture resource creation descriptor.
///
/// Purpose:
/// Defines dimensions, pixel format, mip levels, array slices, and usage intent for GPU textures.
///
/// Ownership and Lifetime:
/// Descriptors are pure value types passed by const reference during texture creation.
///
/// Thread-safety:
/// Value type with const accessors; fully thread-safe.
///
/// Performance:
/// Standard layout struct, zero allocation overhead.

#pragma once

#include "rhi/rhi_types.h"

#include <cstdint>
#include <string_view>

namespace eng::rhi {

/// @struct TextureDesc
/// @brief Initialization descriptor for GPU texture resource creation.
struct TextureDesc {
    /// Texture dimensionality (2D, 3D, Cube, 2DArray).
    TextureType type{TextureType::Texture2D};

    /// Pixel format encoding.
    TextureFormat format{TextureFormat::RGBA8_UNORM};

    /// Width of the base mip level in texels (must be >= 1).
    uint32_t width{1};

    /// Height of the base mip level in texels (must be >= 1).
    uint32_t height{1};

    /// Depth in texels for 3D textures (must be >= 1).
    uint32_t depth{1};

    /// Number of mip levels (must be >= 1).
    uint32_t mipLevels{1};

    /// Number of array layers (must be >= 1). For cube maps, must be multiple of 6.
    uint32_t arrayLayers{1};

    /// Intended GPU usage bitmask (Sampled, Storage, ColorAttachment, etc.).
    TextureUsageFlags usage{TextureUsageFlags::None};

    /// Diagnostic name string for graphics debuggers.
    // NOLINTNEXTLINE(readability-redundant-member-init): Required for GCC -Wmissing-field-initializers
    std::string_view debugName{};
};

} // namespace eng::rhi
