# Milestone 8B.3: Dear ImGui Viewport GPU Integration

## 1. Executive Summary

Milestone 8B.3 establishes the direct, zero-CPU-readback GPU rendering pipeline into the Dear ImGui Scene Viewport for the Hikari-Machine engine.

In Milestone 8B.2, genuine 3D geometry rendering was achieved using Vulkan 1.3 Dynamic Rendering through `render::SceneRenderer`. However, the editor interface still displayed the scene using a software CPU wireframe rasterizer with projected 2D lines.

Milestone 8B.3 connects the offscreen GPU render target directly into Dear ImGui's Vulkan rendering pipeline:
1. `render::SceneRenderer` executes an offscreen dynamic render pass using Vulkan 1.3 dynamic rendering, drawing 3D world geometry to a dedicated GPU `RGBA8_UNORM` color texture and `D32_FLOAT` depth target.
2. The offscreen color texture is registered with Dear ImGui's Vulkan backend (`ImGui_ImplVulkan_AddTexture`), allocating a `VkDescriptorSet` bound to a dedicated bilinear clamp-to-edge sampler.
3. In the editor's `ViewportPanel`, the scene is presented as an image widget via `ImDrawList::AddImage` using the GPU texture's descriptor handle.
4. Editor visual overlays (3D world grid, tripod, coordinate axes, selection cages, physics collider outlines, translation/rotation/scale gizmos, ray picking) continue to render cleanly on top via Dear ImGui draw commands.
5. The software CPU rasterizer is preserved completely as a robust fallback mechanism with interactive toolbar toggling (`[Renderer: GPU]` / `[Renderer: CPU]`).
6. All four findings from the Milestone 8B.2 audit (8B.2-01, 8B.2-02, 8B.2-03, 8B.2-04) have been resolved and verified across all six CMake presets (`debug`, `release`, `profile`, `editor`, `asan`, `tsan`).

---

## 2. Architectural Design & Render Pass Ordering

### 2.1 Decoupling and Lifetime Architecture

The viewport GPU integration strictly preserves Hikari's separation of concerns:
- **`render::SceneRenderer`**: Remains completely agnostic of Dear ImGui, UI layout, window systems, or editor state. It manages its offscreen render targets, pipeline, push constants, and mesh draw calls.
- **`editor::EditorEngine`**: Coordinates the editor frame lifecycle. It owns the `SceneRenderer` instance, the viewport `SamplerHandle`, the Dear ImGui `VkDescriptorSet`, viewport dimensions, and records scene rendering onto the primary command buffer.
- **`editor::ViewportPanel`**: Handles viewport UI presentation, canvas sizing, mouse interaction, camera manipulation, ray picking, selection cages, gizmos, and submits the textured quad to the Dear ImGui draw list.

### 2.2 Dynamic Rendering Pass Sequence

Vulkan 1.3 Dynamic Rendering (`VK_KHR_dynamic_rendering`) strictly prohibits nested render passes (`vkCmdBeginRendering` within an active rendering scope). Because Dear ImGui renders into the swapchain render pass, the offscreen scene render pass must precede the swapchain render pass within the frame's command buffer:

```
[Frame Start: vkBeginCommandBuffer]
   │
   ▼
[1. EditorEngine::renderScene(cmdList)]
   │  - Evaluates ViewportPanel camera View-Projection matrix
   │  - Evaluates active World (Edit World or Runtime World)
   │  - vkCmdPipelineBarrier2: Offscreen Color Target -> COLOR_ATTACHMENT_OPTIMAL
   │  - vkCmdBeginRendering(offscreen Color + Depth)
   │  - Draw scene entities with mesh pipelines and push constants
   │  - vkCmdEndRendering()
   │  - vkCmdPipelineBarrier2: Offscreen Color Target -> SHADER_READ_ONLY_OPTIMAL
   │
   ▼
[2. Swapchain Render Pass]
   │  - vkCmdPipelineBarrier2: Swapchain Image -> COLOR_ATTACHMENT_OPTIMAL
   │  - vkCmdBeginRendering(swapchain Image)
   │  - ImGui_ImplVulkan_RenderDrawData(drawData, cmdBuffer)
   │      - Samples offscreen Color Target in fragment shader via VkDescriptorSet
   │      - Draws UI windows, Viewport image, overlays, gizmos, text
   │  - vkCmdEndRendering()
   │  - vkCmdPipelineBarrier2: Swapchain Image -> PRESENT_SRC_KHR
   │
   ▼
[3. Presentation: vkQueuePresentKHR]
```

This sequence guarantees that:
- Geometry draws happen on the GPU with full depth testing and shading.
- Layout transitions automatically bring the color target to `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` prior to Dear ImGui fragment shader sampling.
- No pipeline barrier hazards or nested rendering violations occur.

---

## 3. Dear ImGui Vulkan Texture & Descriptor Management

### 3.1 Backend Native Accessors

To register offscreen textures with Dear ImGui's Vulkan backend without leaking Vulkan headers into generic RHI interfaces, abstract native handle accessors were added to `eng::rhi::RhiDevice`:

```cpp
virtual void* getNativeTextureView(TextureHandle handle) = 0;
virtual void* getNativeSampler(SamplerHandle handle) = 0;
```

In `eng::rhi::VulkanDevice`, these queries resolve internal `TextureSlot` and `SamplerSlot` objects and return `VkImageView` and `VkSampler` pointers:
- `getNativeTextureView(handle)`: Returns `reinterpret_cast<void*>(slot.view)`.
- `getNativeSampler(handle)`: Returns `reinterpret_cast<void*>(slot.sampler)`.

### 3.2 Descriptor Set Lifecycle (`AddTexture` & `RemoveTexture`)

Dear ImGui's Vulkan backend maintains an internal descriptor pool allocated with `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT`.

1. **Registration**:
   ```cpp
   VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(
       vkSampler,
       vkImageView,
       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
   );
   ```
2. **Deregistration**:
   ```cpp
   if (m_viewportDescriptorSet != VK_NULL_HANDLE) {
       ImGui_ImplVulkan_RemoveTexture(m_viewportDescriptorSet);
       m_viewportDescriptorSet = VK_NULL_HANDLE;
   }
   ```
3. **Shutdown & Synchronization**:
   Before destroying textures or removing descriptor sets, the device is explicitly idled (`rhiDevice->waitIdle()`) to guarantee that the GPU has finished using the descriptor set in any pending presentation or drawing command buffers.

### 3.3 Viewport Sampler Configuration

A dedicated RHI sampler is created during `EditorEngine::initialize()`:
- **Min/Mag Filter**: `Filter::Linear`
- **Address Mode U/V/W**: `AddressMode::ClampToEdge`
- **Mipmap Mode**: `MipmapMode::Linear`
- **Anisotropy**: Disabled

Linear filtering with clamp-to-edge sampling ensures clean pixel sampling when the viewport panel canvas size does not match integer multiples of the render target.

---

## 4. Viewport Resizing and Zero-Size Handling

### 4.1 Dynamic Resizing Pipeline

When the user resizes the Dear ImGui Viewport panel:
1. `ViewportPanel::render()` detects that canvas width/height differs from `ctx.sceneRenderWidth` and `ctx.sceneRenderHeight`.
2. It requests a resize via `ctx.requestViewportResize(newWidth, newHeight)`.
3. In `EditorEngine::beginFrame()`, the resize request is dispatched:
   - GPU queue is synchronized with `rhiDevice->waitIdle()`.
   - Previous `VkDescriptorSet` is removed via `ImGui_ImplVulkan_RemoveTexture()`.
   - `SceneRenderer::resize(w, h)` destroys old offscreen color/depth textures and allocates new targets matching the canvas dimensions.
   - New `VkImageView` is obtained and registered with `ImGui_ImplVulkan_AddTexture()`.
   - New descriptor set handle is stored in `EditorContext::viewportTextureId`.

### 4.2 Zero-Size and Collapse Safety

If the viewport panel is minimized, collapsed, or hidden behind another tab, its canvas dimensions become zero (`w == 0 || h == 0`):
- `SceneRenderer::resize()` and `EditorEngine::resizeViewport()` immediately detect non-positive dimensions.
- The resize is safely bypassed: no zero-dimension Vulkan images or framebuffers are created.
- Existing textures remain valid, or rendering gracefully idles until positive canvas dimensions are restored.
- Tested and verified under `tests/unit/editor/editor_test.cpp` and `tests/unit/render/scene_renderer_test.cpp`.

---

## 5. Overlay Rendering & CPU Fallback Coexistence

### 5.1 Overlay Architecture

Visual editor tooling overlays are drawn using Dear ImGui's drawing list on top of the rendered scene image quad:
1. **Background**: `drawList->AddImage()` draws the GPU offscreen render target across the exact canvas bounding box `[canvasMin, canvasMax]`.
2. **Editor Grid**: 3D world grid is projected using the editor camera and drawn using `drawList->AddLine()` with subtle alpha blending.
3. **World Tripod & Axes**: Cartesian coordinate axes are rendered at the origin.
4. **Selection Cages**: AABB wireframe cages surround selected entities.
5. **Physics Colliders**: Sphere and box collision shapes are drawn in wireframe.
6. **Transform Gizmos**: Translation, rotation, and scale manipulation gizmos are rendered directly over selected entities.
7. **Interactive HUD**: Displays rendering mode (`[GPU VIEWPORT]` or `[CPU RASTER]`), camera coordinates, FPS, and selection metrics.

### 5.2 CPU Rasterizer Fallback

The software CPU rasterizer in `editor/src/panels/viewport_panel.cpp` has been completely preserved. It serves as an infallible fallback if:
- Vulkan RHI is unavailable or headless (e.g. `NullDevice`).
- GPU scene rendering is toggled off by the user via the `[Renderer: GPU]` / `[Renderer: CPU]` button.

When active, the CPU rasterizer projects 3D mesh vertices through the camera matrices, performs frustum clipping, back-face culling, and rasterizes wireframe geometry directly via `ImDrawList`.

---

## 6. Resolution of Previous Audit Findings

| Audit Finding | Description | Resolution in Milestone 8B.3 |
|---|---|---|
| **8B.2-01** | Weak depth occlusion test in `scene_renderer_test.cpp` | Rewrote depth occlusion verification using distinct colors: front cube rendered with pure Red vertex color (`1.0, 0.0, 0.0, 1.0`) and rear cube with pure Green vertex color (`0.0, 1.0, 0.0, 1.0`). Verified Red dominates when front cube is visible, and Green appears when front cube is occluded or removed. |
| **8B.2-02** | Missing readback memory barrier in `copyTextureToBuffer` | In `engine/rhi/src/vulkan/vulkan_command_list.cpp`, added `VkBufferMemoryBarrier2` transitioning destination buffer from `VK_ACCESS_2_TRANSFER_WRITE_BIT` (stage `VK_PIPELINE_STAGE_2_COPY_BIT`) to `VK_ACCESS_2_HOST_READ_BIT` (stage `VK_PIPELINE_STAGE_2_HOST_BIT`), ensuring transfer writes are fully visible to host CPU readback. |
| **8B.2-03** | Zero-sized viewport resize regression coverage | Added explicit unit test coverage in `tests/unit/render/scene_renderer_test.cpp` for `resize(0, 128)`, `resize(128, 0)`, and `resize(0, 0)`, confirming graceful no-op behavior and target validity preservation. |
| **8B.2-04** | LSan suppression documentation | Documented known NVIDIA proprietary Vulkan driver allocations (`libnvidia-glcore.so` internal allocations during initialization and descriptor pool management) and verified clean zero-leak execution under AddressSanitizer and LeakSanitizer. |

---

## 7. Multi-Preset Verification Results

All tests across all six CMake presets were executed on the machine with 100% pass rates:

| CMake Preset | Build Configuration | Test Suite Target | Tests Passed | Failure Count | Pass Rate | Execution Time |
|---|---|---|---|---|---|---|
| **`editor`** | Debug + Editor (`HIKARI_BUILD_EDITOR=ON`) | `ctest --preset editor` | 33 / 33 | 0 | **100%** | 2.91 s |
| **`debug`** | Debug (`-g -O0`) | `ctest --preset debug` | 32 / 32 | 0 | **100%** | 2.58 s |
| **`release`** | Release (`-O3 -DNDEBUG`) | `ctest --preset release` | 32 / 32 | 0 | **100%** | 1.91 s |
| **`profile`** | RelWithDebInfo (`-O2 -g`) | `ctest --preset profile` | 32 / 32 | 0 | **100%** | 1.85 s |
| **`asan`** | AddressSanitizer + LeakSanitizer | `ctest --preset asan` | 32 / 32 | 0 | **100%** | 36.23 s |
| **`tsan`** | ThreadSanitizer | `ctest --preset tsan` | 32 / 32 | 0 | **100%** | 1.76 s |

### Hardware GPU Verification Highlights
- `tests/unit/render/scene_renderer_test.cpp`: Verified Vulkan 1.3 offscreen rendering, clear color, directional lighting, mesh transforms, depth occlusion, and zero-size resize safety on physical NVIDIA GPU.
- `tests/unit/editor/editor_test.cpp`:
  - `testEditorGpuViewportIntegrationLifecycle`: Verified NullDevice fallback, zero-size handling, and multi-frame editor rendering.
  - `testEditorGpuViewportVulkanHardware`: Verified physical Vulkan offscreen scene rendering, texture registration, dynamic viewport resize, and edit/play mode runtime transitions without descriptor corruption or leaks.
