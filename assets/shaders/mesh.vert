#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    mat4 model;
} pc;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec4 outColor;
layout(location = 2) out vec2 outUV;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = pc.viewProj * worldPos;

    mat3 normalMatrix = transpose(inverse(mat3(pc.model)));
    outNormal = normalize(normalMatrix * inNormal);
    outColor = inColor;
    outUV = inUV;
}
