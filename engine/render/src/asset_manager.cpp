/// @file asset_manager.cpp
/// @brief Asset repository and cache implementation.

#include "render/asset_manager.h"

#include "core/log.h"
#include "render/gltf_importer.h"
#include "render/obj_importer.h"

#include <algorithm>
#include <cctype>

namespace eng::render {

namespace {

std::string normalizePath(std::string_view path) {
    std::string result(path);
    std::replace(result.begin(), result.end(), '\\', '/');
    if (result.rfind("./", 0) == 0) {
        result = result.substr(2);
    }
    return result;
}

} // namespace

AssetManager::~AssetManager() noexcept {
    unloadAll();
}

Result<AssetId> AssetManager::loadMesh(std::string_view path) {
    const std::string normPath = normalizePath(path);

    auto pathIt = pathToId_.find(normPath);
    if (pathIt != pathToId_.end()) {
        auto entryIt = meshEntries_.find(pathIt->second);
        if (entryIt != meshEntries_.end() &&
            entryIt->second.metadata.loadState == AssetLoadState::Loaded) {
            return Result<AssetId>::success(pathIt->second);
        }
    }

    std::string ext{};
    const auto dotPos = normPath.rfind('.');
    if (dotPos != std::string::npos) {
        ext = normPath.substr(dotPos);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }

    Result<MeshData> importRes = Result<MeshData>::failure(
        Error(ErrorCode::InvalidArgument, "Unsupported 3D asset file extension"));

    if (ext == ".gltf" || ext == ".glb") {
        importRes = GltfImporter::importFromFile(normPath);
    } else if (ext == ".obj") {
        importRes = ObjImporter::importFromFile(normPath);
    } else {
        // Fallback: try glTF then OBJ
        importRes = GltfImporter::importFromFile(normPath);
        if (!importRes.hasValue()) {
            importRes = ObjImporter::importFromFile(normPath);
        }
    }

    if (!importRes.hasValue()) {
        ENG_LOG_WARN("AssetManager", "Failed to load mesh from path");
        return importRes.error();
    }

    const AssetId id = makeAssetIdFromPath(normPath);

    MeshEntry entry{};
    entry.metadata.id = id;
    entry.metadata.path = normPath;
    entry.metadata.type = AssetType::Mesh;
    entry.metadata.loadState = AssetLoadState::Loaded;
    entry.cpuMesh = std::move(importRes.value());
    entry.gpuMesh = nullptr;

    pathToId_[normPath] = id;
    meshEntries_[id] = std::move(entry);

    ENG_LOG_INFO("AssetManager", "Loaded mesh asset successfully");

    return Result<AssetId>::success(id);
}

Result<AssetId> AssetManager::registerMesh(std::string_view path, MeshData mesh) {
    auto valRes = mesh.validate();
    if (!valRes.hasValue()) {
        return valRes.error();
    }

    const std::string normPath = normalizePath(path);
    const AssetId id = makeAssetIdFromPath(normPath);

    MeshEntry entry{};
    entry.metadata.id = id;
    entry.metadata.path = normPath;
    entry.metadata.type = AssetType::Mesh;
    entry.metadata.loadState = AssetLoadState::Loaded;
    entry.cpuMesh = std::move(mesh);
    entry.gpuMesh = nullptr;

    pathToId_[normPath] = id;
    meshEntries_[id] = std::move(entry);

    return Result<AssetId>::success(id);
}

Result<GpuMesh*> AssetManager::uploadMesh(AssetId id, rhi::RhiDevice& device) {
    auto it = meshEntries_.find(id);
    if (it == meshEntries_.end()) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Asset not found in AssetManager"};
    }

    if (it->second.gpuMesh != nullptr) {
        return Result<GpuMesh*>::success(it->second.gpuMesh.get());
    }

    auto gpuRes = GpuMesh::create(device, it->second.cpuMesh);
    if (!gpuRes.hasValue()) {
        ENG_LOG_ERROR("AssetManager", "Failed to create GPU mesh for asset");
        return gpuRes.error();
    }

    it->second.gpuMesh = std::move(gpuRes.value());
    return Result<GpuMesh*>::success(it->second.gpuMesh.get());
}

GpuMesh* AssetManager::getGpuMesh(AssetId id) noexcept {
    auto it = meshEntries_.find(id);
    if (it != meshEntries_.end()) {
        return it->second.gpuMesh.get();
    }
    return nullptr;
}

const GpuMesh* AssetManager::getGpuMesh(AssetId id) const noexcept {
    auto it = meshEntries_.find(id);
    if (it != meshEntries_.end()) {
        return it->second.gpuMesh.get();
    }
    return nullptr;
}

const MeshData* AssetManager::getMeshData(AssetId id) const noexcept {
    auto it = meshEntries_.find(id);
    if (it != meshEntries_.end() && it->second.metadata.loadState == AssetLoadState::Loaded) {
        return &it->second.cpuMesh;
    }
    return nullptr;
}

const MeshData* AssetManager::getMeshData(std::string_view path) const noexcept {
    const std::string normPath = normalizePath(path);
    auto it = pathToId_.find(normPath);
    if (it != pathToId_.end()) {
        return getMeshData(it->second);
    }
    return nullptr;
}

const std::vector<Material>* AssetManager::getMaterials(AssetId id) const noexcept {
    auto it = meshEntries_.find(id);
    if (it != meshEntries_.end() && it->second.metadata.loadState == AssetLoadState::Loaded) {
        return &it->second.cpuMesh.materials;
    }
    return nullptr;
}

const AssetMetadata* AssetManager::getMetadata(AssetId id) const noexcept {
    auto it = meshEntries_.find(id);
    if (it != meshEntries_.end()) {
        return &it->second.metadata;
    }
    return nullptr;
}

AssetId AssetManager::findAssetByPath(std::string_view path) const noexcept {
    const std::string normPath = normalizePath(path);
    auto it = pathToId_.find(normPath);
    if (it != pathToId_.end()) {
        return it->second;
    }
    return {};
}

bool AssetManager::isLoaded(AssetId id) const noexcept {
    auto it = meshEntries_.find(id);
    return it != meshEntries_.end() && it->second.metadata.loadState == AssetLoadState::Loaded;
}

void AssetManager::unload(AssetId id) noexcept {
    auto it = meshEntries_.find(id);
    if (it != meshEntries_.end()) {
        pathToId_.erase(it->second.metadata.path);
        meshEntries_.erase(it);
    }
}

void AssetManager::unloadAll() noexcept {
    pathToId_.clear();
    meshEntries_.clear();
}

std::vector<AssetMetadata> AssetManager::getAllMetadata() const {
    std::vector<AssetMetadata> result;
    result.reserve(meshEntries_.size());
    for (const auto& [id, entry] : meshEntries_) {
        result.push_back(entry.metadata);
    }
    return result;
}

} // namespace eng::render
