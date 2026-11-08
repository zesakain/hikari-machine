/// @file asset_types.h
/// @brief Asset identity, metadata, type classifications, and path hashing primitives.
///
/// Purpose:
/// Defines hardware-neutral, persistent asset identifiers (AssetId), asset metadata,
/// and type classifications. All asset references across scenes, ECS components,
/// and editor panels use AssetId to avoid storing transient pointers or GPU handles.
///
/// Ownership and Lifetime:
/// AssetId is a pure value type (64-bit StrongId). Zero dynamic allocations.
///
/// Thread Safety:
/// Value types; fully thread-safe for concurrent read access.

#pragma once

#include "core/id.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace eng::render {

struct AssetTag {};

/// @brief Strongly typed 64-bit identifier uniquely referencing an asset across sessions.
using AssetId = eng::StrongId<AssetTag, uint64_t>;

/// @enum AssetType
/// @brief Primary resource classification for engine and editor assets.
enum class AssetType : uint8_t {
    Unknown = 0,
    Mesh = 1,
    Material = 2,
    Texture = 3,
    Scene = 4,
    Audio = 5,
};

/// @enum AssetLoadState
/// @brief Runtime lifecycle status of an asset.
enum class AssetLoadState : uint8_t {
    Unloaded = 0,
    Loaded = 1,
    Failed = 2,
};

/// @struct AssetMetadata
/// @brief Descriptive metadata tracking an asset's source, size, and operational status.
struct AssetMetadata {
    AssetId id{0};
    AssetType type{AssetType::Unknown};
    std::string path{};
    std::string sourcePath{};
    size_t fileSize{0};
    AssetLoadState loadState{AssetLoadState::Unloaded};
    std::string errorMessage{};
};

/// Computes a stable, deterministic 64-bit FNV-1a hash of a project-relative file path.
/// Normalizes path separators ('\\' to '/') so paths match across platforms.
[[nodiscard]] constexpr uint64_t fnv1a64Path(std::string_view path) noexcept {
    constexpr uint64_t kFnv1a64Basis = 14695981039346656037ULL;
    constexpr uint64_t kFnv1a64Prime = 1099511628211ULL;

    if (path.size() >= 2 && path[0] == '.' && (path[1] == '/' || path[1] == '\\')) {
        path.remove_prefix(2);
    }

    uint64_t hash = kFnv1a64Basis;
    for (char c : path) {
        char normC = (c == '\\') ? '/' : c;
        hash ^= static_cast<uint8_t>(normC);
        hash *= kFnv1a64Prime;
    }
    return (hash == 0) ? 1ULL : hash;
}

/// Helper function creating an AssetId from a file path.
[[nodiscard]] constexpr AssetId makeAssetIdFromPath(std::string_view path) noexcept {
    return AssetId{fnv1a64Path(path)};
}

} // namespace eng::render
