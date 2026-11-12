# Hikari Engine Architecture: Resource Lifetime, Ownership & Synchronization

**Document ID:** `ARCH-RES-2026-REV1`  
**Governing Standard:** Repository Contract [`/AGENTS.md`](../../AGENTS.md) Section 5 (Memory Management)  
**Parent Document:** [`hikari_engine_architecture.md`](hikari_engine_architecture.md)  
**Related Documents:** [`hikari_application_lifecycle.md`](hikari_application_lifecycle.md), [`hikari_rendering_architecture.md`](hikari_rendering_architecture.md), [`hikari_asset_pipeline_architecture.md`](hikari_asset_pipeline_architecture.md)  

---

## 1. Overview & Architectural Role

Hikari strictly adheres to `/AGENTS.md` Rule 5 (Memory Management):
- **Single Clear Ownership:** Every heap or GPU resource has exactly one owning object. Ownership is encoded in types (`std::unique_ptr`, owning manager classes), never in comments.
- **No Shared Pointers:** `std::shared_ptr` is prohibited in engine code. Non-owning access is granted exclusively through references, `std::span`, `std::string_view`, or generational handles (`AssetId`, `EntityHandle`).
- **No Raw Allocations:** Raw `new`/`delete` and `malloc`/`free` are forbidden in engine code.
- **GPU Synchronization First:** No GPU resource is freed while referenced by in-flight command buffers.

---

## 2. Comprehensive Resource Ownership Matrix

The following matrix documents the complete lifecycle, ownership, invalidation triggers, and destruction strategies for every resource across Hikari Engine:

| Resource Type | Creator | Primary Owner | Non-Owning Borrowers | Invalidation Triggers | Destroyer | Strategy | GPU Sync Required |
|---|---|---|---|---|---|---|---|
| **ECS World** | `src/main.cpp` | `main()` / `world::World` | `framework::Conductor`, Systems, Panels, Renderer | Engine shutdown, Scene clear | Scope termination | Immediate | None |
| **ECS Entities** | `SceneSerializer` / Importers | `world::World` (Flecs) | `EntityHandle` references | Scene clear, entity destruction | `World::destroyEntity()` | Deferred (end of tick) | None |
| **Vulkan Device & Instance** | `rhi::createVulkanDevice` | `main()` (`std::unique_ptr`) | All RHI resources | Engine shutdown | `VulkanDevice::~VulkanDevice` | Immediate at exit | `device->waitIdle()` |
| **Vulkan Swapchain** | `VulkanDevice` | `VulkanDevice` | Window, ImGui presenter | Window resize, VSync toggle, display change | `VulkanDevice::recreateSwapchain` | Immediate after drain | `vkDeviceWaitIdle` |
| **GPU Buffers (Vertex/Index)** | `AssetManager` | `AssetManager` (`GpuMesh`) | `MeshComponent`, CommandList | Asset unload, cache clear, shutdown | `AssetManager::unloadMesh` | Immediate | Must not be in-flight |
| **GPU Textures & Views** | `AssetManager` | `AssetManager` (`GpuTexture`) | `MaterialComponent`, Shaders | Asset unload, cache clear, shutdown | `AssetManager::unloadTexture` | Immediate | Must not be in-flight |
| **GPU Samplers** | `AssetManager` / `SceneRenderer` | Owning subsystem | Shaders, ImGui backend | Settings change (filtering), shutdown | Owner destructor | Immediate | Must not be in-flight |
| **Vulkan Pipelines** | `SceneRenderer` | `SceneRenderer` | `CommandList` execution | Shader reload, pipeline config, shutdown | `SceneRenderer::~SceneRenderer` | Immediate | Must not be in-flight |
| **Offscreen Render Targets** | `SceneRenderer` | `SceneRenderer` | ViewportPanel, Pass descriptors | Viewport resize, HDR/Shadow settings apply | `SceneRenderer::resizeTargets` | Immediate after drain | `vkDeviceWaitIdle` |
| **ImGui Viewport Descriptors** | `ViewportPanel` | `editor::EditorEngine` | `ImGui::Image` drawing | Viewport resize, Target recreation | `EditorEngine` deferred queue | **Deferred** (`pendingDescriptorFrees_`) | Complete frame fence |
| **Jolt Physics Bodies** | `physics::registerPhysicsSystems` | `physics::PhysicsWorld` | `PhysicsBodyComponent` | Entity removal, Scene clear | `PhysicsWorld::destroyBody` | Immediate | None |
| **Lua Script State** | `ScriptEngine` | `ScriptEngine` | Script systems | Hot reload, mode reset, shutdown | `ScriptEngine::~ScriptEngine` | Immediate | None |

---

## 3. Resource Ownership Hierarchy Diagram

```mermaid
graph TD
    App["Application Entry Point (src/main.cpp)"] --> Plat["platform::Platform (SDL3)"]
    App --> RHI["rhi::Device (VulkanDevice)"]
    App --> World["world::World (Flecs)"]
    App --> Phys["physics::PhysicsWorld (Jolt)"]
    App --> Asset["render::AssetManager"]
    App --> Rend["render::SceneRenderer"]
    App --> Edit["editor::EditorEngine"]

    RHI --> Vma["VMA Allocator (VRAM Pools)"]
    RHI --> Swap["VulkanSwapchain (Backbuffers)"]

    Asset --> GpuM["GpuMesh (Vertex/Index VkBuffers)"]
    Asset --> GpuT["GpuTexture (VkImage, VkImageView, VkSampler)"]

    Rend --> Offscreen["Offscreen Targets (HDR, Depth, Shadow, Bloom Mips)"]
    Rend --> Pipelines["Vulkan Pipelines (Shadow, PBR, Bloom, ToneMap)"]

    Edit --> ImGuiPool["VkDescriptorPool (Fonts & Viewport)"]
    Edit --> DefQueue["Deferred Descriptor Free Queue"]

    World -.->|EntityHandle Reference| App
    World -.->|AssetId Reference| Asset
    Edit -.->|VkDescriptorSet Borrow| Rend
```

---

## 4. GPU Synchronization, Command Submission & Resource Invalidation

### 4.1 Actual Synchronization Model: Synchronous Lockstep Submission

An audit of [`engine/rhi/src/vulkan/vulkan_device.cpp:1280-1337`](../../engine/rhi/src/vulkan/vulkan_device.cpp#L1280-L1337) reveals the actual submission and synchronization model currently implemented:

```mermaid
sequenceDiagram
    autonumber
    participant CPU as Main Thread (src/main.cpp)
    participant Dev as rhi::VulkanDevice
    participant Queue as VkQueue (Graphics)
    participant Fence as VkFence (Temporary)
    participant GPU as GPU Execution Pipeline

    CPU->>Dev: submit(mainCmd)
    Dev->>Dev: vkCreateFence(flags = 0)
    Dev->>Queue: vkQueueSubmit(submitInfo, fence)
    Note over CPU,Dev: SYNCHRONOUS STALL: CPU immediately blocks on fence!
    Dev->>Fence: vkWaitForFences(fence, timeout = 2s)
    GPU-->>Fence: Command buffer execution completes
    Fence-->>Dev: Fence signaled
    Dev->>Dev: vkDestroyFence(fence)
    Dev->>Dev: cmdList->cleanupStagingResources()
    Dev-->>CPU: submit() returns success
    CPU->>Dev: present()
```

#### Key Architectural Findings:
1. **Synchronous Command Submission:**  
   `VulkanDevice::submit()` does not pipeline multiple frames in flight. Instead, it creates a temporary fence per submission, calls `vkQueueSubmit`, and **immediately blocks the CPU thread with `vkWaitForFences()`** until the GPU finishes recording and executing that exact command buffer. This accounts for the `CPU vkWaitForFences: Avg 35.15 ms` stall duration observed in telemetry.
2. **Device-Wide Idle Waits (`waitIdle`) for Resource Safety:**  
   Resource invalidation and destruction across the engine explicitly rely on device-wide stalls via `device_->waitIdle()`:
   - **Render Target Resize:** [`engine/render/src/scene_renderer.cpp:850`](../../engine/render/src/scene_renderer.cpp#L850) calls `device_->waitIdle()` before destroying `colorTarget_`, `depthTarget_`, and bloom pyramid textures.
   - **Shadow Map Resize:** [`engine/render/src/scene_renderer.cpp:923`](../../engine/render/src/scene_renderer.cpp#L923) calls `device_->waitIdle()` before destroying `shadowDepthTarget_` and updating scene descriptors.
   - **Viewport Descriptor Cleanup:** [`editor/src/editor_engine.cpp:681`](../../editor/src/editor_engine.cpp#L681) calls `impl_->rhiDevice->waitIdle()` at the start of `beginFrame()` before calling `ImGui_ImplVulkan_RemoveTexture()` on descriptor sets queued in `pendingDescriptorFrees_`.
   - **Swapchain Recreation:** [`engine/rhi/src/vulkan/vulkan_device.cpp:1488`](../../engine/rhi/src/vulkan/vulkan_device.cpp#L1488) calls `waitIdle()` before destroying the old `VkSwapchainKHR` and its backbuffer image views.
   - **Engine Teardown:** [`src/main.cpp`](../../src/main.cpp) and `VulkanDevice::~VulkanDevice` ([`vulkan_device.cpp:54`](../../engine/rhi/src/vulkan/vulkan_device.cpp#L54)) execute `waitIdle()` prior to releasing any descriptor pools, pipelines, or VMA allocations.

3. **Planned Evolution (Milestone 11B):**  
   True asynchronous multi-frame pipelining (allowing CPU frame $N+1$ recording while GPU executes frame $N$) requires decoupling `VulkanDevice::submit` from `vkWaitForFences`, maintaining permanent per-frame fences in a ring buffer, and rotating descriptor sets per frame. This is a primary optimization target for Milestone 11B.
