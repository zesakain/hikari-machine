#version 450

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;
layout(location = 4) in vec4 inTangent;
layout(location = 5) flat in vec3 inCameraPos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uBaseColorMap;
layout(set = 0, binding = 1) uniform sampler2D uMetallicRoughnessMap;
layout(set = 0, binding = 2) uniform sampler2D uNormalMap;
layout(set = 0, binding = 3) uniform sampler2D uOcclusionMap;
layout(set = 0, binding = 4) uniform sampler2D uEmissiveMap;

layout(std140, set = 0, binding = 5) uniform MaterialData {
    vec4 baseColorFactor;
    vec4 emissiveFactorAndAlphaCutoff;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    uint hasBaseColorTex;
    uint hasMetallicRoughnessTex;
    uint hasNormalTex;
    uint hasOcclusionTex;
    uint hasEmissiveTex;
    uint alphaMode;
    uint doubleSided;
    uint _pad;
} material;

const float PI = 3.141592653589793;

// GGX / Trowbridge-Reitz Normal Distribution Function
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / max(denom, 0.0000001);
}

// Smith Joint / GGX Correlated Visibility Function (V = G / (4 * NdotV * NdotL))
float visibilitySmithGGXCorrelated(float NdotV, float NdotL, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;

    float gv = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float gl = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);

    return 0.5 / max(gv + gl, 0.0000001);
}

// Schlick Fresnel Approximation
vec3 fresnelSchlick(float VdotH, vec3 F0) {
    return F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
}

void main() {
    // 1. Base Color & Alpha
    vec4 baseColor = material.baseColorFactor * inColor;
    if (material.hasBaseColorTex != 0u) {
        baseColor *= texture(uBaseColorMap, inUV);
    }

    // Alpha Cutoff / Masking
    if (material.alphaMode == 1u) { // Mask
        if (baseColor.a < material.emissiveFactorAndAlphaCutoff.w) {
            discard;
        }
    }

    // 2. Metallic and Roughness (glTF: Green = roughness, Blue = metallic)
    float metallic = material.metallicFactor;
    float roughness = material.roughnessFactor;
    if (material.hasMetallicRoughnessTex != 0u) {
        vec4 mrSample = texture(uMetallicRoughnessMap, inUV);
        roughness *= mrSample.g;
        metallic *= mrSample.b;
    }
    // Clamp to valid, numerically stable ranges
    metallic = clamp(metallic, 0.0, 1.0);
    roughness = clamp(roughness, 0.045, 1.0);

    // 3. Normal Mapping & TBN Basis
    vec3 N = normalize(inNormal);
    vec3 T = normalize(inTangent.xyz);
    // Gram-Schmidt re-orthogonalization to avoid interpolation skew
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * inTangent.w;
    mat3 TBN = mat3(T, B, N);

    if (material.hasNormalTex != 0u) {
        vec3 normalSample = texture(uNormalMap, inUV).xyz * 2.0 - 1.0;
        normalSample.xy *= material.normalScale;
        N = normalize(TBN * normalSample);
    }
    if (material.doubleSided != 0u && !gl_FrontFacing) {
        N = -N;
    }

    // 4. View and Light vectors
    vec3 V = normalize(inCameraPos - inWorldPos);
    // Primary directional light
    vec3 L = normalize(vec3(0.5, 0.8, 0.5));
    vec3 H = normalize(V + L);

    float NdotV = max(dot(N, V), 0.0001);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    // 5. Specular F0 reflection coefficient
    // Dielectric default F0 is 0.04 (approx 4% reflectance); metals use baseColor.
    vec3 F0 = mix(vec3(0.04), baseColor.rgb, metallic);

    // 6. Cook-Torrance Specular BRDF: D * V * F
    float D = distributionGGX(N, H, roughness);
    float Vis = visibilitySmithGGXCorrelated(NdotV, NdotL, roughness);
    vec3 F = fresnelSchlick(VdotH, F0);

    vec3 specTerm = D * Vis * F;

    // 7. Energy-Conserving Diffuse BRDF: kD * (baseColor / PI)
    // Refracted light is (1 - F); metallic surfaces absorb all refraction.
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffTerm = kD * (baseColor.rgb / PI);

    // 8. Direct Light Evaluation
    const vec3 lightColor = vec3(1.0, 1.0, 1.0);
    const float lightIntensity = 2.5;
    vec3 directLight = (diffTerm + specTerm) * lightColor * lightIntensity * NdotL;

    // 9. Ambient Occlusion (glTF: Red channel)
    float occlusion = 1.0;
    if (material.hasOcclusionTex != 0u) {
        occlusion = mix(1.0, texture(uOcclusionMap, inUV).r, material.occlusionStrength);
    }

    // 10. Ambient Term (environment fallback illuminating unlit/metallic facets)
    vec3 ambientDiffuse = baseColor.rgb * (1.0 - metallic) * 0.15 * occlusion;
    vec3 ambientSpecular = mix(F0, baseColor.rgb, metallic) * (1.0 - roughness) * 0.15 * occlusion;
    vec3 ambient = ambientDiffuse + ambientSpecular;

    // 11. Emissive contribution
    vec3 emissive = material.emissiveFactorAndAlphaCutoff.xyz;
    if (material.hasEmissiveTex != 0u) {
        emissive *= texture(uEmissiveMap, inUV).rgb;
    }

    // 12. Final Radiance Output
    // Ambient Occlusion modulates indirect/ambient lighting, never direct illumination.
    vec3 finalColor = directLight + ambient + emissive;
    outColor = vec4(finalColor, baseColor.a);
}
