#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;
layout(location = 4) in vec4 inTangent;

layout(push_constant) uniform PushConstants {
    mat4 lightViewProj;
    mat4 model;
} pc;

void main() {
    gl_Position = pc.lightViewProj * pc.model * vec4(inPosition, 1.0);
}
