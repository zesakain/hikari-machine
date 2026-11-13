#!/usr/bin/env python3
import subprocess, struct, os, sys

shader_mappings = [
    ("kMeshVertexShaderSpirv", "assets/shaders/mesh.vert"),
    ("kMeshFragmentShaderSpirv", "assets/shaders/mesh.frag"),
    ("kTexturedMeshFragmentShaderSpirv", "assets/shaders/textured_mesh.frag"),
    ("kPbrMeshVertexShaderSpirv", "assets/shaders/pbr_mesh.vert"),
    ("kGltfMaterialFragmentShaderSpirv", "assets/shaders/pbr_mesh.frag"),
    ("kShadowDepthVertexShaderSpirv", "assets/shaders/shadow_depth.vert"),
    ("kFullscreenVertexShaderSpirv", "assets/shaders/fullscreen.vert"),
    ("kBloomDownsampleFragmentShaderSpirv", "assets/shaders/bloom_downsample.frag"),
    ("kBloomUpsampleFragmentShaderSpirv", "assets/shaders/bloom_upsample.frag"),
    ("kToneMappingFragmentShaderSpirv", "assets/shaders/tone_mapping.frag"),
]

output_header = "engine/render/include/render/mesh_shader_spirv.h"

lines = [
    "#pragma once",
    "",
    "#include <cstdint>",
    "",
    "namespace eng::render {",
    ""
]

for var_name, src in shader_mappings:
    spv_path = f"/tmp/{os.path.basename(src)}.spv"
    res = subprocess.run(["glslc", "-O", src, "-o", spv_path], capture_output=True, text=True)
    if res.returncode != 0:
        print(f"Error compiling {src}:\n{res.stderr}", file=sys.stderr)
        sys.exit(1)
    
    with open(spv_path, "rb") as f:
        spv_bytes = f.read()
    
    count = len(spv_bytes) // 4
    words = struct.unpack(f"<{count}I", spv_bytes)
    
    lines.append(f"inline constexpr uint32_t {var_name}[] = {{")
    for i in range(0, count, 7):
        chunk = words[i:i+7]
        chunk_str = ", ".join(f"0x{w:08x}U" for w in chunk)
        if i + 7 < count:
            lines.append(f"    {chunk_str},")
        else:
            lines.append(f"    {chunk_str},")
    lines.append("};")
    lines.append("")

lines.append("} // namespace eng::render")
lines.append("")

with open(output_header, "w") as f:
    f.write("\n".join(lines))

print(f"Successfully updated {output_header}")
