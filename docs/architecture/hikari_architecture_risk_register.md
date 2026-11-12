# Hikari Engine Architecture: Architectural Risk Register

**Document ID:** `ARCH-RISK-2026-REV1`  
**Governing Standard:** Repository Contract [`/AGENTS.md`](../../AGENTS.md)  
**Parent Document:** [`hikari_engine_architecture.md`](hikari_engine_architecture.md)  
**Related Documents:** [`hikari_rendering_architecture.md`](hikari_rendering_architecture.md), [`../roadmap/hikari_rendering_evolution_milestone_11.md`](../roadmap/hikari_rendering_evolution_milestone_11.md)  

---

## 1. Overview & Risk Assessment Methodology

This register identifies, catalogs, and ranks the technical risks, architectural debt, and performance bottlenecks discovered during the full engine codebase audit.

Risks are categorized based on their impact on **Milestone 11 (Photorealistic Rendering & Performance Engineering)** and evaluated against `/AGENTS.md` hard constraints (determinism, frame-time consistency, memory safety, and hot-path rules).

---

## 2. Prioritized Risk Register Matrix

| Risk ID | Subsystem | Source Location | Severity | Likelihood | Impact Area | M11 Blocker Status |
|---|---|---|---|---|---|---|
| **RISK-01** | `render` | [`engine/render/src/scene_renderer.cpp`](../../engine/render/src/scene_renderer.cpp) | **CRITICAL** | HIGH | Frame time & GPU vertex throughput | **Blocks M11B** (Architecture Opt) |
| **RISK-02** | `render` | [`engine/render/src/scene_renderer.cpp`](../../engine/render/src/scene_renderer.cpp), [`render_graph.cpp`](../../engine/render/src/render_graph.cpp) | **CRITICAL** | HIGH | Extensibility, barrier bugs, multi-pass | **Blocks M11B & M11D** (GI/Passes) |
| **RISK-03** | `render` | [`engine/render/src/scene_renderer.cpp`](../../engine/render/src/scene_renderer.cpp) | **HIGH** | HIGH | Shadow visual fidelity & aliasing | **Blocks M11E** (Advanced Shadows) |
| **RISK-04** | `render` | [`engine/render/src/scene_renderer.cpp`](../../engine/render/src/scene_renderer.cpp) | **HIGH** | MEDIUM | Overdraw & Geometry Throughput (Provisional) | **Addresses M11B** (Empirical Study) |
| **RISK-05** | `platform` / `rhi` | [`engine/rhi/src/vulkan/vulkan_device.cpp`](../../engine/rhi/src/vulkan/vulkan_device.cpp) | **MEDIUM** | HIGH | Presentation latency & PCIe bus load | Non-blocking (Hardware baseline) |
| **RISK-06** | `editor` / `render`| [`editor/src/viewport_panel.cpp`](../../editor/src/viewport_panel.cpp), [`editor_engine.cpp`](../../editor/src/editor_engine.cpp) | **HIGH** | LOW | Crash / Segmentation fault | Mitigated by `waitIdle` + queue |
| **RISK-07** | `rhi` | [`engine/rhi/src/vulkan/vulkan_device.cpp`](../../engine/rhi/src/vulkan/vulkan_device.cpp) | **HIGH** | HIGH | CPU Synchronous Fence Wait Stall ($35\text{ ms}$) | **Blocks M11B** (Pipelining) |
| **RISK-08** | `script` | [`engine/script/include/script/script_engine.h`](../../engine/script/include/script/script_engine.h) | **MEDIUM** | LOW | CPU frame budget violation | Non-blocking (Enforced by policy) |

---

## 3. Deep-Dive Risk Analysis

### RISK-01: Viewport Rendering Lacks Frustum Culling
- **Source Location:** [`engine/render/src/scene_renderer.cpp:1164-1180, 1288-1320`](../../engine/render/src/scene_renderer.cpp#L1164-L1180)
- **Technical Description:** `SceneRenderer::renderScene()` performs a brute-force query over all entities containing `MeshComponent` and submits every submesh unconditionally to both the shadow pass and main PBR pass.
- **Measured Symptom:** In the Sponza benchmark, all 103 submeshes (262,267 triangles) are processed twice every frame, yielding 206 draw calls and 524,534 triangles/frame, with `culledPrimitives = 0` reported in telemetry regardless of camera orientation.
- **Roadmap Consequence:** Directly blocks Milestone 11B. Any expansion of scene complexity or addition of multi-camera shadow cascades will cause severe GPU vertex processing stalls on Tier 0 hardware.
- **Mitigation Requirement:** Implement hierarchical CPU bounding-box frustum culling (`AABB` vs camera frustum planes) during scene query gathering before command recording.

---

### RISK-02: Live SceneRenderer Disconnected from RenderGraph
- **Source Location:** [`engine/render/src/scene_renderer.cpp:976-1425`](../../engine/render/src/scene_renderer.cpp#L976-L1425), [`engine/render/src/render_graph.cpp`](../../engine/render/src/render_graph.cpp)
- **Technical Description:** The repository includes a fully implemented DAG-based `RenderGraph` module with automated hazard tracking and barrier synthesis, but `SceneRenderer` completely ignores it. All passes in `SceneRenderer` are hardcoded in a manual linear sequence with hand-placed `VkImageMemoryBarrier` calls.
- **Failure Mode:** Adding future passes (Depth Pre-pass, SSAO, SSR, Cascaded Shadows, TAA) requires manual hazard tracking. Any missing or malformed pipeline barrier causes Vulkan validation errors, race conditions, or graphical corruption.
- **Roadmap Consequence:** Blocks Milestone 11B (Renderer Modernization) and Milestone 11D (Global Illumination).
- **Mitigation Requirement:** Refactor `SceneRenderer` to declare passes (Shadow, Pre-pass, PBR, Post) as nodes within `RenderGraph` and delegate barrier generation to `RenderGraph::compile()` and `execute()`.

---

### RISK-03: Single Fixed 2048 Directional Shadow Projection
- **Source Location:** [`engine/render/src/scene_renderer.cpp:1118-1131`](../../engine/render/src/scene_renderer.cpp#L1118-L1131)
- **Technical Description:** Lighting uses a single orthographic projection matrix for directional shadows, rendering into a single $2048 \times 2048$ depth buffer.
- **Visual Failure:** In expansive scenes like Sponza (length ~35 meters), shadow texels are distributed thinly across the entire scene. Foregrounds suffer from extreme shadow pixelation (perspective aliasing), while distant objects experience shadow acne or detachment.
- **Roadmap Consequence:** Blocks Milestone 11E (Cascaded Shadow Maps & Contact Shadows).
- **Mitigation Requirement:** Implement 4-split Cascaded Shadow Maps (CSM) using logarithmic/practical split distances with sub-frustum bounding boxes.

---

### RISK-04: Geometry Overhead & Provisional Depth Pre-Pass Analysis
- **Source Location:** [`engine/render/src/scene_renderer.cpp:1243-1423`](../../engine/render/src/scene_renderer.cpp#L1243-L1423)
- **Technical Description:** Forward PBR currently rasterizes geometry directly to the HDR color buffer without a preceding depth-only pass. In Sponza, columns, arches, and layered curtains result in visual depth complexity.
- **Critical Evidence Against Pure Overdraw:** Scalability benchmarks (`docs/architecture/graphics_settings_and_scalability.md`) demonstrate that the main PBR pass duration remains invariant across resolutions ($19.00\text{ ms}$ at 0.75x vs $19.10\text{ ms}$ at 1.25x) and polygon wireframe mode ($18.88\text{ ms}$). Furthermore, the directional shadow pass (zero fragment shading) consumes $15.81\text{ ms}$. This indicates that vertex processing, push constant updates, and draw-call submission overhead constitute major bottlenecks on Pascal P106-100.
- **Roadmap Strategy for Milestone 11B:** A Depth Pre-pass is **not unconditionally mandatory**. Adding an Early-Z pre-pass introduces 103 additional draw calls and a third vertex pass over the 262k triangles. Its net benefit must be empirically benchmarked against vertex cost before being made permanent.

---

### RISK-05: Cross-GPU Presentation Overhead in Dual-GPU Topology
- **Source Location:** [`engine/platform/`](../../engine/platform/), [`engine/rhi/src/vulkan/vulkan_device.cpp`](../../engine/rhi/src/vulkan/vulkan_device.cpp)
- **Technical Description:** The developer rig operates a dual-GPU topology: rendering executes on an NVIDIA Pascal P106-100 (headless discrete GPU with no video outputs), while display presentation occurs via an NVIDIA GeForce GT 1030 connected to the monitor.
- **System Impact:** Rendered backbuffers must be copied across the PCIe bus via PRIME offload or host memory blits to the display GPU, introducing 1-3 ms of latency and PCIe bandwidth consumption.
- **Mitigation Requirement:** Maintain strict separation between offscreen render target operations and presentation buffers. Ensure presentation timing telemetry distinguishes GPU execution time from swapchain present wait time.

---

### RISK-06: Viewport Texture Descriptor Set Invalidation
- **Source Location:** [`editor/src/viewport_panel.cpp`](../../editor/src/viewport_panel.cpp), [`editor/src/editor_engine.cpp:681`](../../editor/src/editor_engine.cpp#L681)
- **Technical Description:** The editor binds offscreen Vulkan images to ImGui via `ImGui_ImplVulkan_AddTexture()`. During window resizing or graphics settings changes, the offscreen image is recreated.
- **Current Safeguard:** Safety currently relies on `device_->waitIdle()` invoked at frame boundaries before calling `ImGui_ImplVulkan_RemoveTexture()` on queued descriptors.
- **Mitigation Requirement:** Maintain the deferred descriptor retirement queue (`pendingDescriptorFrees_`) and retain `waitIdle()` synchronization until true multi-frame fence retiring is implemented.

---

### RISK-07: Synchronous CPU Fence Stall in VulkanDevice::submit
- **Source Location:** [`engine/rhi/src/vulkan/vulkan_device.cpp:1310-1327`](../../engine/rhi/src/vulkan/vulkan_device.cpp#L1310-L1327)
- **Technical Description:** `VulkanDevice::submit()` currently creates a temporary `VkFence`, calls `vkQueueSubmit`, and immediately invokes `vkWaitForFences()`, synchronously stalling the CPU until the GPU finishes executing all recorded commands for that frame.
- **Measured Impact:** Profiling telemetry reveals a CPU stall duration of **$35.15\text{ ms}$** waiting inside `vkWaitForFences`, completely serializing CPU recording and GPU execution (zero frame overlap).
- **Roadmap Consequence:** Blocks Milestone 11B throughput goals. Eliminating this stall requires refactoring RHI submission to support asynchronous double/triple buffered frames in flight with decoupled per-frame fences.
