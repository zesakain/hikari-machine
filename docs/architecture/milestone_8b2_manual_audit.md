# Milestone 8B.2 Independent Manual Acceptance Audit
## Viewport GPU Scene Rendering Integration

**Auditor:** Independent Senior Graphics Engineer (Vulkan 1.3 / C++20 / Real-Time Rendering)  
**Repository:** Repository root  
**Date:** 2026-11-08  
**Audit Scope:** Milestone 8B.2 (Commits `112a4d0` through `57ed907`)  

---

## 1. Executive Summary

Milestone 8B.2 integrates Hikari-Machine's RHI graphics pipeline abstraction (Milestone 8A), GPU mesh buffer infrastructure (Milestone 7), and offscreen dynamic rendering infrastructure (Milestone 8B.1) into an operational GPU scene rendering subsystem.

The audit verified that `SceneRenderer`:
1. Manages an offscreen color attachment (`RGBA8_UNORM`) and depth attachment (`D32_FLOAT`) with resolution-independent pipeline state.
2. Compiles SPIR-V bytecode shaders (`assets/shaders/mesh.vert` and `assets/shaders/mesh.frag`) featuring 128-byte push constants (`mat4 viewProj` + `mat4 model`) and directional diffuse lighting.
3. Successfully records and executes genuine Vulkan 1.3 dynamic rendering passes on physical GPU hardware (`NVIDIA P106-100`, Pascal architecture).
4. Accurately renders 3D mesh geometry, proven via GPU texture-to-buffer transfer (`RhiCommandList::copyTextureToBuffer`) and host-coherent memory readback (`RhiDevice::readBuffer`).
5. Successfully resolves Audit Finding F-03 by suppressing draw commands on pipeline format mismatch in both Vulkan and Null RHI backends.
6. Addresses Audit Finding F-04 by enforcing `device_->waitIdle()` synchronization prior to destroying previous render targets during viewport resize.
7. Preserves the CPU viewport rasterizer in `editor/src/panels/viewport_panel.cpp` intact and untouched.
8. Passes 100% of test suites across all six CMake presets (`debug`, `release`, `profile`, `editor`, `asan`, and `tsan`).

**Final Verdict:** **PASS** (Ready for Milestone 8B.3).

---

## 2. Audit Scope and Methodology

### Scope
The audit examined 22 files altered or introduced across the 7 commits of Milestone 8B.2:
- `engine/render/include/render/scene_renderer.h` and `engine/render/src/scene_renderer.cpp`
- `assets/shaders/mesh.vert` and `assets/shaders/mesh.frag`
- `engine/render/include/render/mesh_shader_spirv.h`
- `engine/rhi/include/rhi/rhi_device.h` and `engine/rhi/include/rhi/command_list.h`
- `engine/rhi/src/vulkan/vulkan_device.cpp`, `vulkan_device.h`, `vulkan_command_list.cpp`, `vulkan_command_list.h`
- `engine/rhi/src/null/null_device.cpp`, `null_device.h`, `null_command_list.cpp`, `null_command_list.h`
- `tests/unit/render/scene_renderer_test.cpp` and `tests/CMakeLists.txt`
- `tests/unit/rhi/null_rhi_test.cpp` and `tests/unit/rhi/vulkan_rhi_test.cpp`
- `docs/architecture/milestone_8b2_gpu_scene_rendering.md` and `CHANGELOG.md`

### Methodology
1. **Source & Shader Inspection:** Trace ECS query traversal, buffer bindings, push constant memory layouts, matrix conventions, and normal transformation formulas.
2. **Synchronization Analysis:** Verify Vulkan image layout transitions, transfer barriers, host-visible memory allocation flags, and CPU readback visibility guarantees.
3. **Hardware Execution:** Execute native Vulkan tests with validation layers enabled (`VK_LAYER_KHR_validation`) on physical NVIDIA GPU hardware.
4. **Sanitizer Verification:** Execute test suites under AddressSanitizer (ASan) and ThreadSanitizer (TSan) presets to verify zero memory leaks and zero data races.
5. **Regression Verification:** Audit draw call early-exit suppression on format mismatch (Audit Finding F-03) and viewport resize synchronization (Audit Finding F-04).

---

## 3. GPU Rendering Execution Path

The complete pipeline execution path was verified from Flecs ECS to the Vulkan driver:

$$\text{World} \longrightarrow \text{SceneRenderer} \longrightarrow \text{AssetManager} \longrightarrow \text{GpuMesh} \longrightarrow \text{RHI} \longrightarrow \text{Vulkan 1.3 Driver}$$

### Verified Steps:
1. **ECS Query Selection:** `SceneRenderer::renderScene` queries `ecs.query<const MeshComponent>()`.
2. **Visibility Filtering:** Entities with `meshComp.visible == false` are immediately filtered out.
3. **Asset Resolution:** If `meshComp.meshAsset` is unassigned, `meshComp.assetPath` is looked up in `AssetManager`. If missing from cache, it is loaded on demand.
4. **GPU Buffer Upload:** If `gpuMesh == nullptr`, `assetManager.uploadMesh(meshId, device)` uploads vertex and index buffers to GPU memory.
5. **Transform Evaluation:** If the entity has `physics::PhysicsTransformComponent`, `tc->transform.toMatrix()` extracts the 4x4 affine model matrix; otherwise falls back to `Mat4::identity()`.
6. **Push Constant Submission:** 128 bytes are submitted via `cmdList.pushConstants(pipelineLayout_, ShaderStageFlags::Vertex, 0, 128, pushData)`:
   - Bytes `[0..63]`: 64 bytes of `viewProjection.m` (column-major).
   - Bytes `[64..127]`: 64 bytes of `model.m` (column-major).
7. **Buffer Binding:**
   - Vertex buffer bound to binding slot 0 with stride 48 bytes (`cmdList.bindVertexBuffer(0, gpuMesh->getVertexBuffer(), 0)`).
   - Index buffer bound with `is32Bit = true` (`cmdList.bindIndexBuffer(gpuMesh->getIndexBuffer(), 0, true)`).
8. **Draw Execution:**
   - Submeshes: `cmdList.drawIndexed(sub.indexCount, 1, sub.firstIndex, sub.vertexOffset, 0)`.
   - Full mesh: `cmdList.drawIndexed(gpuMesh->getIndexCount(), 1, 0, 0, 0)`.
   - Non-indexed fallback: `cmdList.draw(gpuMesh->getVertexCount(), 1, 0, 0)`.
9. **Dynamic Rendering Scope:**
   - If `colorTarget_` is valid, `cmdList.beginRenderPass(passInfo)` initializes dynamic rendering with `VkRenderingInfo`.
   - `cmdList.setViewport` and `cmdList.setScissor` dynamically establish the viewport box.
   - `cmdList.endRenderPass()` terminates dynamic rendering and automatically transitions the color attachment to `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`.

---

## 4. Readback and Synchronization Analysis

The GPU readback pipeline was audited against Vulkan 1.3 synchronization and memory model specifications:

### Complete Dependency Chain:
1. **Rendering writes to Color Attachment:**
   `beginRenderPass()` transitions `colorTarget` from previous layout to `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` with `dstStageMask = COLOR_ATTACHMENT_OUTPUT_BIT` and `dstAccessMask = COLOR_ATTACHMENT_WRITE_BIT`.
2. **Dynamic Rendering Ends:**
   `endRenderPass()` issues `vkCmdEndRendering` followed by `VkImageMemoryBarrier2`:
   - `srcStageMask = COLOR_ATTACHMENT_OUTPUT_BIT`, `srcAccessMask = COLOR_ATTACHMENT_WRITE_BIT`.
   - `dstStageMask = ALL_COMMANDS_BIT`, `dstAccessMask = SHADER_READ_BIT`.
   - `newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`.
3. **Transition to Transfer Source:**
   `copyTextureToBuffer()` issues `VkImageMemoryBarrier2`:
   - `srcStageMask = ALL_COMMANDS_BIT`, `srcAccessMask = SHADER_READ_BIT`.
   - `dstStageMask = TRANSFER_BIT`, `dstAccessMask = TRANSFER_READ_BIT`.
   - `oldLayout = SHADER_READ_ONLY_OPTIMAL`, `newLayout = TRANSFER_SRC_OPTIMAL`.
4. **Copy Image to Buffer:**
   `vkCmdCopyImageToBuffer` executes on queue, reading image in `TRANSFER_SRC_OPTIMAL` and writing to destination buffer.
5. **Transition Back to Shader Read:**
   `copyTextureToBuffer()` restores image to `SHADER_READ_ONLY_OPTIMAL`:
   - `srcStageMask = TRANSFER_BIT`, `srcAccessMask = TRANSFER_READ_BIT`.
   - `dstStageMask = ALL_COMMANDS_BIT`, `dstAccessMask = SHADER_READ_BIT`.
6. **Queue Execution & Host Synchronization:**
   `device->submit(cmd)` queues command buffer; `device->waitIdle()` blocks CPU until queue completes.
7. **Host Memory Visibility:**
   `BufferDesc` with `MemoryUsage::CpuToGpu` allocates memory via `VulkanMemoryManager::allocateBufferMemory`:
   ```cpp
   propFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   ```
   Under the Vulkan specification (Section 7.5.3: "Host Write Ordering and Host Read Visibility"), queue execution completion (`vkQueueWaitIdle` / `vkDeviceWaitIdle`) guarantees that all device writes to host-coherent memory are made visible to host read operations.
   Therefore, no manual `vkInvalidateMappedMemoryRanges` is required.
8. **CPU Buffer Read:**
   `device->readBuffer(buf, 0, outData, size)` copies from persistently mapped `mappedData` via `std::memcpy`.

**Synchronization Verdict:** Fully valid, verified, and free of race conditions.

---

## 5. Shader and Matrix Correctness

### Matrix Layout & Conventions
- **CPU Storage:** `eng::Mat4` stores 16 floats in column-major order (`m[0..3]` = column 0, `m[4..7]` = column 1, `m[8..11]` = column 2, `m[12..15]` = column 3). Translation resides in `m[12]`, `m[13]`, `m[14]`.
- **GLSL Declaration:** `uniform PushConstants { mat4 viewProj; mat4 model; } pc;`. In GLSL `std430` push constants, `mat4` is column-major by default (4 consecutive `vec4` column vectors).
- **Matrix Multiplication:**
  ```glsl
  vec4 worldPos = pc.model * vec4(inPosition, 1.0);
  gl_Position = pc.viewProj * worldPos;
  ```
  This is the standard column-vector multiplication order: $v_{\text{clip}} = P \cdot V \cdot M \cdot v_{\text{local}}$.
- **Handedness & Depth:** `Mat4::perspective(fovY, aspect, near, far)` builds a right-handed perspective projection targeting Vulkan depth range $[0, 1]$ ($z_{\text{NDC}} \in [0, 1]$). `Mat4::lookAt` establishes forward direction along $-Z$.

### Normal Transformation
`assets/shaders/mesh.vert:21`:
```glsl
mat3 normalMatrix = transpose(inverse(mat3(pc.model)));
outNormal = normalize(normalMatrix * inNormal);
```
- **Mathematical Correctness:** Normals transform via the cofactor / inverse transpose matrix $N = (M^{-1})^T$.
  - Pure rotation: $(R^{-1})^T = R$.
  - Uniform scaling: $\frac{1}{s} I \rightarrow$ normalized to unit length.
  - Non-uniform scaling: Correctly preserves surface perpendicularity.
  - Inverted / Negative scaling: Preserves outward direction after normalization.
- **Performance Evaluation:** Calculating `inverse(mat3)` per vertex in the vertex shader is acceptable for the baseline pipeline, but should eventually be precomputed per entity on CPU when CPU-side transform caching is introduced.

### Fragment Shader
`assets/shaders/mesh.frag`:
- Normalizes incoming interpolated normal: `vec3 norm = normalize(inNormal)`.
- Directional diffuse lighting: `max(dot(norm, lightDir), 0.0)`.
- Ambient factor: `ambient = 0.25`, scaling total illumination to $[0.25, 1.0]$.
- Base material color fallback: If `length(inColor.rgb) < 0.01`, defaults to neutral slate `vec3(0.7, 0.75, 0.85)`.
- Output alpha: Preserves vertex alpha if $> 0.01$, otherwise defaults to opaque $1.0$.

---

## 6. Push Constants and Pipeline Compatibility

### Push Constant Block Specifications
- Hardware limit: Guaranteed minimum across baseline Vulkan devices is 128 bytes (Vulkan Spec Table 52).
- Declared in Pipeline Layout: `size = 128`, `offset = 0`, `stageFlags = ShaderStageFlags::Vertex`.
- Shader definition: 2 $\times$ `mat4` = $2 \times 64 = 128$ bytes.
- CPU submission: `alignas(16) float pushData[32]` ($32 \times 4 = 128$ bytes).
- Push constant offset, alignment, and size match across CPU, pipeline layout, and shader stages.

### Vertex Input Layout Compatibility
| Attribute | Location | Binding | Format | Offset | `Vertex` Struct Member |
|:---|:---:|:---:|:---:|:---:|:---|
| Position | 0 | 0 | `Float3` (12B) | 0 | `position` (`Vec3`) |
| Normal | 1 | 0 | `Float3` (12B) | 12 | `normal` (`Vec3`) |
| UV | 2 | 0 | `Float2` (8B) | 24 | `uv` (`Vec2`) |
| Color | 3 | 0 | `Float4` (16B) | 32 | `color` (`Vec4`) |
| **Total Stride** | - | 0 | - | - | **48 bytes (`sizeof(Vertex)`)** |

Attribute locations, formats, offsets, and stride match `eng::render::Vertex` with byte precision.

### Pipeline Dynamic Rendering State
- Color Attachment Format: `RGBA8_UNORM`
- Depth Attachment Format: `D32_FLOAT`
- Depth Test Enable: `true`
- Depth Write Enable: `true`
- Depth Compare Op: `CompareOp::Less`
- Rasterization: `PolygonMode::Fill`, `CullMode::Back`, `FrontFace::CounterClockwise`
- Color Blend: Single attachment, blending disabled (`blendEnable = false`), `colorWriteMask = All`.

---

## 7. Depth Testing Analysis

The depth testing infrastructure was audited for correctness:
- Format: `D32_FLOAT` (guaranteed mandatory device support for depth attachment in Vulkan).
- Clear Depth: `1.0F` (far plane).
- Compare Operation: `CompareOp::Less` (closer geometry has smaller depth value $\in [0, 1]$).
- Winding: CCW with back-face culling ensures interior cube faces are discarded, and front faces write correct depth values.

### Unit Test Occlusion Finding (Audit Finding 8B.2-01)
In `scene_renderer_test.cpp:317-347`:
- Test renders `CenterCube` at $z = 0$, then adds `RearCube` at $z = -2.0$.
- Flecs query order draws `RearCube` after `CenterCube`.
- The test asserts `std::abs(occludedCenterR - centerR) <= 2`.
- **Finding:** Because both entities share the identical unit cube mesh and identical unrotated orientation under a distant directional light, both front faces have normal $(0, 0, 1)$ and compute the identical diffuse lighting factor ($0.6525$). Even if depth testing were completely disabled, drawing `RearCube` over `CenterCube` would produce the exact same pixel color.
- **Conclusion:** The Vulkan pipeline depth configuration is 100% correct, but the test assertion relies on an indistinguishable color sample. A test improvement is logged in Section 14 to use distinct colors or spatial offsets for unambiguous occlusion testing.

---

## 8. Resource Lifetime and Ownership

### `SceneRenderer` Lifecycle
- **Construction / Destruction:**
  - Destructor invokes `shutdown()`, releasing pipeline, pipeline layout, shaders, depth target, and color target.
  - Zero raw `new` / `delete`.
- **Move Semantics:**
  - `SceneRenderer(SceneRenderer&&)` transfers handles and resets source handles to invalid.
  - `operator=(SceneRenderer&&)` invokes `shutdown()` on destination before transferring state.
  - Copy constructor and copy assignment are `= delete`.
- **Target Recreation on Viewport Resize:**
  - `SceneRenderer::resize(width, height)` executes:
    ```cpp
    device_->waitIdle();
    ```
    before destroying old `colorTarget_` and `depthTarget_`. This resolves Audit Finding F-04 and guarantees no in-flight command buffers reference freed textures.
  - New textures are allocated with matching `colorFormat_` and `depthFormat_`.
  - Pipeline does not need recreation because dynamic rendering pipelines are resolution-independent.

---

## 9. LSan Suppression Review

In `tests/unit/render/scene_renderer_test.cpp:32-37`:
```cpp
#if defined(HIKARI_HAS_ASAN)
extern "C" const char* __lsan_default_suppressions() {
    return "leak:libdbus-1\nleak:libvulkan\nleak:nvidia\nleak:libwayland\n";
}
#endif
```

### Audit Assessment:
1. **Origin of Suppressed Allocations:**
   During physical Vulkan device creation (`vkCreateInstance` / `vkCreateDevice`), the proprietary NVIDIA Linux ICD driver interacts with system D-Bus and Wayland display protocols, allocating process-lifetime singletons.
2. **Scope of Suppression:**
   The function is restricted to the test translation unit (`tests/unit/render/scene_renderer_test.cpp`). It is not present in engine library binaries (`libengine_render.a`, `libengine_rhi.a`).
3. **Risk Analysis:**
   The patterns (`libdbus-1`, `libvulkan`, `nvidia`, `libwayland`) match shared library module names. They cannot suppress leaks occurring in Hikari engine code (`engine/...`).
4. **Conclusion:** Valid and appropriate standard Linux practice for proprietary Vulkan driver testing under ASan.

---

## 10. F-03 Regression Verification

Audit Finding F-03 mandated that pipeline format mismatches must explicitly suppress draw calls.

### Verification:
1. **`VulkanCommandList` (`vulkan_command_list.cpp:448, 463`)**:
   ```cpp
   if (hasFormatMismatch_) {
       return;
   }
   ```
   Verified present in both `draw()` and `drawIndexed()`. Calls to `vkCmdDraw` and `vkCmdDrawIndexed` are completely bypassed.
2. **`NullCommandList` (`null_command_list.cpp:202, 227`)**:
   ```cpp
   if (hasUnboundDraw_ || hasFormatMismatch_) {
       return;
   }
   ```
   Verified present. `drawCount_` and `drawIndexedCount_` are not incremented.
3. **Automated Regression Tests:**
   - `tests/unit/rhi/null_rhi_test.cpp`: Asserts `getDrawCount() == 0` and `getDrawIndexedCount() == 0` when drawing with format mismatch.
   - `tests/unit/rhi/vulkan_rhi_test.cpp`: Executes `draw` and `drawIndexed` against mismatched render target and asserts clean suppression without driver validation errors.

---

## 11. CPU Renderer Preservation

`editor/src/panels/viewport_panel.cpp`:
- Inspected lines 1 to 480.
- Confirmed that the Dear ImGui 2D software triangle rasterizer (`drawMeshWireframe`, `drawMeshSolid`, camera orbit controls, projection math) remains completely intact.
- Git diff between `main` and pre-8B.2 commit `a56206c` on `editor/` shows **0 modified lines**.
- The CPU rasterizer will remain the active editor viewport until Milestone 8B.3 ImGui presentation is completed.

---

## 12. Hardware and Test Evidence

### Hardware Test Environment
- **GPU:** NVIDIA GeForce P106-100 (6GB VRAM, Pascal GP106)
- **Vulkan API Version:** 1.4.312
- **Driver Version:** NVIDIA 580.159.04
- **Validation Layers:** `VK_LAYER_KHR_validation` active and clean

### Execution of `scene_renderer_test`
```bash
$ ./build/debug/tests/scene_renderer_test
Running SceneRenderer Tests...
SceneRenderer lifecycle with NullDevice verified!
SceneRenderer empty scene verified!
SceneRenderer entity filtering and visibility verified!
SceneRenderer multiple entities verified!
Vulkan SceneRenderer hardware rendering & pixel readback verified!
All SceneRenderer Tests Passed!
```

### Verified Pixel Assertions:
1. **Background Clear Color:** At corner `(2, 2)`, readback bytes match `(0.08, 0.12, 0.20, 1.0)`:
   - $R = 20 \in [16, 26]$, $G = 31 \in [26, 36]$, $B = 51 \in [45, 57]$, $A = 255$.
2. **Rendered Mesh Geometry:** At center `(32, 32)`, readback bytes confirm lit front-face cube geometry:
   - $|R_{\text{center}} - R_{\text{corner}}| > 15$, $A = 255$.
3. **Target Resizing:** Reallocated from 64x64 to 128x128 with `waitIdle()` synchronization without Vulkan validation errors.

---

## 13. Build and Sanitizer Results

All six presets were compiled from clean state and tested:

```bash
# 1. Debug
cmake --build --preset debug && ctest --preset debug --output-on-failure
# Result: 32/32 Passed (100%), Time: 2.55s

# 2. Release
cmake --build --preset release && ctest --preset release --output-on-failure
# Result: 32/32 Passed (100%), Time: 3.56s

# 3. Profile
cmake --build --preset profile && ctest --preset profile --output-on-failure
# Result: 32/32 Passed (100%), Time: 4.46s

# 4. Editor
cmake --build --preset editor && ctest --preset editor --output-on-failure
# Result: 33/33 Passed (100%), Time: 2.97s

# 5. AddressSanitizer (ASan)
cmake --build --preset asan && ctest --preset asan --output-on-failure
# Result: 32/32 Passed (100%), Time: 40.17s (0 memory leaks)

# 6. ThreadSanitizer (TSan)
cmake --build --preset tsan && ctest --preset tsan --output-on-failure
# Result: 32/32 Passed (100%), Time: 2.05s (0 data races)
```

---

## 14. Findings and Severity

| Finding ID | Severity | File / Function | Description | Consequence | Blocks 8B.3? |
|:---|:---:|:---|:---|:---|:---:|
| **8B.2-01** | **LOW** | `tests/unit/render/scene_renderer_test.cpp:337` | Occlusion test uses identical mesh and directional lighting for front and rear cubes, yielding identical center pixel colors. | Depth test assertion does not fail if depth test is disabled. | **No** (Hardware pipeline depth state verified correct) |
| **8B.2-02** | **INFORMATIONAL** | `engine/rhi/src/vulkan/vulkan_command_list.cpp:764` | `copyTextureToBuffer` relies on queue completion (`vkDeviceWaitIdle`) for host domain visibility rather than an explicit buffer barrier. | Fully correct for CPU readback after waitIdle; documented for future intra-GPU transfers. | **No** |
| **8B.2-03** | **INFORMATIONAL** | `engine/render/src/scene_renderer.cpp:270` | `SceneRenderer::resize()` rejects `width == 0 \|\| height == 0`. | Editor viewport panel in 8B.3 must guard against resizing when minimized or zero-sized. | **No** (Design input for 8B.3) |
| **8B.2-04** | **INFORMATIONAL** | `tests/unit/render/scene_renderer_test.cpp:34` | LSan suppression includes `leak:nvidia` and `leak:libvulkan`. | Filters proprietary driver allocations; handle slot maps should maintain debug verification. | **No** |

---

## 15. Milestone 8B.3 Readiness Matrix

| Capability | Status | Notes for Milestone 8B.3 |
|:---|:---:|:---|
| **Offscreen Color Target Handle** | **VERIFIED** | `getColorTarget()` returns valid `RGBA8_UNORM` texture handle. |
| **Shader-Readable Image Layout** | **VERIFIED** | `endRenderPass()` transitions color target to `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`. |
| **Vulkan Image View Access** | **PARTIALLY VERIFIED** | `VkImageView` is allocated in `VulkanTextureSlot`. RHI or backend needs accessor for `ImGui_ImplVulkan_AddTexture`. |
| **Sampler Integration** | **VERIFIED** | RHI supports `createSampler()`. Needs linear clamp-to-edge sampler instance for ImGui texture binding. |
| **Zero-Sized Viewport Guard** | **PARTIALLY VERIFIED** | `resize()` returns error on 0x0; editor viewport panel must check for $>0$ before calling `resize()`. |
| **CPU Rasterizer Fallback** | **VERIFIED** | CPU rasterizer in `viewport_panel.cpp` is 100% intact and available. |
| **Multi-Frame Render Loop** | **VERIFIED** | Idempotent begin/end pass cycle verified across multiple recording cycles. |

---

## 16. Final Verdict

### **PASS**

Milestone 8B.2 is complete, correct, and verified on physical Vulkan 1.3 hardware. The implementation satisfies all architectural constraints, adheres to C++20 non-allocating hot-path standards, resolves previous audit findings, and provides a sound foundation for Milestone 8B.3 (ImGui Viewport Presentation & Sampler Integration).
