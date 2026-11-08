# Milestone 8B.5: External 3D Asset Import Pipeline

## 1. Executive Summary

Milestone 8B.5 establishes a production-quality external 3D asset import pipeline for the **Hikari-Machine** game engine, enabling the ingestion of industry-standard 3D models created in Blender, Maya, 3ds Max, and modern digital content creation (DCC) packages.

Prior to this milestone, Hikari-Machine only supported Wavefront OBJ models via `ObjImporter`. Milestone 8B.5 introduces full glTF 2.0 (`.gltf`) and binary glTF (`.glb`) support, complete with:
- Multi-node scene hierarchy traversal with baked world transforms.
- Normal transformation via inverse-transpose matrices ($M^{-T}$).
- Negative scale (reflection/mirroring) detection with triangle winding reversal.
- Non-indexed primitive index buffer generation.
- Robust missing attribute fallbacks (automatic normal calculation and default UV coordinates).
- glTF 2.0 PBR metallic-roughness material extraction and color-space metadata preservation.
- AssetManager integration with asset hashing, caching, GPU mesh upload deduplication, and transactional error recovery.
- Content Browser UI integration with format badges, geometric metadata inspection, and drag-and-drop entity spawning directly onto the Scene Viewport.

All changes strictly adhere to Hikari's C++20 core guidelines: `-fno-exceptions`, `-fno-rtti`, zero external runtime dependencies, deterministic memory allocation, and repository-relative pathing.

---

## 2. Library Evaluation & Selection Rationale

An exhaustive evaluation of external glTF loading libraries was conducted prior to implementation:

| Criteria | `cgltf` (Selected) | `tinygltf` | Custom Hand-Rolled Parser |
| :--- | :--- | :--- | :--- |
| **Language & Dialect** | C99 / C++ compatible single-header | C++11 header-only | C++20 engine-native |
| **Exceptions / RTTI** | Zero exceptions, zero RTTI | Uses `<exception>` and `std::string` throwing | Zero exceptions, zero RTTI |
| **Dependencies** | **0** (self-contained JSON & base64 parsers) | Depends on `nlohmann/json` & `stb_image` | Needs JSON parser |
| **Binary GLB Support** | Built-in, high-performance chunk reader | Built-in | Requires manual binary chunking |
| **Node Hierarchy** | Built-in `cgltf_node_transform_world` | Manual matrix multiplication required | Manual matrix multiplication required |
| **License** | Permissive MIT | Permissive MIT | N/A |
| **Footprint & Compile Time** | ~1 file, single compile unit (~150 ms) | Heavy template instantiation (~2-4 s) | High development maintenance |

### Decision
`cgltf` (v1.14 pinned, MIT license, vendored under `engine/render/src/cgltf/cgltf.h`) was selected as the optimal choice. It operates strictly without exceptions or RTTI, features an internal zero-allocation JSON parser, supports binary GLB chunk loading, base64 data URIs, external buffers, and provides node world transform composition out of the box.

---

## 3. Supported Formats & Capabilities

Hikari-Machine now supports three primary 3D model formats:

1. **Binary glTF (`.glb`)**: Single self-contained file containing JSON scene hierarchy, binary buffer chunks (vertices, normals, tangents, UVs, indices), and embedded textures.
2. **Text glTF (`.gltf`)**: JSON descriptor referencing external `.bin` binary files or inline base64-encoded data URIs (`data:application/octet-stream;base64,...`).
3. **Wavefront OBJ (`.obj`)**: Maintained with full backward compatibility via `ObjImporter`.

---

## 4. Architecture & Pipeline Design

### 4.1 Component Diagram

```
                        External 3D Asset (.glb / .gltf)
                                      |
                                      v
                             +-----------------+
                             |  GltfImporter   |
                             +-----------------+
                                      |
                 +--------------------+--------------------+
                 |                                         |
                 v                                         v
          +--------------+                          +--------------+
          |   MeshData   |                          |  [Material]  |
          +--------------+                          +--------------+
                 |                                         |
                 v                                         v
         +---------------+                         +---------------+
         |  AssetManager |<------------------------+ ContentBrowser|
         +---------------+                         +---------------+
                 |                                         |
                 v                                         v
         +---------------+                         +---------------+
         |    GpuMesh    |                         | Viewport Drag |
         +---------------+                         |    & Drop     |
                 |                                 +---------------+
                 v                                         |
         +---------------+                                 v
         | SceneRenderer |                         +---------------+
         +---------------+                         | World Entity  |
                                                   +---------------+
```

### 4.2 GltfImporter Class (`engine/render/include/render/gltf_importer.h`)

The `GltfImporter` provides a clean static interface:

```cpp
class GltfImporter {
public:
    static Result<MeshData> importFromFile(const std::string& filepath,
                                          const GltfImportOptions& options = {});

    static Result<MeshData> importFromMemory(const uint8_t* data,
                                            size_t size,
                                            const std::string& baseDir = "",
                                            const GltfImportOptions& options = {});
};
```

Configuration options via `GltfImportOptions`:
- `generateNormalsIfMissing`: Automatically synthesizes per-vertex normals using adjacent face averaging if the mesh lacks normal vectors.
- `flipUVs`: Inverts vertical texture coordinates ($V' = 1.0 - V$) for APIs requiring top-left texture origins.
- `importMaterials`: Parses glTF PBR metallic-roughness materials and stores them within `MeshData`.

---

## 5. Coordinate Systems & Geometric Transformations

### 5.1 Coordinate System Alignment
glTF 2.0 specifies a right-handed coordinate system where $+Y$ is up, $+X$ is right, and $-Z$ is the forward view direction.
Hikari-Machine also adopts a right-handed coordinate system with $+Y$ up and $-Z$ forward.
Consequently, **no axis swizzling or axis negation is required** between glTF 2.0 and Hikari engine space.

### 5.2 Node Transform Hierarchy & World Matrix Baking
glTF assets typically organize meshes inside hierarchical node trees containing local translation, rotation (quaternion), and scale (TRS), or $4\times4$ transform matrices.
During import:
1. Every node's full ancestral transform is evaluated to determine its final $4\times4$ world matrix via `cgltf_node_transform_world()`.
2. Positions are transformed into model-space:
   $$\mathbf{P}_{\text{world}} = \mathbf{M}_{\text{node}} \cdot \begin{bmatrix} x \\ y \\ z \\ 1 \end{bmatrix}$$
3. Normals are transformed using the inverse-transpose of the upper $3\times3$ world matrix to guarantee perpendicularity under non-uniform scaling:
   $$\mathbf{N}_{\text{world}} = \operatorname{normalize}\left( (\mathbf{M}_{3\times3}^{-1})^T \cdot \mathbf{N}_{\text{local}} \right)$$

### 5.3 Reflection & Negative Scale Handling
When a node transform contains reflection (an odd number of negative scale factors), the determinant of the $3\times3$ transform is negative ($\det(\mathbf{M}_{3\times3}) < 0$).
Transforming vertices under reflection inverts the polygon winding order, turning counter-clockwise front-facing triangles into clockwise back-facing triangles.
The importer detects this condition and automatically reverses the index winding order for affected triangles:
$$(i_0, i_1, i_2) \longrightarrow (i_0, i_2, i_1)$$
This guarantees proper front-face rendering and back-face culling in the Vulkan graphics pipeline.

---

## 6. Material & Texture Data Model

The core material representation (`engine/render/include/render/material.h`) has been expanded to natively accommodate glTF 2.0 PBR metallic-roughness properties:

```cpp
enum class AlphaMode : uint8_t {
    Opaque = 0,
    Mask   = 1,
    Blend  = 2
};

enum class TextureColorSpace : uint8_t {
    Linear = 0,
    sRGB   = 1
};

struct TextureInfo {
    std::string filepath;
    uint32_t texCoordIndex = 0;
    TextureColorSpace colorSpace = TextureColorSpace::Linear;
    TextureSamplerDesc sampler;
};

struct Material {
    std::string name;
    Vec4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    Vec3 emissiveFactor{0.0f, 0.0f, 0.0f};
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;

    TextureInfo baseColorTexture;
    TextureInfo metallicRoughnessTexture;
    TextureInfo normalTexture;
    TextureInfo occlusionTexture;
    TextureInfo emissiveTexture;
};
```

Color-space metadata is explicitly preserved:
- Base color and emissive textures default to `TextureColorSpace::sRGB`.
- Metallic-roughness, normal maps, and ambient occlusion textures default to `TextureColorSpace::Linear`.

---

## 7. AssetManager & Editor Viewport Integration

### 7.1 AssetManager Routing & Deduplication
`AssetManager::loadMesh(filepath)` inspects the file extension:
- `.gltf` and `.glb` files route directly to `GltfImporter::importFromFile()`.
- `.obj` files route to `ObjImporter::importFromFile()`.
- Unknown formats fail cleanly with `ErrorCode::InvalidArgument`.

Asset IDs are deterministically derived via string hashing (`AssetId::fromString(filepath)`). Multiple requests for the same path return cached `MeshData` without redundant disk access. Similarly, `getOrCreateGpuMesh()` caches `GpuMesh` instances by asset ID, avoiding duplicate Vulkan buffer allocations.

### 7.2 Failure Safety & Transactional Consistency
If file parsing or buffer loading fails at any stage (e.g., malformed JSON, corrupted GLB header, missing external buffer), `AssetManager` discards all temporary allocations and inserts no entries into its internal registry, maintaining atomic consistency.

### 7.3 Content Browser Badging & Inspection
The editor Content Browser (`editor/src/panels/content_browser_panel.cpp`) automatically catalogs `.gltf`, `.glb`, and `.obj` files located within the project assets tree. It presents distinct visual badges:
- `[GLB]` in soft cyan (`#4DB6AC`).
- `[GLTF]` in amber (`#FFB74D`).
- `[OBJ]` in periwinkle (`#9FA8DA`).

Selecting any model file in the Content Browser opens a detailed metadata card displaying:
- Vertex count
- Triangle count
- Submesh count
- Material count and associated names

### 7.4 Drag-and-Drop Viewport Instantiation
Users can drag model assets directly from the Content Browser into the Dear ImGui Scene Viewport:
1. `ContentBrowserPanel` initiates a drag-and-drop payload with type `CONTENT_BROWSER_MESH`.
2. `ViewportPanel` accepts the drop over the viewport canvas.
3. An active entity is spawned at the camera's focus pivot:
   - `PhysicsTransformComponent`: Positioned at camera target, zero rotation, unit scale.
   - `MeshComponent`: Bound to the imported asset ID and submesh indices.
   - `ColliderComponent`: Sized dynamically to match the mesh's computed AABB bounding dimensions.
4. The newly created entity is automatically selected in the Hierarchy and Inspector panels.

---

## 8. Verification & Test Coverage

### 8.1 Automated Unit Tests
A comprehensive test suite was implemented in `tests/unit/render/gltf_importer_test.cpp`, validating all 31 core functional requirements:

1. `testCubeGlbImport`: Validates vertex and triangle counts on basic geometry.
2. `testTriangleGltfExternalBin`: Validates external `.bin` buffer loading.
3. `testVertexAttributes`: Validates positions, normals, and UV coordinate extraction.
4. `testVertexColors`: Validates 4-channel vertex color loading.
5. `testMultiplePrimitivesSubmeshes`: Validates multi-primitive partitioning into submeshes.
6. `testNonIndexedMeshSynthesis`: Validates synthetic index generation for non-indexed primitives.
7. `testBoundingBoxCalculation`: Validates correct AABB extents.
8. `testCoordinateSystemOrientation`: Verifies $+Y$ up and $+Z$ depth orientations.
9. `testNodeHierarchyTransform`: Validates node world transform concatenation.
10. `testNonUniformScalingNormals`: Validates normal transformation via $(M^{-1})^T$.
11. `testNegativeScaleReflection`: Validates winding order reversal under reflection.
12. `testNormalGenerationFallback`: Validates automatic normal generation when omitted.
13. `testMissingUVDefault`: Validates $(0, 0)$ fallback for missing texture coordinates.
14. `testMaterialPbrFactors`: Validates base color, roughness, and metallic parsing.
15. `testEmissiveAndAlphaMode`: Validates emissive factors and mask cutoff.
16. `testTextureMetadataAndColorSpace`: Validates linear vs sRGB color space tagging.
17. `testRelativeTexturePathResolution`: Validates texture URI path resolution.
18. `testEmbeddedGlbTextures`: Validates embedded GLB buffer view references.
19. `testCorruptedGlbHeader`: Rejects invalid GLB magic or truncated headers.
20. `testCorruptedGltfJson`: Rejects malformed JSON syntax.
21. `testMissingExternalBin`: Rejects missing external buffer files cleanly.
22. `testUnsupportedGltfVersion`: Rejects legacy glTF 1.0 files.
23. `testInvalidIndices`: Detects and rejects out-of-bounds vertex index references.
24. `testLargeMeshImport`: Validates high-poly geometry imports (3,000+ vertices).
25. `testAssetManagerGltfRouting`: Validates format routing inside `AssetManager`.
26. `testAssetManagerDeduplication`: Validates CPU asset caching.
27. `testGpuMeshUpload`: Validates Vulkan GPU vertex and index buffer allocations.
28. `testObjImporterPreserved`: Verifies Wavefront OBJ loader functionality is intact.
29. `testCleanStateOnFailure`: Verifies zero residue in `AssetManager` after load errors.
30. `testHikariSerializationRoundTrip`: Verifies `.hikari` scene save/load with glTF assets.
31. `testContentBrowserDiscovery`: Verifies asset discovery and metadata extraction.

### 8.2 Multi-Preset Test Results

All test targets passed across all 6 CMake configuration presets:

| CMake Preset | Test Suite Result | Status |
| :--- | :--- | :--- |
| `editor` | 34 / 34 Passed | **PASS** |
| `debug` | 33 / 33 Passed | **PASS** |
| `release` | 33 / 33 Passed | **PASS** |
| `profile` | 33 / 33 Passed | **PASS** |
| `asan` (AddressSanitizer) | 33 / 33 Passed (0 leaks, 0 errors) | **PASS** |
| `tsan` (ThreadSanitizer) | 33 / 33 Passed (0 race conditions) | **PASS** |

### 8.3 Physical GUI Verification
The editor was executed against the physical GPU for 180 continuous simulation frames:
```bash
./build/editor/bin/hikari --frames 180
```
- Dear ImGui dynamic rendering swapchain presentation completed without validation errors.
- Telemetry confirmed GPU draw calls, vertex uploads, and descriptor allocations.
- Workspace docking state was restored and saved cleanly.
- Editor shut down cleanly with 0 errors, warnings, or memory leaks.

---

## 9. File Modifications & Additions

### New Files:
- `engine/render/src/cgltf/cgltf.h`: Vendored single-header glTF 2.0 loader library.
- `engine/render/include/render/gltf_importer.h`: Public glTF importer interface and options.
- `engine/render/src/gltf_importer.cpp`: Core glTF 2.0 importer implementation.
- `tests/unit/render/gltf_importer_test.cpp`: Automated unit test suite (31 tests).
- `assets/models/cube.glb`: Bundled sample asset for editor discovery and viewport testing.
- `tests/fixtures/gltf/*`: Deterministic glTF and GLB test fixtures.

### Modified Files:
- `engine/render/include/render/material.h`: Added PBR metallic-roughness factors, alpha modes, texture metadata, and color spaces.
- `engine/render/include/render/mesh.h`: Added material index to `Submesh` and material list to `MeshData`.
- `engine/render/include/render/asset_manager.h`: Added glTF routing, material accessors, and error handling.
- `engine/render/src/asset_manager.cpp`: Implemented format dispatch and material retrieval.
- `engine/render/CMakeLists.txt`: Configured `cgltf` include paths and compilation rules.
- `editor/src/panels/content_browser_panel.cpp`: Added glTF/GLB badges, inspection UI, and drag-and-drop sources.
- `editor/src/panels/viewport_panel.cpp`: Added drag-and-drop target to spawn mesh entities.
- `tests/CMakeLists.txt`: Added `gltf_importer_test` target.
