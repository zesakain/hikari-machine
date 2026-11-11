#version 450

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;
layout(location = 4) in vec4 inTangent;
layout(location = 5) flat in vec3 inCameraPos;

layout(location = 0) out vec4 outColor;

// Set 0: Material Textures and Parameters
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

// Set 1: Scene Lighting and Shadow Map
struct GpuDirectionalLight {
    vec4 directionAndCastShadow; // xyz: dir, w: castShadows
    vec4 colorAndIntensity;     // rgb: color, a: intensity
};

struct GpuPointLight {
    vec4 positionAndRange;      // xyz: pos, w: range
    vec4 colorAndIntensity;     // rgb: color, a: intensity
};

struct GpuSpotLight {
    vec4 positionAndRange;      // xyz: pos, w: range
    vec4 directionAndIntensity; // xyz: dir, w: intensity
    vec4 colorAndCastShadow;    // rgb: color, a: castShadows
    vec4 coneAngles;            // x: cosInner, y: cosOuter, zw: pad
};

layout(std140, set = 1, binding = 0) uniform LightingData {
    mat4 lightSpaceMatrix;
    GpuDirectionalLight dirLight;
    GpuPointLight pointLights[16];
    GpuSpotLight spotLights[16];
    vec4 cameraPosAndAmbientIntensity;
    vec4 ambientColorAndShadowBias;
    uint numPointLights;
    uint numSpotLights;
    uint dirLightActive;
    uint _pad;
} uLighting;

layout(set = 1, binding = 1) uniform sampler2DShadow uShadowMap;

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

// Evaluate Cook-Torrance BRDF for a single incident light
vec3 evaluateDirectLight(vec3 L, vec3 radiance, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) {
        return vec3(0.0);
    }
    float NdotV = max(dot(N, V), 0.0001);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float D = distributionGGX(N, H, roughness);
    float Vis = visibilitySmithGGXCorrelated(NdotV, NdotL, roughness);
    vec3 F = fresnelSchlick(VdotH, F0);

    vec3 specTerm = D * Vis * F;
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffTerm = kD * (albedo / PI);

    return (diffTerm + specTerm) * radiance * NdotL;
}

// Sample directional shadow map with 3x3 PCF and slope-scaled depth bias
float calculateShadow(vec4 lightSpacePos, vec3 N, vec3 L, float baseBias) {
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x < -1.0 || projCoords.x > 1.0 ||
        projCoords.y < -1.0 || projCoords.y > 1.0) {
        return 1.0;
    }

    vec2 shadowUV = vec2(projCoords.x * 0.5 + 0.5, projCoords.y * -0.5 + 0.5);
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0) {
        return 1.0;
    }

    float NdotL = max(dot(N, L), 0.0);
    float bias = max(baseBias * (1.0 - NdotL), baseBias * 0.2);
    float currentDepth = projCoords.z;

    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(uShadowMap, 0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            shadow += texture(uShadowMap, vec3(shadowUV + vec2(x, y) * texelSize, currentDepth - bias));
        }
    }
    return shadow / 9.0;
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
    metallic = clamp(metallic, 0.0, 1.0);
    roughness = clamp(roughness, 0.045, 1.0);

    // 3. Normal Mapping & TBN Basis
    vec3 N = normalize(inNormal);
    vec3 T = normalize(inTangent.xyz);
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

    // 4. View vector and Specular F0
    vec3 camPos = (length(uLighting.cameraPosAndAmbientIntensity.xyz) > 0.0)
                      ? uLighting.cameraPosAndAmbientIntensity.xyz
                      : inCameraPos;
    vec3 V = normalize(camPos - inWorldPos);
    vec3 F0 = mix(vec3(0.04), baseColor.rgb, metallic);

    // 5. Direct Light Accumulation
    vec3 directLighting = vec3(0.0);

    // 5a. Directional Light
    if (uLighting.dirLightActive != 0u) {
        vec3 L = normalize(uLighting.dirLight.directionAndCastShadow.xyz);
        vec3 radiance = uLighting.dirLight.colorAndIntensity.rgb * uLighting.dirLight.colorAndIntensity.w;
        float shadow = 1.0;
        if (uLighting.dirLight.directionAndCastShadow.w > 0.5) {
            vec4 lightSpacePos = uLighting.lightSpaceMatrix * vec4(inWorldPos, 1.0);
            shadow = calculateShadow(lightSpacePos, N, L, uLighting.ambientColorAndShadowBias.w);
        }
        directLighting += shadow * evaluateDirectLight(L, radiance, N, V, baseColor.rgb, metallic, roughness, F0);
    }

    // 5b. Point Lights
    for (uint i = 0u; i < uLighting.numPointLights && i < 16u; ++i) {
        vec3 lightPos = uLighting.pointLights[i].positionAndRange.xyz;
        float range = uLighting.pointLights[i].positionAndRange.w;
        vec3 toLight = lightPos - inWorldPos;
        float dist = length(toLight);
        if (dist < range && dist > 0.0001) {
            vec3 L = toLight / dist;
            float atten = 1.0 / (dist * dist + 1.0);
            float window = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
            atten *= (window * window);

            vec3 radiance = uLighting.pointLights[i].colorAndIntensity.rgb *
                            uLighting.pointLights[i].colorAndIntensity.w * atten;
            directLighting += evaluateDirectLight(L, radiance, N, V, baseColor.rgb, metallic, roughness, F0);
        }
    }

    // 5c. Spot Lights
    for (uint i = 0u; i < uLighting.numSpotLights && i < 16u; ++i) {
        vec3 lightPos = uLighting.spotLights[i].positionAndRange.xyz;
        float range = uLighting.spotLights[i].positionAndRange.w;
        vec3 toLight = lightPos - inWorldPos;
        float dist = length(toLight);
        if (dist < range && dist > 0.0001) {
            vec3 L = toLight / dist;
            vec3 spotDir = normalize(uLighting.spotLights[i].directionAndIntensity.xyz);
            float cosAngle = dot(-L, spotDir);
            float cosInner = uLighting.spotLights[i].coneAngles.x;
            float cosOuter = uLighting.spotLights[i].coneAngles.y;

            float spotAtten = clamp((cosAngle - cosOuter) / max(cosInner - cosOuter, 0.0001), 0.0, 1.0);
            spotAtten = spotAtten * spotAtten;

            if (spotAtten > 0.0) {
                float distAtten = 1.0 / (dist * dist + 1.0);
                float window = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
                distAtten *= (window * window);

                vec3 radiance = uLighting.spotLights[i].colorAndCastShadow.rgb *
                                uLighting.spotLights[i].directionAndIntensity.w *
                                distAtten * spotAtten;
                directLighting += evaluateDirectLight(L, radiance, N, V, baseColor.rgb, metallic, roughness, F0);
            }
        }
    }

    // 6. Ambient Occlusion (glTF: Red channel)
    float occlusion = 1.0;
    if (material.hasOcclusionTex != 0u) {
        occlusion = mix(1.0, texture(uOcclusionMap, inUV).r, material.occlusionStrength);
    }

    // 7. Ambient Term (environment fallback scaled by ambient parameters)
    float ambIntensity = uLighting.cameraPosAndAmbientIntensity.w;
    vec3 ambColor = uLighting.ambientColorAndShadowBias.rgb;
    vec3 ambientDiffuse = baseColor.rgb * (1.0 - metallic) * ambIntensity * ambColor * occlusion;
    vec3 ambientSpecular = mix(F0, baseColor.rgb, metallic) * (1.0 - roughness) * ambIntensity * ambColor * occlusion;
    vec3 ambient = ambientDiffuse + ambientSpecular;

    // 8. Emissive Term
    vec3 emissive = material.emissiveFactorAndAlphaCutoff.xyz;
    if (material.hasEmissiveTex != 0u) {
        emissive *= texture(uEmissiveMap, inUV).rgb;
    }

    // 9. Final Radiance Output
    vec3 finalColor = directLighting + ambient + emissive;
    outColor = vec4(finalColor, baseColor.a);
}
