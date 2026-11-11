/// @file asset_manager.cpp
/// @brief Asset repository and cache implementation.

#include "render/asset_manager.h"

#include "core/log.h"
#include "render/gltf_importer.h"
#include "render/gltf_scene_importer.h"
#include "render/image.h"
#include "render/obj_importer.h"
#include "world/world.h"

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

    const auto hashPos = normPath.find('#');
    if (hashPos != std::string::npos) {
        std::string baseFile = normPath.substr(0, hashPos);
        world::World dummyWorld;
        auto importRes = GltfSceneImporter::importScene(baseFile, dummyWorld, this);
        if (importRes.success) {
            auto it = pathToId_.find(normPath);
            if (it != pathToId_.end()) {
                return Result<AssetId>::success(it->second);
            }
        }
        return Result<AssetId>::failure(
            Error(ErrorCode::InvalidArgument, "Mesh URI fragment not found in scene: " + normPath));
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

Result<AssetId> AssetManager::loadTexture(std::string_view path, bool srgb) {
    const std::string normPath = normalizePath(path);

    auto pathIt = pathToId_.find(normPath);
    if (pathIt != pathToId_.end()) {
        auto entryIt = textureEntries_.find(pathIt->second);
        if (entryIt != textureEntries_.end() &&
            entryIt->second.metadata.loadState == AssetLoadState::Loaded) {
            return Result<AssetId>::success(pathIt->second);
        }
    }

    auto decodeRes = ImageDecoder::decodeFromFile(normPath, 4, srgb);
    if (!decodeRes.hasValue()) {
        ENG_LOG_WARN("AssetManager", "Failed to decode image from path");
        return decodeRes.error();
    }

    const AssetId id = makeAssetIdFromPath(normPath);

    TextureEntry entry{};
    entry.metadata.id = id;
    entry.metadata.path = normPath;
    entry.metadata.type = AssetType::Texture;
    entry.metadata.loadState = AssetLoadState::Loaded;
    entry.metadata.fileSize = decodeRes.value().sizeInBytes();
    entry.cpuImage = std::move(decodeRes.value());
    entry.gpuTexture = nullptr;

    pathToId_[normPath] = id;
    textureEntries_[id] = std::move(entry);

    ENG_LOG_INFO("AssetManager", "Loaded texture asset successfully");
    return Result<AssetId>::success(id);
}

Result<AssetId> AssetManager::registerTexture(std::string_view path, ImageData image) {
    if (!image.isValid()) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Cannot register invalid ImageData"};
    }

    const std::string normPath = normalizePath(path);
    const AssetId id = makeAssetIdFromPath(normPath);

    TextureEntry entry{};
    entry.metadata.id = id;
    entry.metadata.path = normPath;
    entry.metadata.type = AssetType::Texture;
    entry.metadata.loadState = AssetLoadState::Loaded;
    entry.metadata.fileSize = image.sizeInBytes();
    entry.cpuImage = std::move(image);
    entry.gpuTexture = nullptr;

    pathToId_[normPath] = id;
    textureEntries_[id] = std::move(entry);

    return Result<AssetId>::success(id);
}

Result<GpuTexture*> AssetManager::uploadTexture(AssetId id, rhi::RhiDevice& device,
                                                bool generateMips,
                                                const rhi::SamplerDesc* samplerDesc) {
    auto it = textureEntries_.find(id);
    if (it == textureEntries_.end()) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Texture asset not found in AssetManager"};
    }

    if (it->second.gpuTexture != nullptr) {
        return Result<GpuTexture*>::success(it->second.gpuTexture.get());
    }

    auto gpuRes = GpuTexture::create(device, it->second.cpuImage, generateMips, samplerDesc);
    if (!gpuRes.hasValue()) {
        ENG_LOG_ERROR("AssetManager", "Failed to create GPU texture for asset");
        return gpuRes.error();
    }

    it->second.gpuTexture = std::move(gpuRes.value());
    return Result<GpuTexture*>::success(it->second.gpuTexture.get());
}

GpuTexture* AssetManager::getGpuTexture(AssetId id) noexcept {
    auto it = textureEntries_.find(id);
    if (it != textureEntries_.end()) {
        return it->second.gpuTexture.get();
    }
    return nullptr;
}

const GpuTexture* AssetManager::getGpuTexture(AssetId id) const noexcept {
    auto it = textureEntries_.find(id);
    if (it != textureEntries_.end()) {
        return it->second.gpuTexture.get();
    }
    return nullptr;
}

const ImageData* AssetManager::getImageData(AssetId id) const noexcept {
    auto it = textureEntries_.find(id);
    if (it != textureEntries_.end() && it->second.metadata.loadState == AssetLoadState::Loaded) {
        return &it->second.cpuImage;
    }
    return nullptr;
}

const ImageData* AssetManager::getImageData(std::string_view path) const noexcept {
    const std::string normPath = normalizePath(path);
    auto it = pathToId_.find(normPath);
    if (it != pathToId_.end()) {
        return getImageData(it->second);
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
    auto itTex = textureEntries_.find(id);
    if (itTex != textureEntries_.end()) {
        return &itTex->second.metadata;
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
    if (it != meshEntries_.end() && it->second.metadata.loadState == AssetLoadState::Loaded) {
        return true;
    }
    auto itTex = textureEntries_.find(id);
    return itTex != textureEntries_.end() &&
           itTex->second.metadata.loadState == AssetLoadState::Loaded;
}

void AssetManager::unload(AssetId id) noexcept {
    auto it = meshEntries_.find(id);
    if (it != meshEntries_.end()) {
        pathToId_.erase(it->second.metadata.path);
        meshEntries_.erase(it);
        return;
    }
    auto itTex = textureEntries_.find(id);
    if (itTex != textureEntries_.end()) {
        pathToId_.erase(itTex->second.metadata.path);
        textureEntries_.erase(itTex);
    }
}

void AssetManager::unloadAll() noexcept {
    pathToId_.clear();
    meshEntries_.clear();
    textureEntries_.clear();
}

std::vector<AssetMetadata> AssetManager::getAllMetadata() const {
    std::vector<AssetMetadata> result;
    result.reserve(meshEntries_.size() + textureEntries_.size());
    for (const auto& [id, entry] : meshEntries_) {
        result.push_back(entry.metadata);
    }
    for (const auto& [id, entry] : textureEntries_) {
        result.push_back(entry.metadata);
    }
    return result;
}

} // namespace eng::render
