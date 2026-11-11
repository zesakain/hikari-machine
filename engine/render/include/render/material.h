/// @file material.h
/// @brief Material definition, PBR parameters, and texture references.
///
/// Purpose:
/// Defines surface properties (base color, roughness, metallic, emissive, alpha cutoff)
/// and texture references with color-space metadata for mesh rendering.
///
/// Ownership and Lifetime:
/// Value type. Managed as an asset or embedded in mesh rendering descriptors.
///
/// Thread-safety:
/// Value type; const access is thread-safe.

#pragma once

#include "core/math.h"
#include "render/asset_types.h"

#include <cstdint>
#include <string>

namespace eng::render {

/// @enum AlphaMode
/// @brief Alpha rendering mode corresponding to glTF 2.0 specifications.
enum class AlphaMode : uint8_t {
    Opaque = 0, ///< Fully opaque surface; alpha channel ignored during rasterization.
    Mask = 1,   ///< Binary cut-out; fragments with alpha < alphaCutoff are discarded.
    Blend = 2,  ///< Alpha blending. NOTE: Currently unsupported pending transparent mesh sorting.
};

/// @enum TextureColorSpace
/// @brief Color space requirements for texture samplers.
enum class TextureColorSpace : uint8_t {
    Linear = 0,
    sRGB = 1,
};

/// @struct TextureSamplerDesc
/// @brief Sampler filtering and wrap configuration.
struct TextureSamplerDesc {
    enum class Filter : uint8_t { Nearest = 0, Linear = 1 };
    enum class WrapMode : uint8_t { Repeat = 0, ClampToEdge = 1, MirroredRepeat = 2 };

    Filter minFilter{Filter::Linear};
    Filter magFilter{Filter::Linear};
    Filter mipmapMode{Filter::Linear};
    WrapMode wrapS{WrapMode::Repeat};
    WrapMode wrapT{WrapMode::Repeat};
};

/// @struct TextureInfo
/// @brief Texture binding metadata and URI reference.
struct TextureInfo {
    std::string uri{};
    AssetId textureAsset{};
    TextureColorSpace colorSpace{TextureColorSpace::Linear};
    TextureSamplerDesc sampler{};
    int texCoordIndex{0};
    float scale{1.0F};    ///< Scale factor for normal texture
    float strength{1.0F}; ///< Strength factor for occlusion texture
    bool isPresent{false};
};

/// @struct Material
/// @brief Surface shading properties supporting standard glTF 2.0 PBR metallic-roughness workflow.
struct Material {
    std::string name{"DefaultMaterial"};
    Vec4 baseColor{1.0F, 1.0F, 1.0F, 1.0F}; // baseColorFactor
    float roughness{0.5F};                  // roughnessFactor
    float metallic{0.0F};                   // metallicFactor
    Vec3 emissiveFactor{0.0F, 0.0F, 0.0F};
    float alphaCutoff{0.5F};
    AlphaMode alphaMode{AlphaMode::Opaque};
    bool doubleSided{false};

    // Textures with color space metadata
    TextureInfo baseColorTexture{};         // sRGB
    TextureInfo metallicRoughnessTexture{}; // Linear
    TextureInfo normalTexture{};            // Linear
    TextureInfo occlusionTexture{};         // Linear
    TextureInfo emissiveTexture{};          // sRGB

    // Backward-compatibility alias
    AssetId textureAsset{}; // references primary baseColorTexture

    [[nodiscard]] static Material createDefault() noexcept {
        return Material{
            .name = "DefaultMaterial",
            .baseColor = {0.8F, 0.8F, 0.82F, 1.0F},
            .roughness = 0.5F,
            .metallic = 0.0F,
            .emissiveFactor = {0.0F, 0.0F, 0.0F},
            .alphaCutoff = 0.5F,
            .alphaMode = AlphaMode::Opaque,
            .doubleSided = false,
            .baseColorTexture = {},
            .metallicRoughnessTexture = {},
            .normalTexture = {},
            .occlusionTexture = {},
            .emissiveTexture = {},
            .textureAsset = {},
        };
    }
};

} // namespace eng::render
