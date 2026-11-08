/// @file material.h
/// @brief Material definition and basic shading parameters.
///
/// Purpose:
/// Defines surface properties (base color, roughness, metallic) and texture references
/// for mesh rendering.
///
/// Ownership and Lifetime:
/// POD/value type. Managed as an asset or embedded in mesh rendering descriptors.
///
/// Thread-safety:
/// Value type; const access is thread-safe.

#pragma once

#include "core/math.h"
#include "render/asset_types.h"

#include <string>

namespace eng::render {

/// @struct Material
/// @brief Surface shading properties.
struct Material {
    std::string name{"DefaultMaterial"};
    Vec4 baseColor{1.0F, 1.0F, 1.0F, 1.0F};
    float roughness{0.5F};
    float metallic{0.0F};
    AssetId textureAsset{};

    [[nodiscard]] static Material createDefault() noexcept {
        return Material{
            .name = "DefaultMaterial",
            .baseColor = {0.8F, 0.8F, 0.82F, 1.0F},
            .roughness = 0.5F,
            .metallic = 0.0F,
            .textureAsset = {},
        };
    }
};

} // namespace eng::render
