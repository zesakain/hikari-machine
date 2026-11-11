# Milestone 10C: glTF Material Integration Architecture

## 1. Overview

Milestone 10C connects imported glTF 2.0 materials to the Hikari Engine rendering pipeline. Building upon Milestone 10A (RHI descriptor and resource binding) and Milestone 10B (image decoding, GPU texture upload, and sampler management), this milestone establishes material data representation, color-space management, GPU material resource caching, and descriptor binding during scene rendering.

**Scope Boundary:** Milestone 10C focuses strictly on material data ingestion, texture resolution, fallback handling, and descriptor binding. Full metallic-roughness PBR lighting, image-based lighting (IBL), shadows, and post-processing belong to Milestone 10D.

---

## 2. glTF Material Data Model

### 2.1 Core Material Representation (`Material`)

`engine/render/include/render/material.h` defines the engine's core material structures:
- `baseColor`: 4-component vector (`baseColorFactor`, default `{1, 1, 1, 1}`).
- `metallic`: scalar metallic factor (default `1.0f`).
- `roughness`: scalar roughness factor (default `1.0f`).
- `emissiveFactor`: 3-component vector (default `{0, 0, 0}`).
- `alphaMode`: `Opaque`, `Mask`, or `Blend` (default `Opaque`).
- `alphaCutoff`: cutoff threshold when `alphaMode == Mask` (default `0.5f`).
- `doubleSided`: boolean flag indicating two-sided rasterization (default `false`).

### 2.2 Texture References (`TextureInfo`)

Each material supports five standard glTF texture slots:
1. `baseColorTexture` (sRGB)
2. `metallicRoughnessTexture` (Linear)
3. `normalTexture` (Linear, with `scale` parameter, default `1.0f`)
4. `occlusionTexture` (Linear, with `strength` parameter, default `1.0f`)
5. `emissiveTexture` (sRGB)

Each `TextureInfo` tracks:
- `isPresent`: indicates whether the glTF material declared this texture.
- `uri`: asset URI or embedded buffer identifier.
- `texCoord`: texture coordinate attribute index (e.g., `0` for `TEXCOORD_0`).
- `colorSpace`: `TextureColorSpace::sRGB` or `TextureColorSpace::Linear`.
- `sampler`: filter and wrap parameters mapped to `TextureSamplerDesc`.

### 2.3 Embedded GLB Image Decoding

In self-contained GLB containers or buffer-view-backed glTF files:
- Embedded image buffers are extracted from `cgltf_buffer_view` slices.
- URIs are assigned deterministic identifiers: `<sourcePath>#image_<index>` or `embedded://image_<index>`.
- The decoded image data is registered directly into `AssetManager` with its deterministic URI and color space suffix, enabling zero-copy sharing across materials referencing the same image buffer.

---

## 3. Color Space & Cache Key Isolation

glTF 2.0 specifies distinct color spaces based on texture semantic:
- **sRGB Data:** `baseColorTexture`, `emissiveTexture` (stored with sRGB transfer function, decoded in hardware to linear color).
- **Linear Data:** `metallicRoughnessTexture`, `normalTexture`, `occlusionTexture` (stored as raw mathematical data, must never be sRGB-decoded).

To prevent cache aliasing where an identical image path or embedded buffer is referenced in different color-space contexts:
- Cache keys in `AssetManager` append `#srgb` or `#linear` suffixes.
- Requests for linear and sRGB interpretations of the same asset yield independent `GpuTexture` instances with correct `VkFormat` (e.g. `VK_FORMAT_R8G8B8A8_SRGB` vs `VK_FORMAT_R8G8B8A8_UNORM`).

### 3.1 Fallback Default Textures

When optional texture maps are omitted from a material, `AssetManager` provides procedural 1x1 fallback textures via `getOrCreateDefaultTexture`:
- `DefaultTextureType::WhiteSRGB`: 1x1 `{255, 255, 255, 255}` sRGB (for missing base color).
- `DefaultTextureType::WhiteLinear`: 1x1 `{255, 255, 255, 255}` Linear (for missing metallic-roughness or occlusion).
- `DefaultTextureType::BlackSRGB`: 1x1 `{0, 0, 0, 255}` sRGB (for missing emissive map).
- `DefaultTextureType::FlatNormalLinear`: 1x1 `{128, 128, 255, 255}` Linear (flat tangent-space normal pointing in +Z).

---

## 4. GpuMaterial and Descriptor Binding

### 4.1 Descriptor Set Layout (Set 0)

Materials are represented on the GPU by `GpuMaterial` (`engine/render/include/render/gpu_material.h`), binding to Descriptor Set 0:
```
Set 0 Layout:
  Binding 0: CombinedImageSampler (Fragment) -> uBaseColorMap
  Binding 1: CombinedImageSampler (Fragment) -> uMetallicRoughnessMap
  Binding 2: CombinedImageSampler (Fragment) -> uNormalMap
  Binding 3: CombinedImageSampler (Fragment) -> uOcclusionMap
  Binding 4: CombinedImageSampler (Fragment) -> uEmissiveMap
  Binding 5: UniformBuffer        (Fragment) -> MaterialData (80 bytes, std140)
```

### 4.2 Material Uniform Buffer Layout (`MaterialUniformBufferData`)

Aligned to std140 rules:
```cpp
struct MaterialUniformBufferData {
    Vec4 baseColorFactor{1.0F, 1.0F, 1.0F, 1.0F};           // Offset 0 (16B)
    Vec4 emissiveFactorAndAlphaCutoff{0.0F, 0.0F, 0.0F, 0.5F}; // Offset 16 (16B)
    float metallicFactor{1.0F};                              // Offset 32 (4B)
    float roughnessFactor{1.0F};                             // Offset 36 (4B)
    float normalScale{1.0F};                                 // Offset 40 (4B)
    float occlusionStrength{1.0F};                           // Offset 44 (4B)
    uint32_t hasBaseColorTex{0};                             // Offset 48 (4B)
    uint32_t hasMetallicRoughnessTex{0};                     // Offset 52 (4B)
    uint32_t hasNormalTex{0};                                // Offset 56 (4B)
    uint32_t hasOcclusionTex{0};                             // Offset 60 (4B)
    uint32_t hasEmissiveTex{0};                              // Offset 64 (4B)
    uint32_t alphaMode{0};                                   // Offset 68 (4B)
    uint32_t doubleSided{0};                                 // Offset 72 (4B)
    uint32_t _pad{0};                                        // Offset 76 (4B)
};                                                           // Total: 80 bytes
```

### 4.3 Lifecycle & Memory Management

Each `GpuMaterial` owns:
- A dedicated `DescriptorPoolHandle` (sized for 1 set: 5 combined image samplers, 1 UBO).
- An allocated `DescriptorSetHandle`.
- An allocated `BufferHandle` (`MemoryUsage::CpuToGpu` for mapped host writes).

All resources are destroyed deterministically via RAII in `~GpuMaterial()`. `AssetManager` caches `GpuMaterial` instances per `(meshId, materialIndex)`.

---

## 5. SceneRenderer Integration

### 5.1 Pipeline Configuration

- `SceneRenderer::init()` creates `materialDescriptorSetLayout_` using `createMaterialDescriptorSetLayoutDesc()`.
- `pipelineLayout_` specifies `materialDescriptorSetLayout_` at Set 0 alongside 128-byte push constants for vertex transformations.
- `fragmentShader_` uses `kGltfMaterialFragmentShaderSpirv`, evaluating material factors, texture sampling, and alpha masking.

### 5.2 Hot-Path Execution (`renderScene`)

During scene recording:
1. Active meshes are retrieved from `AssetManager::getGpuMesh()`.
2. Model and view-projection matrices are pushed via push constants.
3. Vertex and index buffers are bound.
4. For each submesh:
   - Corresponding `GpuMaterial` is retrieved via `AssetManager::getGpuMaterial()`.
   - If not yet loaded, uploaded on-demand or resolved to default material fallback.
   - `DescriptorSetHandle` is bound to Set 0 via `cmdList.bindDescriptorSets()`.
   - `drawIndexed` is recorded.
5. In compliance with Rule 5, zero heap allocations occur during the hot path.

### 5.3 Multi-Submesh Rendering & `kAllSubmeshes`

When an imported glTF mesh contains multiple primitives:
- Each primitive maps to a distinct `Submesh` with its own `firstIndex`, `indexCount`, and `materialIndex`.
- `MeshComponent::submeshIndex` defaults to 0 or targets a specific primitive.
- The `kAllSubmeshes` sentinel constant (`~0u`) instructs `SceneRenderer` to iterate and render all primitives of the mesh sequentially, binding the correct material descriptor set for each draw call.

---

## 6. Verification and Test Coverage

1. **`tests/unit/render/gltf_importer_test.cpp`**:
   - `testMaterialFactorAndPropertyParsing`: Verifies factors, alpha cutoff, doubleSided, sampler settings.
   - `testEmbeddedImageDecodingAndResolution`: Verifies GLB embedded image slice decoding, deterministic URI resolution, and texture presence.
2. **`tests/unit/render/asset_manager_test.cpp`**:
   - `testColorSpaceSeparation`: Verifies `#srgb` vs `#linear` cache key isolation.
   - `testFallbackDefaultTextures`: Verifies procedural 1x1 default texture generation.
3. **`tests/unit/render/material_descriptor_test.cpp`**:
   - Tests descriptor layout creation, UBO data layout packing, texture slot resolution, and AssetManager material caching.
4. **`tests/unit/render/scene_renderer_test.cpp`**:
   - `testSceneRendererMultiMaterialSubmeshes`: Hardware Vulkan execution on NVIDIA P106-100 verifying multi-primitive rendering with distinct glTF materials (red and blue) and offscreen pixel readback.
