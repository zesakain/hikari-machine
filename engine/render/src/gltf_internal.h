/// @file gltf_internal.h
/// @brief Internal shared parsing helpers for glTF materials, textures, and samplers.
///
/// Private to engine/render; not exposed to public consumers.

#pragma once

#include "core/error.h"
#include "render/material.h"

#ifndef CGLTF_H_INCLUDED__
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include "cgltf/cgltf.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace eng::render::detail {

[[nodiscard]] inline TextureSamplerDesc parseSampler(const cgltf_texture_view& view) noexcept {
    TextureSamplerDesc desc{};
    if (view.texture != nullptr && view.texture->sampler != nullptr) {
        const auto* s = view.texture->sampler;

        if (s->mag_filter == 9728) { // GL_NEAREST
            desc.magFilter = TextureSamplerDesc::Filter::Nearest;
        } else {
            desc.magFilter = TextureSamplerDesc::Filter::Linear;
        }

        switch (s->min_filter) {
        case 9728: // GL_NEAREST
            desc.minFilter = TextureSamplerDesc::Filter::Nearest;
            desc.mipmapMode = TextureSamplerDesc::Filter::Nearest;
            break;
        case 9729: // GL_LINEAR
            desc.minFilter = TextureSamplerDesc::Filter::Linear;
            desc.mipmapMode = TextureSamplerDesc::Filter::Nearest;
            break;
        case 9984: // GL_NEAREST_MIPMAP_NEAREST
            desc.minFilter = TextureSamplerDesc::Filter::Nearest;
            desc.mipmapMode = TextureSamplerDesc::Filter::Nearest;
            break;
        case 9985: // GL_LINEAR_MIPMAP_NEAREST
            desc.minFilter = TextureSamplerDesc::Filter::Linear;
            desc.mipmapMode = TextureSamplerDesc::Filter::Nearest;
            break;
        case 9986: // GL_NEAREST_MIPMAP_LINEAR
            desc.minFilter = TextureSamplerDesc::Filter::Nearest;
            desc.mipmapMode = TextureSamplerDesc::Filter::Linear;
            break;
        case 9987: // GL_LINEAR_MIPMAP_LINEAR
        default:
            desc.minFilter = TextureSamplerDesc::Filter::Linear;
            desc.mipmapMode = TextureSamplerDesc::Filter::Linear;
            break;
        }

        auto convertWrap = [](cgltf_int wrap) noexcept {
            if (wrap == 33071) { // GL_CLAMP_TO_EDGE
                return TextureSamplerDesc::WrapMode::ClampToEdge;
            }
            if (wrap == 33648) { // GL_MIRRORED_REPEAT
                return TextureSamplerDesc::WrapMode::MirroredRepeat;
            }
            return TextureSamplerDesc::WrapMode::Repeat;
        };
        desc.wrapS = convertWrap(s->wrap_s);
        desc.wrapT = convertWrap(s->wrap_t);
    }
    return desc;
}

[[nodiscard]] inline TextureInfo parseTextureInfo(const cgltf_texture_view& view,
                                                  std::string_view baseDir,
                                                  TextureColorSpace colorSpace) noexcept {
    TextureInfo info{};
    if (view.texture == nullptr || view.texture->image == nullptr) {
        return info;
    }

    info.isPresent = true;
    info.colorSpace = colorSpace;
    info.texCoordIndex = view.texcoord;
    info.scale = (view.scale != 0.0F) ? view.scale : 1.0F;
    info.strength = (view.scale != 0.0F) ? view.scale : 1.0F;
    info.sampler = parseSampler(view);

    if (view.texture->image->uri != nullptr) {
        std::string rawUri = view.texture->image->uri;
        if (!baseDir.empty() && rawUri.rfind("data:", 0) != 0) {
            std::filesystem::path fullP = std::filesystem::path(baseDir) / rawUri;
            info.uri = fullP.lexically_normal().string();
        } else {
            info.uri = std::move(rawUri);
        }
        std::replace(info.uri.begin(), info.uri.end(), '\\', '/');
        info.textureAsset = makeAssetIdFromPath(info.uri);
    } else if (view.texture->image->buffer_view != nullptr) {
        info.uri = "embedded://image";
        info.textureAsset = AssetId{1};
    }

    return info;
}

[[nodiscard]] inline Material parseMaterial(const cgltf_material& cgMat, std::string_view baseDir,
                                            size_t index) noexcept {
    Material mat{};
    mat.name = (cgMat.name != nullptr && cgMat.name[0] != '\0')
                   ? std::string(cgMat.name)
                   : ("Material_" + std::to_string(index));
    mat.doubleSided = (cgMat.double_sided != 0);

    if (cgMat.has_pbr_metallic_roughness) {
        const auto& pbr = cgMat.pbr_metallic_roughness;
        mat.baseColor = Vec4{pbr.base_color_factor[0], pbr.base_color_factor[1],
                             pbr.base_color_factor[2], pbr.base_color_factor[3]};
        mat.roughness = pbr.roughness_factor;
        mat.metallic = pbr.metallic_factor;

        mat.baseColorTexture =
            parseTextureInfo(pbr.base_color_texture, baseDir, TextureColorSpace::sRGB);
        mat.textureAsset = mat.baseColorTexture.textureAsset;
        mat.metallicRoughnessTexture =
            parseTextureInfo(pbr.metallic_roughness_texture, baseDir, TextureColorSpace::Linear);
    } else {
        mat.baseColor = Vec4{1.0F, 1.0F, 1.0F, 1.0F};
        mat.roughness = 1.0F;
        mat.metallic = 1.0F;
    }

    mat.normalTexture = parseTextureInfo(cgMat.normal_texture, baseDir, TextureColorSpace::Linear);
    if (mat.normalTexture.isPresent && cgMat.normal_texture.scale != 0.0F) {
        mat.normalTexture.scale = cgMat.normal_texture.scale;
    }

    mat.occlusionTexture =
        parseTextureInfo(cgMat.occlusion_texture, baseDir, TextureColorSpace::Linear);
    if (mat.occlusionTexture.isPresent && cgMat.occlusion_texture.scale != 0.0F) {
        mat.occlusionTexture.strength = cgMat.occlusion_texture.scale;
    }

    mat.emissiveTexture =
        parseTextureInfo(cgMat.emissive_texture, baseDir, TextureColorSpace::sRGB);
    mat.emissiveFactor =
        Vec3{cgMat.emissive_factor[0], cgMat.emissive_factor[1], cgMat.emissive_factor[2]};

    if (cgMat.alpha_mode == cgltf_alpha_mode_mask) {
        mat.alphaMode = AlphaMode::Mask;
        mat.alphaCutoff = cgMat.alpha_cutoff;
    } else if (cgMat.alpha_mode == cgltf_alpha_mode_blend) {
        mat.alphaMode = AlphaMode::Blend;
    } else {
        mat.alphaMode = AlphaMode::Opaque;
    }

    return mat;
}

[[nodiscard]] inline std::vector<Material> extractAllMaterials(const cgltf_data* data,
                                                               std::string_view baseDir) noexcept {
    std::vector<Material> materials;
    if (data == nullptr || data->materials_count == 0) {
        materials.push_back(Material::createDefault());
        return materials;
    }

    materials.reserve(data->materials_count);
    for (size_t i = 0; i < data->materials_count; ++i) {
        materials.push_back(parseMaterial(data->materials[i], baseDir, i));
    }
    return materials;
}

} // namespace eng::render::detail
