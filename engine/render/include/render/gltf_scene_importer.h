/// @file gltf_scene_importer.h
/// @brief Parser and entity graph constructor for hierarchical glTF 2.0 scenes.
///
/// Purpose:
/// Ingests glTF 2.0 (.gltf, .glb) scenes as structured Flecs ECS entity hierarchies, preserving
/// local TRS transforms, empty locator/pivot nodes, and parent-child relationships without
/// vertex flattening. Deduplicates shared mesh geometry through AssetManager URIs.
///
/// Contract:
/// - filePath must point to a readable, valid glTF 2.0 or GLB asset.
/// - Does not flatten or bake parent transforms into child vertex coordinates.
/// - Parenting uses world.setParent(child, parent, false) because local transforms are set
///   directly from glTF node data.
/// - Meshes with multiple primitives spawn child primitive entities named
/// "{NodeName}_Primitive_{i}".
///
/// Ownership and Lifetime:
/// Returns a value-type GltfSceneImportResult containing EntityHandles to entities created in the
/// supplied World. The caller owns the World and controls the lifecycle of created entities.
///
/// Thread Safety:
/// Not thread-safe for the same World instance. Caller must ensure exclusive access to the World
/// during import.
///
/// Cost:
/// Allocates during scene graph parsing and asset registration; forbidden in the simulation hot
/// path.
///
/// Errors:
/// On error, returns GltfSceneImportResult with success == false and errorMessage set.

#pragma once

#include "world/entity.h"

#include <cstddef>
#include <string>
#include <vector>

namespace eng::world {
class World;
} // namespace eng::world

namespace eng::render {

class AssetManager;

/// @struct GltfSceneNode
/// @brief Describes one entity created during hierarchical glTF scene import.
///
/// Each imported glTF node corresponds to a GltfSceneNode entry in GltfSceneImportResult::nodes.
/// Consumers inspect hasMesh to distinguish locator/pivot entities from renderable mesh entities.
struct GltfSceneNode {
    std::string name;           ///< Name from the glTF node or auto-generated fallback.
    world::EntityHandle entity; ///< Live Flecs entity handle created in World.
    bool hasMesh{false};        ///< True if the entity carries a MeshComponent.
};

/// @struct GltfSceneImportResult
/// @brief Aggregate result returned by GltfSceneImporter::importScene().
///
/// On success: success == true, errorMessage is empty, nodes contains all created entities in
/// depth-first traversal order, rootEntity references the first root-level entity, and nodeCount
/// equals nodes.size().
/// On failure: success == false and errorMessage describes the error.
struct GltfSceneImportResult {
    bool success{false};
    std::string errorMessage{};         ///< Diagnostic error message on failure.
    std::vector<GltfSceneNode> nodes{}; ///< All created entities in depth-first order.
    world::EntityHandle rootEntity{};   ///< First root-level entity; invalid on failure.
    std::size_t nodeCount{0};           ///< Total number of entities created (equals nodes.size()).
};

/// @brief Alias matching Milestone 8B.6 architecture specification nomenclature.
using GltfSceneTemplate = GltfSceneImportResult;

/// @class GltfSceneImporter
/// @brief Hierarchical glTF 2.0 scene importer mapping node graphs to Flecs entity trees.
class GltfSceneImporter {
public:
    /// @brief Ingests a glTF or GLB file into the supplied World as a hierarchical entity tree.
    ///
    /// For each node in the glTF active scene:
    ///   - Creates a Flecs entity with TransformComponent containing local TRS.
    ///   - Preserves empty locator/pivot nodes without meshes.
    ///   - Connects parent-child relationships via world.setParent(child, parent, false).
    ///   - Deduplicates shared meshes by generating URIs in the form "filepath#mesh_<index>".
    ///   - Spawns "{NodeName}_Primitive_{i}" child entities for multi-primitive meshes.
    ///
    /// @param filePath Path to .gltf or .glb file on the filesystem.
    /// @param world The World in which entities are instantiated.
    /// @param assetManager Optional pointer to AssetManager to register extracted mesh assets.
    /// @return GltfSceneImportResult containing success status, created nodes, and root entity.
    [[nodiscard]] static GltfSceneImportResult
    importScene(const std::string& filePath, world::World& world,
                AssetManager* assetManager = nullptr) noexcept;
};

} // namespace eng::render
