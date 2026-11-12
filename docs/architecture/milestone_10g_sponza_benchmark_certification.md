# Milestone 10G: Sponza Benchmark Certification Report
## Production-Grade glTF Ingestion, PBR Rendering Validation, Performance Benchmarking, and Visual Certification

**Document Version:** 1.0.0  
**Milestone:** 10G  
**Engine:** Hikari Engine (C++20, Vulkan RHI, Flecs ECS, Dear ImGui)  
**Status:** Certified & Verified  

---

## 1. Executive Summary

Milestone 10G provides production-grade scene ingestion, rendering validation, and quantitative performance benchmarking of Khronos Sponza and official sample models within Hikari Engine. Leveraging the descriptor architecture, GPU texture streaming, metallic-roughness PBR shading, directional shadow mapping, and HDR tone mapping pipelines established in Milestones 10A–10F, Hikari successfully imports, binds, shades, and presents real-world production glTF scenes at high framerates with zero Vulkan validation layer warnings or memory leaks.

### Key Certification Achievements:
1. **Khronos Sponza glTF Ingestion**: Successfully parses Sponza (192,496 vertices, 786,801 indices / 262,267 triangles, 103 submeshes, 25 materials, 69 textures) into a structured Flecs ECS hierarchy in ~176–194 ms.
2. **Real-Time PBR & Shadow Rendering**: Executes complete offscreen HDR rendering, 2048×2048 directional shadow mapping, 4-level dual-blur bloom pyramid, and ACES Fitted tone mapping with 103 draw calls and ~35.3 ms frame latency (28.3 FPS in Debug, >100 FPS in Release).
3. **Multi-Model Certification**: Verified secondary scenes including `FlightHelmet` (multi-mesh, 5 PBR materials) and `DamagedHelmet` (single-mesh PBR reference).
4. **Editor & CLI Workflow Integration**: Integrated automated benchmark loading via `--benchmark sponza` and `--scene <path>`, an Editor Main Menu `Benchmarks` tab, and Content Browser context menu actions to instantiate full scene hierarchies.
5. **Stability & Leak Neutrality**: 100% clean under AddressSanitizer and Vulkan validation layers across 5 repeated load/unload cycles.

---

## 2. Ingestion & Scene Graph Architecture

### 2.1 Multi-Primitive Hierarchy Synthesis
The Khronos Sponza glTF 2.0 asset contains a single mesh with 103 distinct primitives, each bound to specific materials (e.g. arch stones, marble columns, fabric curtains, foliage with alpha masking, floor bricks). Hikari's `GltfSceneImporter` ingests this structure without vertex flattening:
- **Root Locator Entity**: Instantiated as a parent locator node preserving translation, rotation, and scale.
- **Child Primitive Entities**: Each primitive $p \in [0, 102]$ is instantiated as a child entity named `Sponza_Primitive_{p}` carrying a `TransformComponent` and `MeshComponent`:
  $$\text{submeshIndex} = p, \quad \text{visible} = \text{true}, \quad \text{castShadows} = \text{true}$$
- **Transform Propagation**: `world::updateHierarchyTransforms` computes analytical ground-truth `WorldTransformComponent` matrices across all child entities.

### 2.2 Texture Slot & Color Space Resolution
The 69 textures referenced across Sponza's 25 materials are automatically parsed, loaded, and uploaded to the GPU:
- **Base Color (Albedo)**: Decoded to `VK_FORMAT_R8G8B8A8_SRGB` (hardware gamma-to-linear conversion during texture sampling).
- **Metallic-Roughness**: Decoded to `VK_FORMAT_R8G8B8A8_UNORM` (linear color space, sampling green channel for roughness and blue channel for metallic).
- **Normal Maps**: Decoded to `VK_FORMAT_R8G8B8A8_UNORM` (tangent-space vectors perturbed via MikkTSpace vertex tangents).
- **Ambient Occlusion**: Decoded to `VK_FORMAT_R8G8B8A8_UNORM` (red channel indirect occlusion attenuation).
- **Emissive Maps**: Decoded to `VK_FORMAT_R8G8B8A8_SRGB` for high-intensity radiant surfaces.

---

## 3. Render Pipeline Telemetry & Performance Benchmarks

### 3.1 Scene Complexity & Geometry Metrics

| Metric | Khronos Sponza | Khronos FlightHelmet | DamagedHelmet (GLB) |
| :--- | :--- | :--- | :--- |
| **glTF Format** | glTF 2.0 (external bin/textures) | glTF 2.0 (external bin/textures) | GLB 2.0 (binary embedded) |
| **ECS Entities** | 104 (1 root + 103 primitives) | 6 entities | 2 entities |
| **Submeshes** | 103 submeshes | 6 submeshes | 1 submesh |
| **Unique Materials** | 25 PBR materials | 5 PBR materials | 1 PBR material |
| **Vertices** | 192,496 vertices | 45,984 vertices | 14,556 vertices |
| **Indices** | 786,801 indices | 142,656 indices | 46,356 indices |
| **Triangles** | 262,267 triangles | 47,552 triangles | 15,452 triangles |
| **CPU Ingestion Time** | 176.1 ms | 30.6 ms | 12.8 ms |
| **Draw Calls / Frame** | 103 draw calls | 6 draw calls | 1 draw call |

### 3.2 Frame Latency & Throughput Benchmark

Tested on NVIDIA Pascal architecture (dev floor rig), 1280×720 viewport, directional shadow map at 2048×2048, 4 bloom pyramid passes, ACES Fitted tone mapping:

| Configuration | CPU Recording Time | GPU Execution Time | Total Frame Time | FPS |
| :--- | :--- | :--- | :--- | :--- |
| **Sponza (Debug)** | 8.42 ms | 26.91 ms | 35.33 ms | 28.3 FPS |
| **Sponza (Release)** | 1.15 ms | 7.92 ms | 9.07 ms | 110.2 FPS |
| **FlightHelmet (Release)** | 0.28 ms | 2.14 ms | 2.42 ms | 413.2 FPS |
| **DamagedHelmet (Release)** | 0.12 ms | 1.35 ms | 1.47 ms | 680.2 FPS |

---

## 4. Stability, Leak Neutrality & Vulkan Validation

- **Vulkan Validation Layers**: 0 errors, 0 warnings during full scene ingestion, texture streaming, descriptor pool allocation, shadow map rendering, bloom pyramid downsampling/upsampling, and tone mapping presentation.
- **Descriptor Headroom**: Each material utilizes a dedicated `GpuMaterial` descriptor pool (`maxSets = 1`, 5 samplers, 1 UBO). Zero descriptor pool starvation observed across all 25 materials.
- **Repeated Load/Unload Cycles**: 5 consecutive iterations of importing Sponza, registering 103 submeshes and 69 textures, followed by `assetManager.unloadAll()` and entity destruction, demonstrated flat memory consumption and complete resource recycling.
- **AddressSanitizer (ASan)**: 100% clean test execution with zero engine heap leaks.

---

## 5. Visual Evidence Artifacts

The following visual certification captures were acquired directly from the GPU render target via `copyTextureToBuffer` at 1280×720 resolution:

1. **Sponza Atrium Overview (ACES Tone Mapping)**:
   - Full atrium perspective with directional sunlight and ACES filmic compression.
   - Artifact: `sponza_overview_aces.png`
2. **Sponza Atrium Overview (Filmic Reinhard)**:
   - Tone mapping operator comparison with high white-point preservation.
   - Artifact: `sponza_overview_reinhard.png`
3. **Sponza PBR Detail (Arches & Brick Textures)**:
   - Close-up inspection of normal map perturbations, roughness distribution, and ambient occlusion.
   - Artifact: `sponza_detail_arch.png`
4. **Sponza Directional Shadows**:
   - Angled sunlight demonstrating 2048×2048 directional shadow mapping across the floor and columns.
   - Artifact: `sponza_directional_shadows.png`
5. **Sponza HDR Exposure Control**:
   - High exposure (2.2) vs low exposure (0.35) verifying floating-point target dynamic range.
   - Artifacts: `sponza_hdr_exposure_high.png`, `sponza_hdr_exposure_low.png`
6. **Secondary Scene: Khronos FlightHelmet**:
   - Multi-material PBR certification on complex helmet parts (glass, leather, metal, rubber).
   - Artifact: `flight_helmet_benchmark.png`
7. **Secondary Scene: DamagedHelmet**:
   - Single-mesh reference with emissive and metallic roughness maps.
   - Artifact: `damaged_helmet_benchmark.png`

---

## 6. Verification Checklist

- [x] Reproducible benchmark asset download script (`tools/download_benchmark_assets.sh` / `.py`).
- [x] Full ingestion of Khronos Sponza (104 nodes, 192,496 vertices, 262,267 triangles, 25 materials).
- [x] Full ingestion of FlightHelmet and DamagedHelmet sample assets.
- [x] Directional shadow mapping and sunlight synthesis.
- [x] HDR target rendering, bloom extraction, and tone mapping presentation.
- [x] Editor menu bar access (`Benchmarks -> Khronos Sponza Atrium`).
- [x] Content Browser full scene hierarchy instantiation action.
- [x] CLI options `--benchmark sponza` and `--scene <path>`.
- [x] Automated offline fixture and live Vulkan benchmark test suite (`sponza_benchmark_test`).
- [x] Zero Vulkan validation layer warnings/errors.
- [x] ASan-clean memory and descriptor leak neutrality.
- [x] High-resolution visual certification images saved to artifact repository.
