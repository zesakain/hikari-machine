/// @file render_graph_types.h
/// @brief Types, identifiers, descriptors, and barrier transitions for the Render Graph.
///
/// Purpose:
/// Declares resource handles, lifetime ranges, access flags, and synchronization descriptors
/// used by the backend-independent Render Graph scheduler.
///
/// Ownership and Lifetime:
/// Resource IDs are 32-bit POD value types. Node and transition metadata are owned by RenderGraph.
///
/// Thread-safety:
/// Value types are thread-safe. Graph manipulation is restricted to the owning recording thread.
///
/// Performance:
/// Standard layout structs, zero dynamic allocation overhead.

#pragma once

#include "core/id.h"
#include "rhi/buffer.h"
#include "rhi/rhi_types.h"
#include "rhi/texture.h"

#include <cstdint>
#include <string_view>

namespace eng::render {

// Tag types for type-safe compile-time resource ID separation
struct GraphBufferTag {};
struct GraphTextureTag {};

/// Type-safe identifier for buffers managed by the Render Graph.
using GraphBufferId = StrongId<GraphBufferTag, uint32_t>;

/// Type-safe identifier for textures managed by the Render Graph.
using GraphTextureId = StrongId<GraphTextureTag, uint32_t>;

/// @enum ResourceAccessType
/// @brief Read/Write access mode for resources declared in a pass.
enum class ResourceAccessType : uint8_t { Read = 0, Write = 1, ReadWrite = 2 };

/// @struct ResourceLifetime
/// @brief First and last pass indices in topological execution order that access a resource.
struct ResourceLifetime {
    uint32_t firstPass{UINT32_MAX};
    uint32_t lastPass{0};

    [[nodiscard]] constexpr bool isUsed() const noexcept { return firstPass != UINT32_MAX; }

    [[nodiscard]] constexpr bool overlaps(const ResourceLifetime& other) const noexcept {
        if (!isUsed() || !other.isUsed()) {
            return false;
        }
        return (lastPass >= other.firstPass) && (other.lastPass >= firstPass);
    }
};

/// @struct TextureTransition
/// @brief Planned state transition and barrier for a texture between passes.
struct TextureTransition {
    GraphTextureId textureId;
    rhi::TextureHandle handle;
    rhi::TextureUsageFlags beforeState{rhi::TextureUsageFlags::None};
    rhi::TextureUsageFlags afterState{rhi::TextureUsageFlags::None};
};

/// @struct BufferTransition
/// @brief Planned state transition and barrier for a buffer between passes.
struct BufferTransition {
    GraphBufferId bufferId;
    rhi::BufferHandle handle;
    rhi::BufferUsageFlags beforeState{rhi::BufferUsageFlags::None};
    rhi::BufferUsageFlags afterState{rhi::BufferUsageFlags::None};
};

} // namespace eng::render
