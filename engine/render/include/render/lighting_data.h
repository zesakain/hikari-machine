/// @file lighting_data.h
/// @brief GPU lighting uniform buffer and scene descriptor layout definitions.
///
/// Purpose:
/// Declares binary-compatible std140 GPU lighting uniform structures and descriptor set layouts
/// for forward multi-light Cook-Torrance PBR shading and directional shadow mapping.
///
/// Contract:
/// - Exact 16-byte alignment and padding matching GLSL std140 rules.
/// - Total buffer size must match static assertion (1680 bytes).
///
/// Ownership and Lifetime:
/// Value types copied directly to GPU uniform buffers via RhiDevice::writeBuffer.
///
/// Thread-safety:
/// Const value structs; thread-safe for reading and stack allocations.
///
/// Cost:
/// Standard layout structs, zero heap allocation.

#pragma once

#include "core/math.h"
#include "rhi/descriptor.h"

#include <cstdint>

namespace eng::render {

inline constexpr uint32_t kMaxPointLights = 16;
inline constexpr uint32_t kMaxSpotLights = 16;

/// @struct GpuDirectionalLight
/// @brief std140-aligned representation of directional sun illumination.
struct GpuDirectionalLight {
    /// xyz: Light direction vector; w: castShadows flag (1.0 = true, 0.0 = false).
    Vec4 directionAndCastShadow{0.5F, 0.8F, 0.5F, 1.0F};

    /// rgb: Emitted radiance color; a: intensity multiplier.
    Vec4 colorAndIntensity{1.0F, 1.0F, 1.0F, 2.5F};
};

/// @struct GpuPointLight
/// @brief std140-aligned representation of omnidirectional point light.
struct GpuPointLight {
    /// xyz: World-space position; w: effective range in meters.
    Vec4 positionAndRange{0.0F, 0.0F, 0.0F, 10.0F};

    /// rgb: Emitted radiance color; a: intensity multiplier.
    Vec4 colorAndIntensity{1.0F, 1.0F, 1.0F, 0.0F};
};

/// @struct GpuSpotLight
/// @brief std140-aligned representation of directed conical spotlight.
struct GpuSpotLight {
    /// xyz: World-space position; w: effective range in meters.
    Vec4 positionAndRange{0.0F, 0.0F, 0.0F, 15.0F};

    /// xyz: World-space direction; w: intensity multiplier.
    Vec4 directionAndIntensity{0.0F, -1.0F, 0.0F, 0.0F};

    /// rgb: Emitted radiance color; a: castShadows flag (1.0 = true, 0.0 = false).
    Vec4 colorAndCastShadow{1.0F, 1.0F, 1.0F, 0.0F};

    /// x: cos(innerConeAngle); y: cos(outerConeAngle); zw: padding.
    Vec4 coneAngles{0.9F, 0.8F, 0.0F, 0.0F};
};

/// @struct GpuLightingData
/// @brief Uniform buffer block bound to Set 1, Binding 0 (size: 1680 bytes).
struct alignas(16) GpuLightingData {
    /// Light-space view-projection transformation matrix for shadow projection (64 bytes).
    Mat4 lightSpaceMatrix{Mat4::identity()};

    /// Directional sunlight parameters (32 bytes).
    GpuDirectionalLight dirLight{};

    /// Omnidirectional point lights array (16 * 32 = 512 bytes).
    GpuPointLight pointLights[kMaxPointLights]{};

    /// Conical spotlights array (16 * 64 = 1024 bytes).
    GpuSpotLight spotLights[kMaxSpotLights]{};

    /// xyz: World-space camera position; w: ambient intensity scalar (16 bytes).
    Vec4 cameraPosAndAmbientIntensity{0.0F, 0.0F, 5.0F, 0.15F};

    /// rgb: Ambient light color; w: constant shadow bias (16 bytes).
    Vec4 ambientColorAndShadowBias{1.0F, 1.0F, 1.0F, 0.005F};

    /// Number of active point lights (up to 16).
    uint32_t numPointLights{0};

    /// Number of active spotlights (up to 16).
    uint32_t numSpotLights{0};

    /// Whether directional light is active (1 = active, 0 = inactive).
    uint32_t dirLightActive{1};

    /// Packed shadow parameters: lower 16 bits = resolution, upper 16 bits = PCF quality.
    uint32_t shadowPackedParams{0};
};

static_assert(sizeof(GpuLightingData) == 1680,
              "GpuLightingData must strictly match std140 layout (1680 bytes)");

/// @brief Generates descriptor set layout descriptor for Scene/Lighting/Shadows (Set 1).
///
/// Binding 0: UniformBuffer (LightingData) in Vertex | Fragment stages.
/// Binding 1: CombinedImageSampler (Directional Shadow Map) in Fragment stage.
[[nodiscard]] inline rhi::DescriptorSetLayoutDesc createSceneDescriptorSetLayoutDesc() {
    return rhi::DescriptorSetLayoutDesc{
        .bindings =
            {
                {
                    .binding = 0,
                    .descriptorType = rhi::DescriptorType::UniformBuffer,
                    .descriptorCount = 1,
                    .stageFlags = rhi::ShaderStageFlags::Vertex | rhi::ShaderStageFlags::Fragment,
                },
                {
                    .binding = 1,
                    .descriptorType = rhi::DescriptorType::CombinedImageSampler,
                    .descriptorCount = 1,
                    .stageFlags = rhi::ShaderStageFlags::Fragment,
                },
            },
        .debugName = "SceneLightingDescriptorSetLayout",
    };
}

} // namespace eng::render
