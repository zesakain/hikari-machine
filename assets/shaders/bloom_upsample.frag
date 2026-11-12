#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D srcTexture;
layout(set = 0, binding = 1) uniform sampler2D additiveTexture;

layout(push_constant) uniform BloomUpsamplePushConstants {
    vec2 texelSize;
    float filterRadius;
    float blendFactor;
} pc;

void main() {
    vec2 d = pc.texelSize * pc.filterRadius;

    // 9-tap 3x3 tent filter (bilinear-friendly weighted reconstruction)
    vec3 a = texture(srcTexture, inUV + vec2(-1.0,  1.0) * d).rgb;
    vec3 b = texture(srcTexture, inUV + vec2( 0.0,  1.0) * d).rgb;
    vec3 c = texture(srcTexture, inUV + vec2( 1.0,  1.0) * d).rgb;

    vec3 d0 = texture(srcTexture, inUV + vec2(-1.0,  0.0) * d).rgb;
    vec3 e  = texture(srcTexture, inUV + vec2( 0.0,  0.0) * d).rgb;
    vec3 f  = texture(srcTexture, inUV + vec2( 1.0,  0.0) * d).rgb;

    vec3 g = texture(srcTexture, inUV + vec2(-1.0, -1.0) * d).rgb;
    vec3 h = texture(srcTexture, inUV + vec2( 0.0, -1.0) * d).rgb;
    vec3 i = texture(srcTexture, inUV + vec2( 1.0, -1.0) * d).rgb;

    vec3 upsampled = e * 4.0;
    upsampled += (b + d0 + f + h) * 2.0;
    upsampled += (a + c + g + i) * 1.0;
    upsampled *= (1.0 / 16.0);

    vec3 current = texture(additiveTexture, inUV).rgb;
    vec3 color = upsampled + current * pc.blendFactor;

    outColor = vec4(color, 1.0);
}
