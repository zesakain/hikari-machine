/// @file asset_manager.h
/// @brief Central asset repository and cache for mesh, material, and GPU resources.
///
/// Purpose:
/// Loads, deduplicates, caches, and provides handle-based access to engine assets
/// (meshes, materials, GPU buffers). Shared across edit and runtime simulation worlds.
///
/// Ownership and Lifetime:
/// Owned by EditorEngine or application root. Outlives active and standby simulation worlds.
///
/// Thread-safety:
/// Read methods are const; mutation (load/unload/upload) must be serialized on the main thread.
///
/// Performance:
/// Cached lookups via 64-bit AssetId are O(1) hash map operations with zero allocation.

#pragma once

#include "core/error.h"
#include "core/result.h"
#include "render/asset_types.h"
#include "render/gpu_mesh.h"
#include "render/material.h"
#include "render/mesh.h"
#include "rhi/rhi_device.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eng::render {

/// @class AssetManager
/// @brief Manages loading, caching, and lifecycle of engine assets.
class AssetManager {
public:
    AssetManager() = default;
    ~AssetManager() noexcept;

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;
    AssetManager(AssetManager&&) noexcept = default;
    AssetManager& operator=(AssetManager&&) noexcept = default;

    /// @brief Loads a 3D mesh asset from a relative or absolute file path.
    /// Deduplicates if the asset is already loaded or registered.
    ///
    /// @param path Relative path (e.g. "assets/models/cube.obj") or absolute path.
    /// @return The persistent 64-bit AssetId for this mesh.
    Result<AssetId> loadMesh(std::string_view path);

    /// @brief Registers an in-memory MeshData with a designated virtual path or name.
    ///
    /// @param path Identifier or path for the mesh.
    /// @param mesh Data describing vertices, indices, and submeshes.
    /// @return The persistent AssetId assigned to this mesh.
    Result<AssetId> registerMesh(std::string_view path, MeshData mesh);

    /// @brief Uploads a loaded CPU mesh to the GPU if not already uploaded.
    ///
    /// @param id Asset identifier of the mesh.
    /// @param device Active RhiDevice for buffer allocation.
    /// @return Pointer to the GpuMesh on success, or Error on failure.
    Result<GpuMesh*> uploadMesh(AssetId id, rhi::RhiDevice& device);

    /// @brief Retrieves the GPU mesh for an asset, or nullptr if not uploaded.
    [[nodiscard]] GpuMesh* getGpuMesh(AssetId id) noexcept;
    [[nodiscard]] const GpuMesh* getGpuMesh(AssetId id) const noexcept;

    /// @brief Retrieves CPU mesh data for an asset, or nullptr if not loaded.
    [[nodiscard]] const MeshData* getMeshData(AssetId id) const noexcept;
    [[nodiscard]] const MeshData* getMeshData(std::string_view path) const noexcept;

    /// @brief Retrieves materials associated with a loaded mesh, or nullptr if not loaded.
    [[nodiscard]] const std::vector<Material>* getMaterials(AssetId id) const noexcept;

    /// @brief Retrieves metadata for an asset by ID, or nullptr if not registered.
    [[nodiscard]] const AssetMetadata* getMetadata(AssetId id) const noexcept;

    /// @brief Finds the AssetId for a given path, or an invalid handle if not registered.
    [[nodiscard]] AssetId findAssetByPath(std::string_view path) const noexcept;

    /// @brief Checks whether the specified asset is loaded in CPU memory.
    [[nodiscard]] bool isLoaded(AssetId id) const noexcept;

    /// @brief Unloads CPU and GPU data for a specific asset.
    void unload(AssetId id) noexcept;

    /// @brief Unloads all CPU and GPU resources.
    void unloadAll() noexcept;

    /// @brief Returns a copy of all registered asset metadata records.
    [[nodiscard]] std::vector<AssetMetadata> getAllMetadata() const;

private:
    struct MeshEntry {
        AssetMetadata metadata{};
        MeshData cpuMesh{};
        std::unique_ptr<GpuMesh> gpuMesh{nullptr};
    };

    std::unordered_map<AssetId, MeshEntry> meshEntries_{};
    std::unordered_map<std::string, AssetId> pathToId_{};
};

} // namespace eng::render
