/// @file buffer.h
/// @brief GPU buffer resource creation descriptor.
///
/// Purpose:
/// Declares allocation parameters, usage flags, and memory domain for GPU buffer creation.
///
/// Ownership and Lifetime:
/// Descriptors are pure value types passed by const reference during buffer creation.
///
/// Thread-safety:
/// Value type with const accessors; fully thread-safe.
///
/// Performance:
/// Standard layout struct, zero allocation overhead.

#pragma once

#include "rhi/rhi_types.h"

#include <cstddef>
#include <string_view>

namespace eng::rhi {

/// @struct BufferDesc
/// @brief Initialization descriptor for GPU buffer creation.
struct BufferDesc {
    /// Total buffer capacity in bytes (must be > 0 and <= maxStorageBufferBindingSize).
    size_t sizeInBytes{0};

    /// Allowed GPU binding capabilities (Vertex, Index, Uniform, Storage, etc.).
    BufferUsageFlags usage{BufferUsageFlags::None};

    /// CPU/GPU memory residency and caching behavior.
    MemoryUsage memoryUsage{MemoryUsage::GpuOnly};

    /// Non-owning diagnostic label for GPU profilers (RenderDoc/Tracy).
    // NOLINTNEXTLINE(readability-redundant-member-init): Required for GCC
    // -Wmissing-field-initializers
    std::string_view debugName{};
};

} // namespace eng::rhi
