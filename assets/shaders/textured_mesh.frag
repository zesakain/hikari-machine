#version 450

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D uTexture;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.6));
    vec3 norm = normalize(inNormal);
    float diff = max(dot(norm, lightDir), 0.0);
    
    float ambient = 0.25;
    float lightFactor = ambient + (0.75 * diff);

    vec4 texColor = texture(uTexture, inUV);
    outColor = vec4(texColor.rgb * lightFactor, texColor.a);
}
