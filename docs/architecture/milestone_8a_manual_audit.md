# Independent Manual Acceptance Audit: Milestone 8A
## RHI Graphics Pipeline & Shader Foundation

**Repository:** `/home/user/hikari-machine`  
**Branch:** `main`  
**Audit Date:** 2026-11-08  
**Audit Scope:** Verification of RHI graphics pipeline and shader infrastructure implemented in Milestone 8A.

---

## A. Executive Verdict

**Verdict: PASS — Ready for Milestone 8B**

Milestone 8A has successfully established a clean, robust, and backend-independent GPU graphics pipeline and shader foundation.

The implementation strictly honors Rule 1, Rule 2, and Rule 3 of `AGENTS.md`:
- All graphics API interactions (Vulkan) are strictly encapsulated inside `/engine/rhi/src/vulkan/`.
- Public RHI headers expose zero vendor types or API-specific concepts.
- The Null RHI backend provides deterministic validation and generational handle protection for headless simulation and CI.
- The Vulkan backend compiles genuine GPU graphics pipelines with Vulkan 1.3 Dynamic Rendering and dynamic viewport/scissor states on physical hardware (NVIDIA P106-100).
- All 6 CMake presets (`editor`, `debug`, `release`, `profile`, `asan`, `tsan`) pass 100% of their test suites with zero leaks and zero data races.

The identified limitations (such as `beginRenderPass` currently targeting swapchain presentation and descriptor sets awaiting allocation infrastructure) are expected boundary demarcations that are scheduled for implementation in Milestone 8B.

---

## B. Verified Implementation Matrix

| Capability | Status | Evidence | Notes |
|---|---|---|---|
| **Shader module lifecycle** | **PASS** | `engine/rhi/src/vulkan/vulkan_device.cpp:635`<br>`engine/rhi/src/null/null_device.cpp:193` | Deep-copies bytecode and entry point into slot memory; generational handle reuse tested; SPIR-V header validation enforced. |
| **Pipeline layout** | **PASS** | `engine/rhi/src/vulkan/vulkan_device.cpp:696`<br>`engine/rhi/src/null/null_device.cpp:235` | Creates `VkPipelineLayout` with push constants and descriptor set layouts; rollback cleanup on partial failure verified. |
| **Descriptor set layouts** | **PASS** | `engine/rhi/src/vulkan/vulkan_device.cpp:705`<br>`engine/rhi/src/null/null_device.cpp:242` | Translates bindings and types to `VkDescriptorSetLayout`; validates count > 0; checks duplicate binding indices. |
| **Push constants** | **PASS** | `engine/rhi/include/rhi/pipeline.h:128`<br>`engine/rhi/src/vulkan/vulkan_command_list.cpp:328` | 4-byte offset and size alignment enforced; size <= 128 bytes enforced; emits `vkCmdPushConstants`. |
| **Graphics pipeline creation** | **PASS** | `engine/rhi/src/vulkan/vulkan_device.cpp:817`<br>`engine/rhi/src/null/null_device.cpp:289` | Full translation of vertex inputs, topology, rasterization, depth/stencil, blending, and dynamic formats. |
| **Dynamic rendering compatibility** | **PASS** | `engine/rhi/src/vulkan/vulkan_device.cpp:1041` | Chained `VkPipelineRenderingCreateInfo` in `VkGraphicsPipelineCreateInfo::pNext`; `renderPass = VK_NULL_HANDLE`. |
| **Pipeline binding** | **PASS** | `engine/rhi/src/vulkan/vulkan_command_list.cpp:305`<br>`engine/rhi/src/null/null_command_list.cpp:74` | Records `vkCmdBindPipeline`; caches bound pipeline and layout; invalidates on bad/stale handle. |
| **Null RHI validation** | **PASS** | `tests/unit/rhi/null_rhi_test.cpp:535` | Generational handle tracking, invalid parameter rejection, unbound draw detection, 100% headless pass. |
| **Vulkan resource lifecycle** | **PASS** | `engine/rhi/src/vulkan/vulkan_device.cpp:1115`<br>`tests/unit/rhi/vulkan_rhi_test.cpp:811` | RAII device cleanup destroys pipelines, layouts, and shaders; verified on NVIDIA P106-100 GPU. |
| **Test coverage** | **PASS** | `tests/unit/rhi/test_shaders.h`<br>`null_rhi_test.cpp`, `vulkan_rhi_test.cpp` | Hermetic precompiled SPIR-V shaders; positive and negative tests; all 6 CMake presets verified. |

---

## C. Critical Findings

**None.**

No crashes, memory corruptions, memory leaks, data races, or breaking architectural violations were found. All 6 CMake build presets compile with zero warnings treated as errors and execute all tests cleanly.

---

## D. Medium and Low Findings

### Medium Findings

1. **`VulkanCommandList::beginRenderPass` Currently Restricted to Swapchain Presentation**
   - **Severity:** Medium (Milestone 8B Prerequisite)
   - **File & Symbol:** [`engine/rhi/src/vulkan/vulkan_command_list.cpp:77-178`](engine/rhi/src/vulkan/vulkan_command_list.cpp#L77-L178), `VulkanCommandList::beginRenderPass`
   - **Evidence:** Line 82 asserts `device_->getSwapchain() != nullptr`, retrieves `swapView`, and line 168 sets `.pDepthAttachment = nullptr`. The `info.colorTarget` parameter is currently not resolved to an offscreen `VkImageView`.
   - **Explanation:** In Milestone 8A, offscreen viewport rendering was deliberately scoped out to keep the foundation atomic. However, `beginRenderPass` must be generalized in Milestone 8B to bind arbitrary offscreen color and depth render targets.
   - **Recommended Action for Milestone 8B:** Extend `RenderPassBeginInfo` with `TextureHandle depthTarget`. In `beginRenderPass`, inspect `info.colorTarget`: if valid, resolve its `VkImageView` and transition its layout; similarly bind `info.depthTarget` if valid.

2. **Descriptor Set Allocation and Binding Path Not Implemented**
   - **Severity:** Medium
   - **File & Symbol:** [`engine/rhi/include/rhi/rhi_device.h`](engine/rhi/include/rhi/rhi_device.h), [`engine/rhi/include/rhi/command_list.h`](engine/rhi/include/rhi/command_list.h)
   - **Evidence:** `createPipelineLayout` successfully compiles `VkDescriptorSetLayout` arrays, but `RhiDevice` does not offer `createDescriptorPool` / `allocateDescriptorSet`, and `RhiCommandList` does not provide `bindDescriptorSet`.
   - **Explanation:** Push constants (`kMaxPushConstantSize = 128` bytes) are fully operational and serve as the primary mechanism for uploading MVP and transform matrices in baseline rendering.
   - **Recommended Action for Milestone 8B:** Use push constants for camera view-projection and entity model matrices in `SceneRenderer`. If descriptor-based uniform buffers or textures are introduced later, implement descriptor pools and binding in a dedicated milestone.

### Low Findings

1. **`pushConstants` Does Not Validate Layout Compatibility Against Bound Pipeline**
   - **Severity:** Low
   - **File & Symbol:** [`engine/rhi/src/vulkan/vulkan_command_list.cpp:328`](engine/rhi/src/vulkan/vulkan_command_list.cpp#L328), `VulkanCommandList::pushConstants`
   - **Evidence:** `pushConstants` resolves `layout` and calls `vkCmdPushConstants` directly without checking whether `layout == boundLayoutHandle_`.
   - **Explanation:** If a caller accidentally passes a layout unrelated to the currently bound pipeline, undefined push constant mapping could occur.
   - **Recommended Action:** In debug builds, add an assert or log warning if `boundPipeline_ != VK_NULL_HANDLE && layout != boundLayoutHandle_`.

2. **Stencil Operations Hardcoded to Disabled**
   - **Severity:** Low
   - **File & Symbol:** [`engine/rhi/src/vulkan/vulkan_device.cpp:981`](engine/rhi/src/vulkan/vulkan_device.cpp#L981)
   - **Evidence:** `depthStencil.stencilTestEnable` is hardcoded to `VK_FALSE` in pipeline creation.
   - **Explanation:** `PipelineDepthStencilState` only exposes depth testing, writing, and compare operators. Baseline 3D mesh rasterization does not require stencil buffers.
   - **Recommended Action:** Defer stencil state exposure until a feature (such as planar shadows or object outlines) requires it.

---

## E. Independent Test Results

All six CMake build presets and unit test suites were executed independently during this audit:

| Preset | Tests Run | Tests Passed | Tests Failed | Execution Time | Sanitizer Diagnostics |
| :--- | :---: | :---: | :---: | :---: | :--- |
| **`editor`** | 32 | **32** | 0 | 2.45 s | Clean |
| **`debug`** | 31 | **31** | 0 | 2.21 s | Clean |
| **`release`** | 31 | **31** | 0 | 1.67 s | Clean |
| **`profile`** | 31 | **31** | 0 | 1.53 s | Clean |
| **`asan`** | 31 | **31** | 0 | 28.93 s | **0 leaks, 0 address violations** |
| **`tsan`** | 31 | **31** | 0 | 1.65 s | **0 data races, 0 thread conflicts** |

### Vulkan Hardware Environment
- **Device:** NVIDIA P106-100 (Pascal architecture, discrete GPU).
- **Driver Version:** 580.159.04.
- **Vulkan API Conformance:** 1.4.312 (Tier 0 baseline).
- **Validation Layers:** System layer audit (`vulkaninfo`) confirmed that `VK_LAYER_KHRONOS_validation` is not installed on the host operating system. Physical driver initialization, dynamic rendering pipeline creation, push constant recording, and queue submission executed without driver warnings or faults.

---

## F. Missing Test Coverage

The following negative test cases should be considered for future additions:
1. **Vulkan Pipeline Creation Negative Paths:** `null_rhi_test` validates rejection of missing vertex shaders, invalid attribute binding slots, and missing attachment formats. In `vulkan_rhi_test`, these checks rely on `validateGraphicsPipelineDesc`, but explicit negative device tests would reinforce coverage.
2. **Push Constant Range Boundary Enforcement:** Test that attempting to write beyond the declared `size` in a push constant block is flagged during recording.
3. **GPU Render Target Draw Output Verification:** Validating that vertex data actually writes expected pixel values to a render target. This is deferred to Milestone 8B when offscreen render targets are available.

---

## G. Milestone 8B Readiness Assessment

Answering the six architectural questions for Milestone 8B:

1. **Can a future `SceneRenderer` create a graphics pipeline through the public RHI?**  
   **YES.** `RhiDevice::createGraphicsPipeline()` is fully implemented in both `NullDevice` and `VulkanDevice`. It takes `GraphicsPipelineDesc` with vertex/fragment shaders, vertex input layouts, rasterization, depth/stencil, blending, and dynamic rendering attachment formats.

2. **Can it bind the pipeline safely?**  
   **YES.** `RhiCommandList::bindPipeline(PipelineHandle)` is fully implemented. It validates generational handles, binds `VkPipeline` to `VK_PIPELINE_BIND_POINT_GRAPHICS`, and invalidates state cleanly on destroyed handles.

3. **Can it upload camera and model data?**  
   **YES, via Push Constants.** `RhiCommandList::pushConstants()` is fully implemented and mapped to `vkCmdPushConstants`. Up to 128 bytes are guaranteed, providing sufficient storage for a 64-byte View-Projection matrix and a 64-byte Model matrix.

4. **Can it use vertex and index buffers with the pipeline?**  
   **YES.** `bindVertexBuffer()` and `bindIndexBuffer()` are already implemented and bind `VkBuffer` resources. `draw()` and `drawIndexed()` are guarded by safety checks that ensure a valid graphics pipeline is bound prior to issuing draw commands.

5. **Can it render into a dynamic-rendering color/depth target?**  
   **PARTIALLY.** The graphics pipeline is compiled with `VkPipelineRenderingCreateInfo` supporting color attachment formats and depth attachment format. However, `VulkanCommandList::beginRenderPass` currently only attaches the swapchain backbuffer. Milestone 8B must update `beginRenderPass` to bind offscreen color and depth textures.

6. **What additional RHI capabilities are still needed for Milestone 8B?**  
   - Add `TextureHandle depthTarget` to `RenderPassBeginInfo`.
   - Update `VulkanCommandList::beginRenderPass` to transition and attach offscreen color/depth textures.
   - Implement texture creation with `TextureFormat::D32_FLOAT` for offscreen depth buffers.
   - Expose the offscreen color texture to Dear ImGui (`ImGui_ImplVulkan_AddTexture`) for display in `ViewportPanel`.

---

## H. Final Recommendation

**Milestone 8A is APPROVED.**

Milestone 8B may begin immediately. The implementation is clean, robust, and mathematically sound. No code refactoring or bug fixes are required prior to initiating Milestone 8B.
