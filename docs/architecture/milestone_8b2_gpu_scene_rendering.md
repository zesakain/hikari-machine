# Milestone 8B.2: Viewport GPU Scene Rendering Integration

## 1. Architectural Objective

Milestone 8B.2 integrates the RHI graphics pipeline (Milestone 8A), GPU mesh buffer infrastructure (Milestone 7), and offscreen dynamic rendering infrastructure (Milestone 8B.1) into a real GPU scene rendering path.

The goal of this milestone is to establish **genuine GPU geometry rendering**:
- Vertices and indices are uploaded to GPU buffers and processed by a real Vulkan 1.3 dynamic graphics pipeline.
- Camera transformations and entity model transforms are supplied through standard 128-byte push constants (`mat4 viewProj` + `mat4 model`).
- Lighting is calculated in fragment shaders with directional diffuse illumination and ambient shading.
- Output is rendered into an offscreen color target (`RGBA8_UNORM`) with depth testing and writing against a depth target (`D32_FLOAT`).
- Rendered pixels and depth occlusion are verified directly on physical GPU hardware via offscreen texture-to-buffer readback.
- The existing CPU rasterizer in `editor/src/panels/viewport_panel.cpp` remains untouched and operational as an independent fallback.
- Viewport presentation into Dear ImGui panels is cleanly decoupled and deferred to Milestone 8B.3.

---

## 2. Architectural Audit Findings Addressed

### Finding F-03: Draw Call Suppression on Format Mismatch
- **Issue**: In Milestone 8B.1, when a pipeline format mismatch was detected against an active dynamic render pass, warning logs were emitted but draw commands were not explicitly suppressed via early return.
- **Resolution**:
  - `VulkanCommandList::draw()` and `VulkanCommandList::drawIndexed()` now check `hasFormatMismatch_` and immediately return, preventing invalid draw submissions to Vulkan.
  - `NullCommandList::draw()` and `NullCommandList::drawIndexed()` enforce matching early returns, preventing increment of `drawCount_` and `drawIndexedCount_`.
  - Regression unit tests added to both `null_rhi_test.cpp` and `vulkan_rhi_test.cpp` validating that draw counts remain zero on mismatch.

### Finding F-04: Target Reallocation Synchronization
- **Issue**: Destroying and reallocating offscreen textures during viewport resize without GPU synchronization could invalidate textures referenced by pending command buffers.
- **Resolution**:
  - `SceneRenderer::resize(uint32_t width, uint32_t height)` now explicitly invokes `device_->waitIdle()` prior to destroying old color and depth texture handles.
  - Target recreation preserves existing format configuration (`colorFormat_`, `depthFormat_`).

---

## 3. Shader Architecture & SPIR-V Bytecodes

### Shaders
1. **Vertex Shader (`assets/shaders/mesh.vert`)**:
   - Inputs:
     - Location 0: `inPosition` (`vec3`)
     - Location 1: `inNormal` (`vec3`)
     - Location 2: `inUV` (`vec2`)
     - Location 3: `inColor` (`vec4`)
   - Push Constants (128 bytes total, Vertex stage):
     - Offset 0 (64 bytes): `mat4 viewProj`
     - Offset 64 (64 bytes): `mat4 model`
   - Transforms:
     - `gl_Position = pc.viewProj * (pc.model * vec4(inPosition, 1.0));`
     - Computes normal matrix `transpose(inverse(mat3(pc.model)))` for normal transformation.
2. **Fragment Shader (`assets/shaders/mesh.frag`)**:
   - Directional light: `lightDir = normalize(vec3(0.5, 0.8, 0.6))`
   - Diffuse term: `max(dot(normal, lightDir), 0.0)`
   - Ambient term: `0.25`
   - Shading factor: `0.25 + 0.75 * diff`
   - Color selection: Uses `inColor` when present; falls back to default surface slate `vec3(0.7, 0.75, 0.85)` if uncolored.
   - Output: `RGBA8_UNORM` color attachment 0.

### Precompiled SPIR-V Header
- Precompiled with `glslc` and validated with `spirv-val`.
- Embedded into `engine/render/include/render/mesh_shader_spirv.h`:
  - `kMeshVertexShaderSpirv`: 570 words (2280 bytes).
  - `kMeshFragmentShaderSpirv`: 425 words (1700 bytes).

---

## 4. `SceneRenderer` Design & Implementation

`SceneRenderer` (`engine/render/include/render/scene_renderer.h` and `engine/render/src/scene_renderer.cpp`) manages the GPU scene rendering lifecycle:

### Lifecycle & Resource Management
- **`init(device, width, height, colorFormat, depthFormat)`**:
  - Allocates offscreen color texture (`ColorAttachment | Sampled | TransferSrc`).
  - Allocates offscreen depth texture (`DepthStencilAttachment`).
  - Creates vertex and fragment `ShaderHandle`s from embedded SPIR-V bytecodes.
  - Creates `PipelineLayoutHandle` with 128-byte push constants on `ShaderStageFlags::Vertex`.
  - Creates `PipelineHandle` specifying 4-attribute vertex layout (`stride = 48`), triangle list topology, back-face culling, depth test/write enabled (`CompareOp::Less`), and attachment formats.
- **`shutdown()`**:
  - Releases GPU pipeline, layout, shaders, and textures in reverse creation order.
- **`resize(width, height)`**:
  - Synchronizes with `device_->waitIdle()`.
  - Reallocates color and depth textures to new dimensions.

### Scene Rendering Workflow (`renderScene`)
```
[World / AssetManager]
       │
       ▼
[SceneRenderer::renderScene]
       │
       ├─► beginRenderPass(colorTarget, depthTarget, clearColor={0.08,0.12,0.20,1.0}, clearDepth=1.0)
       ├─► setViewport(0, 0, width, height) & setScissor(0, 0, width, height)
       ├─► bindPipeline(meshGraphicsPipeline)
       │
       ├─► For each entity with MeshComponent & visible == true:
       │     ├─► Query physics::PhysicsTransformComponent -> toMatrix() (or Mat4::identity())
       │     ├─► Pack 128B: [viewProj (64B) | model (64B)]
       │     ├─► pushConstants(pipelineLayout, Vertex, 0, 128, pushData)
       │     ├─► Upload GpuMesh if needed
       │     ├─► bindVertexBuffer(0, vertexBuffer, 0)
       │     ├─► bindIndexBuffer(indexBuffer, 0, true)
       │     └─► drawIndexed(indexCount, ...) or draw(vertexCount, ...)
       │
       └─► endRenderPass() -> transitions colorTarget to SHADER_READ_ONLY_OPTIMAL
```

---

## 5. GPU Readback & Verification

To verify that meshes are truly rendered by the physical GPU hardware, two additions were made to the RHI:
1. **`RhiCommandList::copyTextureToBuffer(TextureHandle srcTexture, BufferHandle dstBuffer)`**:
   - Inserts `VkImageMemoryBarrier2` transitioning texture to `VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL`.
   - Records `vkCmdCopyImageToBuffer` for mip 0.
   - Restores texture layout to `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`.
2. **`RhiDevice::readBuffer(BufferHandle handle, size_t offset, void* outData, size_t size)`**:
   - Copies from host-visible `CpuToGpu` mapped memory back to user host buffer.

### Hardware Pixel Verification (`scene_renderer_test.cpp`)
- Initializes `SceneRenderer` at 64x64 on physical Vulkan device.
- Renders `assets/models/cube.obj` centered at origin from camera at `(0, 0, 2.5)` with 60° FOV.
- Copies rendered offscreen texture to readback buffer and inspects pixel bytes:
  1. **Corner pixels `(2, 2)`**: Background clear color `(0.08, 0.12, 0.20, 1.0)` -> R in [16..26], G in [26..36], B in [45..57], A = 255.
  2. **Center pixel `(32, 32)`**: Hit by front face of cube. Validated to be significantly different from clear color (`delta > 15`) and fully opaque (`A = 255`).
  3. **Depth Occlusion**: Adds second cube at `(0, 0, -2.0)`. Verifies center pixel values remain identical, confirming depth buffer occlusion prevents overdraw.
  4. **Target Resizing**: Resizes renderer to 128x128 with `waitIdle()` synchronization.

---

## 6. Multi-Preset Test Results

All 6 CMake presets build cleanly and pass 100% of tests:

| Preset | Tests Total | Tests Passed | Pass Rate | Key Verifications |
|:---|:---:|:---:|:---:|:---|
| `debug` | 32 | 32 | 100% | Full debug assertions, null & vulkan RHI, scene renderer |
| `release` | 32 | 32 | 100% | Optimized release binaries |
| `profile` | 32 | 32 | 100% | Profiling instrumentation enabled |
| `editor` | 33 | 33 | 100% | Editor UI + SceneRenderer + Viewport CPU rasterizer |
| `asan` | 32 | 32 | 100% | AddressSanitizer clean, 0 memory leaks |
| `tsan` | 32 | 32 | 100% | ThreadSanitizer clean, 0 data races |

---

## 7. Deferred to Milestone 8B.3

- **ImGui Viewport Presentation**:
  - Sampling `SceneRenderer::getColorTarget()` through Vulkan descriptor sets and Dear ImGui `ImTextureID`.
  - Viewport panel resizing binding and interactive camera integration.
- The CPU rasterizer in `editor/src/panels/viewport_panel.cpp` remains intact and unaffected until Milestone 8B.3 completes.
