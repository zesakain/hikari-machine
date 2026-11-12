#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D hdrSceneTexture;
layout(set = 0, binding = 1) uniform sampler2D bloomTexture;

layout(push_constant) uniform ToneMappingPushConstants {
    float exposure;
    int toneMapOp; // 0 = ACES, 1 = FilmicReinhard, 2 = Neutral, 3 = Bypass
    float bloomIntensity;
    int bloomEnabled;
    float whitePoint;
} pc;

vec3 acesFitted(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 filmicReinhard(vec3 color, float white) {
    float w = max(white, 0.01);
    return clamp(color * (1.0 + color / (w * w)) / (1.0 + color), 0.0, 1.0);
}

// Khronos PBR Neutral Tone Mapper
vec3 neutralToneMapping(vec3 color) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return clamp(color, 0.0, 1.0);

    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return clamp(mix(color, vec3(newPeak), g), 0.0, 1.0);
}

vec3 linearBypass(vec3 color) {
    return clamp(color, 0.0, 1.0);
}

float linearToSrgb(float x) {
    return x <= 0.0031308 ? 12.92 * x : 1.055 * pow(max(x, 0.0), 1.0 / 2.4) - 0.055;
}

vec3 linearToSrgb(vec3 c) {
    return vec3(linearToSrgb(c.r), linearToSrgb(c.g), linearToSrgb(c.b));
}

void main() {
    vec4 sceneSample = texture(hdrSceneTexture, inUV);
    vec3 color = sceneSample.rgb;

    if (pc.bloomEnabled != 0 && pc.bloomIntensity > 0.0) {
        vec3 bloom = texture(bloomTexture, inUV).rgb;
        color += bloom * pc.bloomIntensity;
    }

    // Apply photometric linear exposure scale
    color *= max(pc.exposure, 0.0);

    // Apply tone mapping operator
    vec3 mappedColor = color;
    if (pc.toneMapOp == 0) {
        mappedColor = acesFitted(color);
    } else if (pc.toneMapOp == 1) {
        mappedColor = filmicReinhard(color, pc.whitePoint);
    } else if (pc.toneMapOp == 2) {
        mappedColor = neutralToneMapping(color);
    } else {
        mappedColor = linearBypass(color);
    }

    // Apply sRGB transfer curve for standard UNORM swapchain
    vec3 srgbColor = clamp(linearToSrgb(mappedColor), 0.0, 1.0);

    outColor = vec4(srgbColor, sceneSample.a);
}
