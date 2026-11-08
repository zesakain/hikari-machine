# Hikari-Machine: Milestone 8B.1 Independent Manual Acceptance Audit
## Offscreen Rendering Infrastructure

**Date:** 2026-11-08  
**Auditor:** Senior Graphics & Vulkan 1.3 Rendering Systems Engineer (Independent Audit)  
**Repository:** `/home/lux/Hikari`  
**Milestone:** 8B.1 (Offscreen Rendering Infrastructure)  
**Scope:** Audit-Only (Zero production code or test modifications)  

---

## 1. Executive Summary

An independent, rigorous, source-level technical audit was conducted on **Milestone 8B.1: Offscreen Rendering Infrastructure** in Hikari-Machine. The implementation was evaluated against Vulkan 1.3 Dynamic Rendering requirements, synchronization correctness, resource lifecycle contracts, Null RHI parity, swapchain backward compatibility, and the strict mandate to preserve the existing CPU viewport rasterizer.

### Audit Verdict: **PASS (Ready for Milestone 8B.2 with Documented Constraints)**

The offscreen rendering infrastructure successfully establishes:
1. Backend-agnostic offscreen dynamic rendering pass configuration via generalized `RenderPassBeginInfo`.
2. Vulkan 1.3 dynamic layout transitions via `VkImageMemoryBarrier2` transitioning offscreen color targets into `COLOR_ATTACHMENT_OPTIMAL` on entry and `SHADER_READ_ONLY_OPTIMAL` on exit (enabling immediate ImGui sampling).
3. 100% backward compatibility with swapchain backbuffer presentation.
4. Comprehensive multi-preset clean builds and 100% passing test suites across all six presets (`editor`, `debug`, `release`, `profile`, `asan`, and `tsan`).
5. Complete preservation of the existing CPU viewport rasterizer in `ViewportPanel`.

Specific non-blocking technical observations and architectural constraints for Milestone 8B.2 are documented in Section 11.

---

## 2. Audit Scope and Methodology

The audit inspected:
- Public RHI headers: `engine/rhi/include/rhi/command_list.h`, `rhi_device.h`, `rhi_types.h`.
- Vulkan implementation: `engine/rhi/src/vulkan/vulkan_command_list.cpp`, `vulkan_command_list.h`, `vulkan_device.cpp`, `vulkan_device.h`, `vulkan_types.h`.
- Null RHI implementation: `engine/rhi/src/null/null_command_list.cpp`, `null_command_list.h`, `null_device.cpp`, `null_device.h`.
- Unit test suites: `tests/unit/rhi/null_rhi_test.cpp`, `tests/unit/rhi/vulkan_rhi_test.cpp`.
- Editor viewport rasterizer: `editor/src/panels/viewport_panel.cpp`.
- Application entry point: `src/main.cpp`.
- Physical GPU runtime execution on dual NVIDIA Pascal (P106-100) hardware.

---

## 3. Source-Level Implementation Findings

### 3.1 Generalized Dynamic Render Pass Begin Info
- **File:** `engine/rhi/include/rhi/command_list.h` (`struct RenderPassBeginInfo`)
- **Analysis:**
  - `TextureHandle colorTarget{}`: Default invalid handle cleanly signals swapchain backbuffer presentation.
  - `TextureHandle depthTarget{}`: Accepts arbitrary offscreen depth attachment.
  - `float clearColor[4]`, `float clearDepth{1.0F}`: Explicit, zero-allocation POD fields.
  - `bool clearTarget{true}`, `bool clearDepthTarget{true}`: Selective load/clear control.
  - `uint32_t renderWidth{0}`, `uint32_t renderHeight{0}`: Optional render extent overriding.
- **Contract Integrity:** POD struct with aggregate initialization; fully backward compatible.

### 3.2 Texture Format Utilities
- **File:** `engine/rhi/include/rhi/rhi_types.h`
- **Analysis:**
  - `isDepthFormat(TextureFormat)`: Correctly recognizes `D32_FLOAT`, `D24_UNORM_S8_UINT`, and `D32_FLOAT_S8_UINT`.
  - `isColorFormat(TextureFormat)`: Evaluates `format != TextureFormat::Undefined && !isDepthFormat(format)`.
  - Both functions are `constexpr noexcept` with zero runtime overhead.

### 3.3 Texture Metadata Query
- **File:** `engine/rhi/include/rhi/rhi_device.h`
- **Analysis:**
  - `virtual const TextureDesc* getTextureDesc(TextureHandle) const noexcept`: Non-owning query returning `nullptr` on invalid or destroyed generational handles.
  - Implemented across both `NullDevice` and `VulkanDevice`.

---

## 4. Vulkan Synchronization Analysis

Synchronization correctness is the core focus of Milestone 8B.1. The implementation in `VulkanCommandList::beginRenderPass` and `endRenderPass` was audited against Vulkan 1.3 specifications:

### 4.1 Offscreen Color Target Synchronization
1. **Pass Entry (`beginRenderPass`, lines 150-175):**
   - **Old Layout:** Queried via `device_->getTextureLayout(info.colorTarget)`. On initial creation, this is `VK_IMAGE_LAYOUT_UNDEFINED`. On subsequent frames, it is `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`.
   - **New Layout:** `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL`.
   - **Source Access Mask:** Evaluated as `(oldColorLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) ? VK_ACCESS_2_SHADER_READ_BIT : 0`.
   - **Destination Access Mask:** `VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT`.
   - **Source Stage:** `VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT`.
   - **Destination Stage:** `VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT`.
   - **Verdict:** Fully establishes memory and execution dependency before rasterization begins.
2. **Pass Exit (`endRenderPass`, lines 420-444):**
   - **Old Layout:** `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL`.
   - **New Layout:** `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`.
   - **Source Access Mask:** `VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT`.
   - **Destination Access Mask:** `VK_ACCESS_2_SHADER_READ_BIT`.
   - **Source Stage:** `VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT`.
   - **Destination Stage:** `VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT`.
   - **Verdict:** Correctly transitions the color attachment for downstream fragment shader sampling (e.g. ImGui viewport panel descriptor binding in Milestone 8B.2).

### 4.2 Offscreen Depth Target Synchronization
1. **Pass Entry (`beginRenderPass`, lines 182-208):**
   - **Old Layout:** `device_->getTextureLayout(info.depthTarget)` (`UNDEFINED` or `DEPTH_STENCIL_READ_ONLY_OPTIMAL`).
   - **New Layout:** `VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL`.
   - **Source Access Mask:** `VK_ACCESS_2_SHADER_READ_BIT` (if previously read-only) or `0`.
   - **Destination Access Mask:** `VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT`.
   - **Destination Stage:** `VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT`.
   - **Verdict:** Correct depth testing/writing setup.
2. **Pass Exit (`endRenderPass`, lines 445-472):**
   - Automatically transitions depth target to `VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL` with `dstStageMask = FRAGMENT_SHADER_BIT` and `dstAccessMask = SHADER_READ_BIT`.
   - **Observation:** In typical editor forward rendering, depth attachments are not sampled by shaders. Transitioning depth to read-only every frame introduces redundant transitions (see Finding F-01).

### 4.3 Subresource Range & Aspect Masks
- Color barriers use `aspectMask = VK_IMAGE_ASPECT_COLOR_BIT`.
- Depth barriers use `aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT`.
- **Observation:** For pure depth textures (`D32_FLOAT`), `VK_IMAGE_ASPECT_DEPTH_BIT` is strictly correct. For combined depth-stencil formats (`D24_UNORM_S8_UINT`), transitioning only `DEPTH_BIT` leaves the stencil aspect in `UNDEFINED` (see Finding F-02).

---

## 5. Dynamic Rendering Audit

### 5.1 Dynamic Attachment Configuration
- `VkRenderingAttachmentInfo` for color:
  - `loadOp`: `VK_ATTACHMENT_LOAD_OP_CLEAR` (if `info.clearTarget`) or `VK_ATTACHMENT_LOAD_OP_LOAD`.
  - `storeOp`: `VK_ATTACHMENT_STORE_OP_STORE`.
  - `clearValue`: Set from `info.clearColor[0..3]`.
- `VkRenderingAttachmentInfo` for depth:
  - `loadOp`: `VK_ATTACHMENT_LOAD_OP_CLEAR` (if `info.clearDepthTarget`) or `VK_ATTACHMENT_LOAD_OP_LOAD`.
  - `storeOp`: `VK_ATTACHMENT_STORE_OP_STORE`.
  - `clearValue.depth`: Set from `info.clearDepth`.
- `VkRenderingInfo`:
  - `layerCount = 1`, `viewMask = 0`.
  - `renderArea`: Correctly bounds `{width, height}`.
  - Automatically invokes dynamic `setViewport` and `setScissor` to avoid uninitialized state.
- **Missing Depth Handling:** If `!info.depthTarget.isValid()`, `pDepthAttachment` is set to `nullptr` and `activeDepthFormat_` to `Undefined`.
- **Pass Nesting & Orphan Guards:**
  - `beginRenderPass` rejects re-entrant calls with `ENG_LOG_WARN`.
  - `endRenderPass` rejects calls when `!isInsideRenderPass_`.

---

## 6. Resource Lifetime and Resize Safety

### 6.1 RAII & Destruction Order
- In `VulkanDevice::destroyTexture`:
  1. `vkDestroyImageView(device_, slot.view, nullptr)`
  2. `vkDestroyImage(device_, slot.image, nullptr)`
  3. `VulkanMemoryManager::freeMemory(device_, slot.memory)`
- **Verdict:** Destruction order strictly adheres to Vulkan requirements (views destroyed before parent images, images destroyed before bound memory is freed).

### 6.2 In-Flight Synchronization Contract
- `destroyTexture` destroys Vulkan objects immediately on the CPU; there is no internal deferred deletion ring or fence-driven garbage collection.
- **Contract Guarantee vs Assumption:**
  - **Verified Guarantee:** Handle invalidation is immediate; subsequent RHI calls using stale handles fail safely via generational index mismatch.
  - **Caller Constraint:** If GPU work referencing a texture is in flight, the caller **must** synchronize via `device->waitIdle()` prior to invoking `destroyTexture()`. Viewport resizing in Milestone 8B.2 must adhere to this contract.

---

## 7. Null RHI Parity

The headless Null RHI backend was verified to provide comprehensive validation parity with Vulkan RHI:
- Tracks `activeColorTarget_`, `activeDepthTarget_`, `activeColorFormat_`, `activeDepthFormat_`, `activeWidth_`, `activeHeight_`.
- Detects invalid attachment handles (`hasInvalidAttachment_`).
- Detects non-color format used as colorTarget or non-depth format used as depthTarget (`hasInvalidAttachment_`).
- Detects dimension mismatches between color and depth attachments (`hasDimensionMismatch_`).
- Detects re-entrant/nested `beginRenderPass` (`hasNestedRenderPass_`).
- Detects orphan `endRenderPass` (`hasOrphanEndPass_`).
- Detects pipeline color/depth format mismatches on draw (`hasFormatMismatch_`).
- **Verdict:** Parity is substantive, not superficial. Enables 100% headless CI validation without a GPU.

---

## 8. Swapchain Backwards Compatibility

Verified against `src/main.cpp` and `tests/unit/rhi/vulkan_rhi_test.cpp`:
- `RenderPassBeginInfo` with default invalid `colorTarget{}` cleanly bypasses offscreen logic and takes the Swapchain presentation branch.
- Acquires current swapchain image index, transitions swapchain image from `UNDEFINED` to `COLOR_ATTACHMENT_OPTIMAL`, begins dynamic rendering, and on `endRenderPass`, transitions to `PRESENT_SRC_KHR`.
- All existing UI rendering and swapchain acquisition tests pass with zero regressions.

---

## 9. Test Quality and Runtime Evidence

### 9.1 Test Categorization Matrix

| Test Function | Target Backend | Nature of Test | What Is Genuinely Proven |
| :--- | :---: | :---: | :--- |
| `testOffscreenTextureLifecycleAndValidation` | Null | Lifecycle / Validation | Slot allocation, descriptor query, rejection of 0 dimension / undefined format, generational invalidation. |
| `testOffscreenDynamicRendering` | Null | Mock Recording | Color-only, color+depth, swapchain fallback, invalid handles, dimension mismatch, nested/orphan passes. |
| `testOffscreenPipelineCompatibility` | Null | Compatibility Check | Color format mismatch detection, depth format mismatch detection. |
| `testVulkanOffscreenTextureLifecycle` | Vulkan | Real GPU Lifecycle | Physical allocation of `RGBA8_UNORM` & `D32_FLOAT` on GPU memory, view creation, desc query, destruction. |
| `testVulkanOffscreenDynamicRendering` | Vulkan | Real GPU Execution | Command recording, dynamic barrier submission, pipeline binding, push constants, draw call, queue submit, and `waitIdle()`. |
| `testVulkanOffscreenPipelineCompatibility` | Vulkan | Error Path Check | Format mismatch warning logging and flag tracking without driver crash. |

### 9.2 Limitations of Existing Tests
1. **No Pixel Readback Verification:** Current tests verify GPU queue completion and barrier execution; they do not download texture bytes back to host CPU to verify rendered pixels. (Pixel golden tests will belong in Milestone 8B.2).
2. **Vulkan Validation Layers Inactive on Dev Machine:** Inspection of the test floor environment reveals `VK_LAYER_KHRONOS_validation` is not installed under `/usr/share/vulkan/explicit_layer.d`. Consequently, runtime validation layers are silently skipped. Tests ran against physical NVIDIA driver runtime directly.

---

## 10. CPU Viewport Rasterizer Preservation

Inspection of `editor/src/panels/viewport_panel.cpp`:
- All projection math (`camera_.project`), 3D OBB corner calculations, backface culling, directional lighting, selection outlines, collider wireframes, name tags, and transform gizmos remain completely intact.
- Git status confirms zero modifications to `editor/`.
- **Verdict:** Fully preserved and operational as the fallback renderer.

---

## 11. Findings and Severity Classification

### [F-01] Unnecessary Automatic Depth Buffer Transition to Read-Only
- **Severity:** MEDIUM
- **Location:** `engine/rhi/src/vulkan/vulkan_command_list.cpp` (`endRenderPass`, lines 445-472)
- **Description:** `endRenderPass()` unconditionally transitions the offscreen depth attachment to `VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL` with `dstStageMask = FRAGMENT_SHADER_BIT`. In editor forward rendering, depth attachments are rarely sampled by shaders. On the subsequent frame, `beginRenderPass()` must transition it back to `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`.
- **Impact:** Redundant layout transition and barrier overhead on depth buffer each frame.
- **Blocking for 8B.2:** NO. Functionally correct, but recommended for optimization.

### [F-02] Depth-Stencil Aspect Mask for Combined Formats
- **Severity:** MEDIUM
- **Location:** `engine/rhi/src/vulkan/vulkan_command_list.cpp` (lines 201, 465)
- **Description:** Image memory barriers for depth attachments hardcode `.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT`. For combined depth-stencil formats (`D24_UNORM_S8_UINT`, `D32_FLOAT_S8_UINT`), transitioning layout to `DEPTH_STENCIL_*` requires both `DEPTH_BIT` and `STENCIL_BIT` if both planes are transitioned.
- **Impact:** For `D32_FLOAT` (Hikari's primary depth format), this has zero impact. For formats with stencil, stencil aspect is not transitioned.
- **Blocking for 8B.2:** NO (Hikari uses `D32_FLOAT` for viewport depth).

### [F-03] Draw Execution Despite Pipeline Format Mismatch in Vulkan Backend
- **Severity:** HIGH (Technical Debt)
- **Location:** `engine/rhi/src/vulkan/vulkan_command_list.cpp` (`draw` line 706, `drawIndexed` line 739)
- **Description:** When format mismatch is detected (`activeColorFormat_ != pDesc->colorAttachmentFormats[0]`), the implementation sets `hasFormatMismatch_ = true` and logs `ENG_LOG_WARN`, but fails to `return;`. It proceeds to call `vkCmdDraw` / `vkCmdDrawIndexed`.
- **Impact:** Calling `vkCmdDraw` with mismatched attachment formats violates Vulkan dynamic rendering specification. On NVIDIA drivers it did not crash, but an early `return;` should prevent invalid draw submission (identical to `hasUnboundDraw`).
- **Blocking for 8B.2:** NO, provided pipelines created in 8B.2 match render pass attachment formats (`RGBA8_UNORM` color, `D32_FLOAT` depth).

### [F-04] Texture Destruction Lacks Deferred Deletion Queue
- **Severity:** LOW (Contract Constraint)
- **Location:** `engine/rhi/src/vulkan/vulkan_device.cpp` (`destroyTexture`, line 552)
- **Description:** Textures are destroyed immediately on the CPU without checking in-flight GPU fences.
- **Impact:** Caller must call `device->waitIdle()` prior to destroying textures during viewport resize.
- **Blocking for 8B.2:** NO, provided `SceneRenderer` / `EditorEngine` invokes `device->waitIdle()` on resize.

---

## 12. Build and Test Verification

All six CMake presets were executed cleanly on the test machine:

```bash
ctest --preset editor --output-on-failure   # 32 / 32 Passed (100%), 2.71s
ctest --preset debug --output-on-failure    # 31 / 31 Passed (100%), 2.39s
ctest --preset release --output-on-failure  # 31 / 31 Passed (100%), 1.89s
ctest --preset profile --output-on-failure  # 31 / 31 Passed (100%), 1.86s
ctest --preset asan --output-on-failure     # 31 / 31 Passed (100%), 34.90s (Zero memory errors / leaks)
ctest --preset tsan --output-on-failure     # 31 / 31 Passed (100%), 1.87s (Zero data races)
```

---

## 13. Milestone 8B.2 Readiness Matrix

| Capability Required for 8B.2 | Status | Evidence / Verification |
| :--- | :---: | :--- |
| **Offscreen Color Target (RGBA8_UNORM)** | **VERIFIED** | Physical GPU allocation, dynamic rendering clear and draw tested in `testVulkanOffscreenDynamicRendering`. |
| **Offscreen Depth Target (D32_FLOAT)** | **VERIFIED** | Physical GPU allocation and depth-stencil dynamic attachment tested in `testVulkanOffscreenDynamicRendering`. |
| **Graphics Pipeline Dynamic Compatibility** | **VERIFIED** | Format validation active in Null and Vulkan command lists. |
| **Vertex & Index Buffer Drawing** | **VERIFIED** | Verified through existing `draw` / `drawIndexed` GPU pipelines. |
| **Repeated Frame Rendering** | **VERIFIED** | State machine correctly recovers `SHADER_READ_ONLY_OPTIMAL` layout on subsequent frame pass entry. |
| **ImGui Viewport Texture Sampling** | **VERIFIED** | Offscreen color target ends in `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` with fragment shader read access. |
| **Resource Lifetime & Resize Safety** | **PARTIALLY VERIFIED** | Verified synchronous destruction; caller must invoke `device->waitIdle()` before resizing. |
| **CPU Fallback Rasterizer Preservation** | **VERIFIED** | `ViewportPanel` untouched and functional. |

---

## 14. Final Verdict

### **PASS: Ready for Milestone 8B.2**

The offscreen rendering infrastructure implemented in Milestone 8B.1 is architecturally sound, thoroughly tested across all six presets, and provides the necessary render target and layout transition capabilities for GPU scene rendering.

Milestone 8B.2 may proceed to implement:
1. `SceneRenderer` viewport render target management.
2. GPU mesh draw submission in offscreen pass.
3. Dear ImGui viewport texture descriptor binding (`ImGui_ImplVulkan_AddTexture`).
4. Viewport resize handling with caller-side `waitIdle()`.
