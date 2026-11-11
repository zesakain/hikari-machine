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
#include "render/gpu_material.h"
#include "render/gpu_mesh.h"
#include "render/gpu_texture.h"
#include "render/image.h"
#include "render/material.h"
#include "render/mesh.h"
#include "rhi/rhi_device.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eng::render {

/// @enum DefaultTextureType
/// @brief Identifiers for singleton fallback GPU textures.
enum class DefaultTextureType {
    WhiteSRGB,
    WhiteLinear,
    BlackSRGB,
    FlatNormalLinear,
};

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

    /// @brief Loads an image texture asset from a relative or absolute file path.
    /// Deduplicates if the asset is already loaded or registered.
    ///
    /// @param path Relative path or absolute path to the image file.
    /// @param srgb Whether the texture format should be tagged as sRGB.
    /// @return The persistent 64-bit AssetId for this texture.
    Result<AssetId> loadTexture(std::string_view path, bool srgb = true);

    /// @brief Registers an in-memory ImageData with a designated virtual path or name.
    ///
    /// @param path Identifier or virtual path for the texture.
    /// @param image Decoded host bitmap pixel data.
    /// @return The persistent AssetId assigned to this texture.
    Result<AssetId> registerTexture(std::string_view path, ImageData image);

    /// @brief Uploads a loaded CPU image to the GPU if not already uploaded.
    ///
    /// @param id Asset identifier of the texture.
    /// @param device Active RhiDevice for texture and sampler allocation.
    /// @param generateMips Whether to generate complete mipmap chain.
    /// @param samplerDesc Optional custom sampler configuration.
    /// @return Pointer to the GpuTexture on success, or Error on failure.
    Result<GpuTexture*> uploadTexture(AssetId id, rhi::RhiDevice& device, bool generateMips = true,
                                      const rhi::SamplerDesc* samplerDesc = nullptr);

    /// @brief Retrieves the GPU texture for an asset, or nullptr if not uploaded.
    [[nodiscard]] GpuTexture* getGpuTexture(AssetId id) noexcept;
    [[nodiscard]] const GpuTexture* getGpuTexture(AssetId id) const noexcept;

    /// @brief Retrieves or creates a default fallback GPU texture.
    ///
    /// Fallback textures are 1x1 single-color textures used when a material does
    /// not supply an optional texture map:
    /// - WhiteSRGB: (1, 1, 1, 1) sRGB for missing BaseColor
    /// - WhiteLinear: (1, 1, 1, 1) Linear for missing MetallicRoughness or Occlusion
    /// - BlackSRGB: (0, 0, 0, 1) sRGB for missing Emissive
    /// - FlatNormalLinear: (0.5, 0.5, 1.0, 1.0) Linear for missing Normal
    ///
    /// @param device The active RHI device for resource allocation.
    /// @param type The default texture type to retrieve or instantiate.
    /// @return Pointer to valid GpuTexture or error.
    Result<GpuTexture*> getOrCreateDefaultTexture(rhi::RhiDevice& device, DefaultTextureType type);

    /// @brief Uploads or retrieves the GpuMaterial for a specific submesh/material index.
    Result<GpuMaterial*> uploadMaterial(AssetId meshId, size_t materialIndex,
                                        rhi::RhiDevice& device,
                                        rhi::DescriptorSetLayoutHandle materialLayout);

    /// @brief Retrieves the GpuMaterial for a mesh material index, or nullptr if not uploaded.
    [[nodiscard]] GpuMaterial* getGpuMaterial(AssetId meshId, size_t materialIndex) noexcept;
    [[nodiscard]] const GpuMaterial* getGpuMaterial(AssetId meshId,
                                                    size_t materialIndex) const noexcept;

    /// @brief Retrieves or creates a default fallback GpuMaterial.
    Result<GpuMaterial*> getOrCreateDefaultMaterial(rhi::RhiDevice& device,
                                                    rhi::DescriptorSetLayoutHandle materialLayout);

    /// @brief Retrieves CPU image data for an asset, or nullptr if not loaded.
    [[nodiscard]] const ImageData* getImageData(AssetId id) const noexcept;
    [[nodiscard]] const ImageData* getImageData(std::string_view path) const noexcept;

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
        std::vector<std::unique_ptr<GpuMaterial>> gpuMaterials{};
    };

    struct TextureEntry {
        AssetMetadata metadata{};
        ImageData cpuImage{};
        std::unique_ptr<GpuTexture> gpuTexture{nullptr};
    };

    std::unordered_map<AssetId, MeshEntry> meshEntries_{};
    std::unordered_map<AssetId, TextureEntry> textureEntries_{};
    std::unordered_map<std::string, AssetId> pathToId_{};
    std::unordered_map<DefaultTextureType, std::unique_ptr<GpuTexture>> defaultTextures_{};
    std::unique_ptr<GpuMaterial> defaultMaterial_{nullptr};
};

} // namespace eng::render
