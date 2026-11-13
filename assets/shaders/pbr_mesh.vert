#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;
layout(location = 4) in vec4 inTangent;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    mat4 model;
} pc;

layout(std140, set = 1, binding = 0) uniform LightingData {
    mat4 lightSpaceMatrix;
} uLighting;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec4 outColor;
layout(location = 4) out vec4 outTangent;
layout(location = 5) out vec3 outShadowCoord;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);

    mat3 normalMatrix = mat3(pc.model);
    outWorldPos = worldPos.xyz;
    outNormal = normalize(normalMatrix * inNormal);
    outUV = inUV;
    outColor = inColor;

    // Transform tangent vector by model matrix (excluding translation)
    vec3 worldTan = normalize(normalMatrix * inTangent.xyz);
    outTangent = vec4(worldTan, inTangent.w);

    // Compute light-space coordinates for directional shadow map directly in vertex stage
    vec4 lpos = uLighting.lightSpaceMatrix * worldPos;
    outShadowCoord = vec3(lpos.x * 0.5 + 0.5, lpos.y * -0.5 + 0.5, lpos.z);
}
