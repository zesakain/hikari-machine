/// @file rhi_types.h
/// @brief Fundamental type definitions, generational handles, and flag enumerations for RHI.
///
/// Purpose:
/// Defines hardware-neutral, zero-cost types and generational handles for GPU resources
/// (buffers, textures, samplers, pipelines, shaders). Guarantees compile-time type safety
/// and generational stale-handle detection across RHI backends.
///
/// Ownership and Lifetime:
/// Resource handles are pure 64-bit value types wrapping GenerationalHandle.
///
/// Thread-safety:
/// Value types; inherently thread-safe.
///
/// Performance:
/// Fits in primitive 64-bit CPU registers; zero allocation overhead.

#pragma once

#include "core/generational_handle.h"

#include <cstdint>
#include <type_traits>

namespace eng::rhi {

/// @struct ResourceHandle
/// @brief Strongly-typed generational handle wrapping GenerationalHandle.
template <typename Tag> struct ResourceHandle {
    /// Underlying index and generation tracking.
    GenerationalHandle handle;

    constexpr ResourceHandle() noexcept = default;
    constexpr explicit ResourceHandle(GenerationalHandle h) noexcept : handle(h) {}

    /// Checks if the handle index and generation are non-sentinel values.
    [[nodiscard]] constexpr bool isValid() const noexcept { return handle.isValid(); }

    /// Boolean conversion operator checking validity.
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return isValid(); }

    /// Returns the 32-bit slot index.
    [[nodiscard]] constexpr uint32_t index() const noexcept { return handle.index; }

    /// Returns the 32-bit generation counter.
    [[nodiscard]] constexpr uint32_t generation() const noexcept { return handle.generation; }

    /// Three-way comparison operator.
    constexpr auto operator<=>(const ResourceHandle&) const = default;
};

// Tag types for compile-time resource handle discrimination
struct BufferTag {};
struct TextureTag {};
struct SamplerTag {};
struct PipelineTag {};
struct PipelineLayoutTag {};
struct ShaderTag {};
struct DescriptorSetLayoutTag {};
struct DescriptorPoolTag {};
struct DescriptorSetTag {};

/// Generational handle for GPU buffer resources.
using BufferHandle = ResourceHandle<BufferTag>;

/// Generational handle for GPU texture/image resources.
using TextureHandle = ResourceHandle<TextureTag>;

/// Generational handle for GPU texture samplers.
using SamplerHandle = ResourceHandle<SamplerTag>;

/// Generational handle for compiled graphics/compute pipeline state objects.
using PipelineHandle = ResourceHandle<PipelineTag>;

/// Generational handle for pipeline layouts (push constants and descriptor sets).
using PipelineLayoutHandle = ResourceHandle<PipelineLayoutTag>;

/// Generational handle for compiled shader modules.
using ShaderHandle = ResourceHandle<ShaderTag>;

/// Generational handle for descriptor set layouts.
using DescriptorSetLayoutHandle = ResourceHandle<DescriptorSetLayoutTag>;

/// Generational handle for descriptor pools.
using DescriptorPoolHandle = ResourceHandle<DescriptorPoolTag>;

/// Generational handle for descriptor sets.
using DescriptorSetHandle = ResourceHandle<DescriptorSetTag>;

/// @enum ShaderStageFlags
/// @brief Bitmask specifying shader stages (vertex, fragment, compute).
enum class ShaderStageFlags : uint32_t {
    None = 0,
    Vertex = 1 << 0,
    Fragment = 1 << 1,
    Compute = 1 << 2,
    AllGraphics = Vertex | Fragment,
    All = 0x7FFFFFFF
};

constexpr ShaderStageFlags operator|(ShaderStageFlags a, ShaderStageFlags b) noexcept {
    return static_cast<ShaderStageFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr ShaderStageFlags operator&(ShaderStageFlags a, ShaderStageFlags b) noexcept {
    return static_cast<ShaderStageFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
constexpr ShaderStageFlags& operator|=(ShaderStageFlags& a, ShaderStageFlags b) noexcept {
    a = a | b;
    return a;
}
constexpr ShaderStageFlags& operator&=(ShaderStageFlags& a, ShaderStageFlags b) noexcept {
    a = a & b;
    return a;
}
constexpr bool hasFlag(ShaderStageFlags flags, ShaderStageFlags test) noexcept {
    return (flags & test) == test;
}

/// @enum VertexFormat
/// @brief Vertex attribute component layout and data format.
enum class VertexFormat : uint8_t {
    Float = 0,
    Float2 = 1,
    Float3 = 2,
    Float4 = 3,
    Byte4Norm = 4,
    UByte4Norm = 5,
    Short2 = 6,
    Short4 = 7,
    Int = 8,
    UInt = 9
};

/// @enum VertexInputRate
/// @brief Step rate for vertex buffer attribute reading.
enum class VertexInputRate : uint8_t { Vertex = 0, Instance = 1 };

/// @enum PrimitiveTopology
/// @brief Primitive assembly interpretation.
enum class PrimitiveTopology : uint8_t {
    TriangleList = 0,
    TriangleStrip = 1,
    LineList = 2,
    LineStrip = 3,
    PointList = 4
};

/// @enum PolygonMode
/// @brief Rasterization polygon fill mode.
enum class PolygonMode : uint8_t { Fill = 0, Line = 1, Point = 2 };

/// @enum CullMode
/// @brief Face culling mode.
enum class CullMode : uint8_t { None = 0, Front = 1, Back = 2, FrontAndBack = 3 };

/// @enum FrontFace
/// @brief Front-facing winding order.
enum class FrontFace : uint8_t { CounterClockwise = 0, Clockwise = 1 };

/// @enum BlendFactor
/// @brief Source and destination blend factors for color blending.
enum class BlendFactor : uint8_t {
    Zero = 0,
    One = 1,
    SrcColor = 2,
    OneMinusSrcColor = 3,
    DstColor = 4,
    OneMinusDstColor = 5,
    SrcAlpha = 6,
    OneMinusSrcAlpha = 7,
    DstAlpha = 8,
    OneMinusDstAlpha = 9
};

/// @enum BlendOp
/// @brief Arithmetic operations for color blending.
enum class BlendOp : uint8_t { Add = 0, Subtract = 1, ReverseSubtract = 2, Min = 3, Max = 4 };

/// @enum ColorComponentFlags
/// @brief Bitmask for color attachment channel write masks.
enum class ColorComponentFlags : uint8_t {
    None = 0,
    R = 1 << 0,
    G = 1 << 1,
    B = 1 << 2,
    A = 1 << 3,
    All = R | G | B | A
};

constexpr ColorComponentFlags operator|(ColorComponentFlags a, ColorComponentFlags b) noexcept {
    return static_cast<ColorComponentFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr ColorComponentFlags operator&(ColorComponentFlags a, ColorComponentFlags b) noexcept {
    return static_cast<ColorComponentFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
constexpr bool hasFlag(ColorComponentFlags flags, ColorComponentFlags test) noexcept {
    return (flags & test) == test;
}

/// @enum DescriptorType
/// @brief Type of resource referenced in descriptor set bindings.
enum class DescriptorType : uint8_t {
    UniformBuffer = 0,
    StorageBuffer = 1,
    SampledImage = 2,
    Sampler = 3,
    CombinedImageSampler = 4,
    StorageImage = 5
};

/// @enum MemoryUsage
/// @brief Intended GPU/CPU memory domain and cache residency.
enum class MemoryUsage : uint8_t {
    /// Device local memory. Fastest for GPU access; inaccessible or slow from CPU.
    GpuOnly = 0,

    /// Host visible and coherent memory staged for frequent CPU-to-GPU streaming.
    CpuToGpu = 1,

    /// Host visible memory designed for GPU readback to CPU.
    GpuToCpu = 2
};

/// @enum BufferUsageFlags
/// @brief Bitmask specifying intended GPU buffer binding capabilities.
// NOLINTNEXTLINE(performance-enum-size): Buffer usage bitmask flags up to 32 bits.
enum class BufferUsageFlags : uint32_t {
    None = 0,
    Vertex = 1 << 0,
    Index = 1 << 1,
    Uniform = 1 << 2,
    Storage = 1 << 3,
    TransferSrc = 1 << 4,
    TransferDst = 1 << 5
};

constexpr BufferUsageFlags operator|(BufferUsageFlags a, BufferUsageFlags b) noexcept {
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    return static_cast<BufferUsageFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr BufferUsageFlags operator&(BufferUsageFlags a, BufferUsageFlags b) noexcept {
    return static_cast<BufferUsageFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
constexpr BufferUsageFlags& operator|=(BufferUsageFlags& a, BufferUsageFlags b) noexcept {
    a = a | b;
    return a;
}
constexpr BufferUsageFlags& operator&=(BufferUsageFlags& a, BufferUsageFlags b) noexcept {
    a = a & b;
    return a;
}
constexpr bool hasFlag(BufferUsageFlags flags, BufferUsageFlags test) noexcept {
    return (flags & test) == test;
}

/// @enum TextureType
/// @brief Dimensionality of texture resource.
enum class TextureType : uint8_t {
    Texture2D = 0,
    TextureCube = 1,
    Texture3D = 2,
    Texture2DArray = 3
};

/// @enum TextureFormat
/// @brief Standardized pixel data formats.
// NOLINTNEXTLINE(performance-enum-size): Texture formats span multi-category ranges.
enum class TextureFormat : uint16_t {
    Undefined = 0,

    // 8-bit normalized
    R8_UNORM = 1,
    RGBA8_UNORM = 2,
    RGBA8_SRGB = 3,
    BGRA8_UNORM = 4,
    BGRA8_SRGB = 5,

    // 16-bit floating point & integer
    RGBA16_FLOAT = 10,
    RGBA16_UNORM = 11,

    // 32-bit floating point
    R32_FLOAT = 20,
    RG32_FLOAT = 21,
    RGB32_FLOAT = 22,
    RGBA32_FLOAT = 23,

    // Depth / Stencil
    D32_FLOAT = 30,
    D24_UNORM_S8_UINT = 31,
    D32_FLOAT_S8_UINT = 32
};

/// Checks if a texture format is a depth or depth-stencil format.
[[nodiscard]] constexpr bool isDepthFormat(TextureFormat format) noexcept {
    return format == TextureFormat::D32_FLOAT || format == TextureFormat::D24_UNORM_S8_UINT ||
           format == TextureFormat::D32_FLOAT_S8_UINT;
}

/// Checks if a texture format is a valid color format (non-undefined and non-depth).
[[nodiscard]] constexpr bool isColorFormat(TextureFormat format) noexcept {
    return format != TextureFormat::Undefined && !isDepthFormat(format);
}

/// @enum TextureUsageFlags
/// @brief Bitmask specifying allowed GPU texture operations.
// NOLINTNEXTLINE(performance-enum-size): Texture usage bitmask flags up to 32 bits.
enum class TextureUsageFlags : uint32_t {
    None = 0,
    Sampled = 1 << 0,
    Storage = 1 << 1,
    ColorAttachment = 1 << 2,
    DepthStencilAttachment = 1 << 3,
    TransferSrc = 1 << 4,
    TransferDst = 1 << 5
};

constexpr TextureUsageFlags operator|(TextureUsageFlags a, TextureUsageFlags b) noexcept {
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    return static_cast<TextureUsageFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr TextureUsageFlags operator&(TextureUsageFlags a, TextureUsageFlags b) noexcept {
    return static_cast<TextureUsageFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
constexpr TextureUsageFlags& operator|=(TextureUsageFlags& a, TextureUsageFlags b) noexcept {
    a = a | b;
    return a;
}
constexpr TextureUsageFlags& operator&=(TextureUsageFlags& a, TextureUsageFlags b) noexcept {
    a = a & b;
    return a;
}
constexpr bool hasFlag(TextureUsageFlags flags, TextureUsageFlags test) noexcept {
    return (flags & test) == test;
}

/// @enum TextureLayout
/// @brief Abstract GPU texture layout state for synchronization and pipeline barriers.
enum class TextureLayout : uint8_t {
    Undefined = 0,
    General = 1,
    ColorAttachment = 2,
    DepthStencilAttachment = 3,
    DepthStencilReadOnly = 4,
    ShaderReadOnly = 5,
    TransferSrc = 6,
    TransferDst = 7,
    Present = 8
};

/// @enum Filter
/// @brief Filtering modes for texture minification, magnification, and mipmapping.
enum class Filter : uint8_t { Nearest = 0, Linear = 1 };

/// @enum SamplerAddressMode
/// @brief Texture coordinate addressing and wrap modes.
enum class SamplerAddressMode : uint8_t {
    Repeat = 0,
    MirroredRepeat = 1,
    ClampToEdge = 2,
    ClampToBorder = 3
};

/// @enum CompareOp
/// @brief Comparison operators for depth and shadow map sampling.
enum class CompareOp : uint8_t {
    Never = 0,
    Less = 1,
    Equal = 2,
    LessOrEqual = 3,
    Greater = 4,
    NotEqual = 5,
    GreaterOrEqual = 6,
    Always = 7
};

} // namespace eng::rhi
