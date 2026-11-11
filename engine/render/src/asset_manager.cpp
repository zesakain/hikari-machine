/// @file asset_manager.cpp
/// @brief Asset repository and cache implementation.

#include "render/asset_manager.h"

#include "core/log.h"
#include "gltf_internal.h"
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

Result<GpuMaterial*> AssetManager::uploadMaterial(AssetId meshId, size_t materialIndex,
                                                  rhi::RhiDevice& device,
                                                  rhi::DescriptorSetLayoutHandle materialLayout) {
    auto it = meshEntries_.find(meshId);
    if (it == meshEntries_.end() || it->second.metadata.loadState != AssetLoadState::Loaded) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Mesh asset not loaded in AssetManager"};
    }

    auto& entry = it->second;
    if (entry.gpuMaterials.empty()) {
        entry.gpuMaterials.resize(entry.cpuMesh.materials.size());
    }

    if (materialIndex >= entry.cpuMesh.materials.size()) {
        return getOrCreateDefaultMaterial(device, materialLayout);
    }

    if (entry.gpuMaterials[materialIndex] != nullptr &&
        entry.gpuMaterials[materialIndex]->isValid()) {
        return Result<GpuMaterial*>::success(entry.gpuMaterials[materialIndex].get());
    }

    auto createRes =
        GpuMaterial::create(device, materialLayout, entry.cpuMesh.materials[materialIndex], *this);
    if (!createRes.hasValue()) {
        return createRes.error();
    }

    entry.gpuMaterials[materialIndex] = std::move(createRes.value());
    return Result<GpuMaterial*>::success(entry.gpuMaterials[materialIndex].get());
}

GpuMaterial* AssetManager::getGpuMaterial(AssetId meshId, size_t materialIndex) noexcept {
    auto it = meshEntries_.find(meshId);
    if (it != meshEntries_.end()) {
        if (materialIndex < it->second.gpuMaterials.size() &&
            it->second.gpuMaterials[materialIndex] != nullptr) {
            return it->second.gpuMaterials[materialIndex].get();
        }
    }
    return nullptr;
}

const GpuMaterial* AssetManager::getGpuMaterial(AssetId meshId,
                                                size_t materialIndex) const noexcept {
    auto it = meshEntries_.find(meshId);
    if (it != meshEntries_.end()) {
        if (materialIndex < it->second.gpuMaterials.size() &&
            it->second.gpuMaterials[materialIndex] != nullptr) {
            return it->second.gpuMaterials[materialIndex].get();
        }
    }
    return nullptr;
}

Result<GpuMaterial*>
AssetManager::getOrCreateDefaultMaterial(rhi::RhiDevice& device,
                                         rhi::DescriptorSetLayoutHandle materialLayout) {
    if (defaultMaterial_ != nullptr && defaultMaterial_->isValid()) {
        return Result<GpuMaterial*>::success(defaultMaterial_.get());
    }

    Material defaultMat = Material::createDefault();
    auto createRes = GpuMaterial::create(device, materialLayout, defaultMat, *this);
    if (!createRes.hasValue()) {
        return createRes.error();
    }

    defaultMaterial_ = std::move(createRes.value());
    return Result<GpuMaterial*>::success(defaultMaterial_.get());
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
    const std::string colorSuffix = (srgb ? "#srgb" : "#linear");
    const std::string cacheKey = (normPath.ends_with("#srgb") || normPath.ends_with("#linear"))
                                     ? normPath
                                     : (normPath + colorSuffix);

    auto pathIt = pathToId_.find(cacheKey);
    if (pathIt != pathToId_.end()) {
        auto entryIt = textureEntries_.find(pathIt->second);
        if (entryIt != textureEntries_.end() &&
            entryIt->second.metadata.loadState == AssetLoadState::Loaded) {
            return Result<AssetId>::success(pathIt->second);
        }
    }

    Result<ImageData> decodeRes = Result<ImageData>::failure(Error(ErrorCode::EntityNotFound, ""));
    const auto hashImg = normPath.find("#image_");
    if (hashImg != std::string::npos) {
        std::string baseFile = normPath.substr(0, hashImg);
        std::string sub = normPath.substr(hashImg + 7);
        size_t endNum = sub.find('#');
        size_t imgIdx = std::stoul(sub.substr(0, endNum));
        decodeRes = detail::extractEmbeddedImageFromFile(baseFile, imgIdx, srgb);
    } else {
        std::string filePath = normPath;
        if (filePath.ends_with("#srgb")) {
            filePath = filePath.substr(0, filePath.size() - 5);
        } else if (filePath.ends_with("#linear")) {
            filePath = filePath.substr(0, filePath.size() - 7);
        }
        decodeRes = ImageDecoder::decodeFromFile(filePath, 4, srgb);
    }

    if (!decodeRes.hasValue()) {
        ENG_LOG_WARN("AssetManager", "Failed to decode image from path");
        return decodeRes.error();
    }

    const AssetId id = makeAssetIdFromPath(cacheKey);

    TextureEntry entry{};
    entry.metadata.id = id;
    entry.metadata.path = cacheKey;
    entry.metadata.type = AssetType::Texture;
    entry.metadata.loadState = AssetLoadState::Loaded;
    entry.metadata.fileSize = decodeRes.value().sizeInBytes();
    entry.cpuImage = std::move(decodeRes.value());
    entry.gpuTexture = nullptr;

    pathToId_[cacheKey] = id;
    if (pathToId_.find(normPath) == pathToId_.end()) {
        pathToId_[normPath] = id;
    }
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
    const bool isSrgb = (image.format == rhi::TextureFormat::RGBA8_SRGB);
    const std::string colorSuffix = (isSrgb ? "#srgb" : "#linear");
    const std::string cacheKey = (normPath.ends_with("#srgb") || normPath.ends_with("#linear"))
                                     ? normPath
                                     : (normPath + colorSuffix);

    const AssetId id = makeAssetIdFromPath(cacheKey);

    TextureEntry entry{};
    entry.metadata.id = id;
    entry.metadata.path = cacheKey;
    entry.metadata.type = AssetType::Texture;
    entry.metadata.loadState = AssetLoadState::Loaded;
    entry.metadata.fileSize = image.sizeInBytes();
    entry.cpuImage = std::move(image);
    entry.gpuTexture = nullptr;

    pathToId_[cacheKey] = id;
    if (pathToId_.find(normPath) == pathToId_.end()) {
        pathToId_[normPath] = id;
    }
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

Result<GpuTexture*> AssetManager::getOrCreateDefaultTexture(rhi::RhiDevice& device,
                                                            DefaultTextureType type) {
    auto it = defaultTextures_.find(type);
    if (it != defaultTextures_.end() && it->second != nullptr && it->second->isValid()) {
        return Result<GpuTexture*>::success(it->second.get());
    }

    ImageData img{};
    img.width = 1;
    img.height = 1;
    img.channels = 4;

    switch (type) {
    case DefaultTextureType::WhiteSRGB:
        img.format = rhi::TextureFormat::RGBA8_SRGB;
        img.pixels = {255, 255, 255, 255};
        break;
    case DefaultTextureType::WhiteLinear:
        img.format = rhi::TextureFormat::RGBA8_UNORM;
        img.pixels = {255, 255, 255, 255};
        break;
    case DefaultTextureType::BlackSRGB:
        img.format = rhi::TextureFormat::RGBA8_SRGB;
        img.pixels = {0, 0, 0, 255};
        break;
    case DefaultTextureType::FlatNormalLinear:
        img.format = rhi::TextureFormat::RGBA8_UNORM;
        img.pixels = {128, 128, 255, 255};
        break;
    }

    auto createRes = GpuTexture::create(device, img, false);
    if (!createRes.hasValue()) {
        return createRes.error();
    }

    defaultTextures_[type] = std::move(createRes.value());
    return Result<GpuTexture*>::success(defaultTextures_[type].get());
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
    auto itSrgb = pathToId_.find(normPath + "#srgb");
    if (itSrgb != pathToId_.end()) {
        return getImageData(itSrgb->second);
    }
    auto itLinear = pathToId_.find(normPath + "#linear");
    if (itLinear != pathToId_.end()) {
        return getImageData(itLinear->second);
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
    auto itSrgb = pathToId_.find(normPath + "#srgb");
    if (itSrgb != pathToId_.end()) {
        return itSrgb->second;
    }
    auto itLinear = pathToId_.find(normPath + "#linear");
    if (itLinear != pathToId_.end()) {
        return itLinear->second;
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
        const std::string& path = itTex->second.metadata.path;
        pathToId_.erase(path);
        if (path.ends_with("#srgb")) {
            auto plain = path.substr(0, path.size() - 5);
            auto pIt = pathToId_.find(plain);
            if (pIt != pathToId_.end() && pIt->second == id) {
                pathToId_.erase(pIt);
            }
        } else if (path.ends_with("#linear")) {
            auto plain = path.substr(0, path.size() - 7);
            auto pIt = pathToId_.find(plain);
            if (pIt != pathToId_.end() && pIt->second == id) {
                pathToId_.erase(pIt);
            }
        }
        textureEntries_.erase(itTex);
    }
}

void AssetManager::unloadAll() noexcept {
    defaultMaterial_.reset();
    defaultTextures_.clear();
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
