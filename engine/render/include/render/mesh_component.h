/// @file mesh_component.h
/// @brief ECS component for 3D mesh representation and rendering.
///
/// Purpose:
/// Binds an entity to a 3D mesh asset, submesh index, and material.
///
/// Ownership and Lifetime:
/// Value type stored in Flecs ECS archetypes.
/// References assets by AssetId and project-relative path string.
/// Contains no raw GPU pointers or device-allocated buffers.
///
/// Thread-safety:
/// Read during render graph recording. Modified during simulation or editor interaction.

#pragma once

#include "render/asset_types.h"
#include "world/world.h"

#include <cstdint>
#include <string>

namespace eng::render {

/// @struct MeshComponent
/// @brief Flecs ECS component specifying mesh geometry and rendering parameters.
struct MeshComponent {
    AssetId meshAsset{};
    std::string assetPath{};
    uint32_t submeshIndex{0};
    AssetId materialAsset{};
    bool visible{true};
    bool castShadows{true};
};

/// @brief Registers render-related ECS components in the Flecs World.
void registerRenderComponents(world::World& world);

} // namespace eng::render
