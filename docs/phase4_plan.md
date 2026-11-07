# Hikari Engine — Phase 4 Implementation Plan
## Vulkan RHI Backend & Render Graph

---

## 1. Architectural Objective

Phase 4 establishes the hardware rendering foundation of the Hikari Engine:
1. **Vulkan RHI Backend (`engine/rhi/vulkan`)**: A complete Vulkan 1.3+ backend implementing the authoritative `RhiDevice` and `RhiCommandList` contracts without leaking Vulkan headers into backend-independent code.
2. **Surface & Swapchain (`VulkanSwapchain`)**: Integration with the SDL3 platform layer for presentation, window resizing, and minimized-window handling, while maintaining full headless testability.
3. **Render Graph (`engine/render` or `engine/rhi`)**: A backend-independent directed acyclic graph (DAG) scheduler managing passes, resource read/write dependencies, topological execution ordering, cycle detection, transient resource identification, and barrier/transition planning.
4. **Preservation of Core & Phase 1-3 Systems**: `NullDevice` remains fully functional and independently testable. All 19 existing tests continue to pass across all 6 presets (`debug`, `release`, `profile`, `editor`, `asan`, `tsan`).

---

## 2. Module Boundaries & Repository Layout

```
engine/
├── rhi/
│   ├── include/rhi/            # Authoritative RHI public interface (unchanged)
│   │   ├── rhi_types.h         # Handles, flags, formats, enums
│   │   ├── gpu_caps.h          # Hardware tiers, limits, features
│   │   ├── buffer.h            # BufferDesc
│   │   ├── texture.h           # TextureDesc
│   │   ├── sampler.h           # SamplerDesc
│   │   ├── command_list.h      # RhiCommandList abstract interface
│   │   └── rhi_device.h        # RhiDevice abstract interface & factories
│   └── src/
│       ├── null/               # Null RHI backend (unchanged, headless)
│       └── vulkan/             # Vulkan RHI backend (strictly private to engine/rhi)
│           ├── vulkan_instance.h / .cpp       # Instance, validation layers, debug messenger
│           ├── vulkan_physical_device.h / .cpp # Enumeration, scoring, queue selection, GpuCaps
│           ├── vulkan_device.h / .cpp          # Logical device, queues, resource pools
│           ├── vulkan_memory.h / .cpp          # GPU memory management & type lookup
│           ├── vulkan_resources.h              # Buffer, Texture, Sampler slot definitions
│           ├── vulkan_command_list.h / .cpp    # Command recording, barriers, submissions
│           └── vulkan_swapchain.h / .cpp       # Surface, swapchain, present synchronization
├── render/
│   ├── include/render/
│   │   ├── render_graph.h      # RenderGraph declaration, pass builder, dependency tracking
│   │   └── render_graph_types.h # Resource nodes, pass nodes, barrier planning
│   └── src/
│       └── render_graph.cpp    # DAG compiler, topological sort, cycle detection, execution
```

---

## 3. Subsystem Breakdown

### 3.1 Vulkan Instance & Debug Diagnostics
- Enumerates available instance layers and extensions.
- Detects presence of `VK_LAYER_KHRONOS_validation`. If available and debug mode is enabled, activates validation layers with `VK_EXT_debug_utils`. If absent, gracefully falls back without failing initialization.
- Hooks diagnostic callback to `ENG_LOG_DEBUG`, `ENG_LOG_WARN`, and `ENG_LOG_ERROR`.
- Dynamically loads entry points via Vulkan loader.

### 3.2 Physical Device Selection & GpuCaps Mapping
- Evaluates candidate GPUs by API version (minimum 1.3 for dynamic rendering and timeline semaphores, or 1.2 with extensions).
- Checks required extensions: `VK_KHR_swapchain`, `VK_KHR_dynamic_rendering`, `VK_KHR_timeline_semaphore`.
- Queries queue families: identifies graphics family and presentation support.
- Maps limits (`maxImageDimension2D`, `maxColorAttachments`, `maxStorageBufferRange`) into `GpuLimits`.
- Maps features into `GpuFeatures`: `dynamicRendering = true`, `timelineSemaphores = true`, `descriptorIndexing`, `rayTracing`, `meshShaders`.
- Classifies hardware tier: GTX 1030 (Pascal) -> `Tier0`.

### 3.3 Logical Device, Queues, & Synchronization
- Creates logical `VkDevice` enabling `dynamicRendering` and `timelineSemaphore` feature structs.
- Obtains graphics queue and present queue.
- Manages command pool (`VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT`).
- Implements `waitIdle()` using `vkDeviceWaitIdle()`.

### 3.4 GPU Memory & Resource Management
- Implements `VulkanMemoryManager`:
  - Queries physical device memory properties (`VkPhysicalDeviceMemoryProperties`).
  - Finds compatible memory type index matching resource memory requirements and `MemoryUsage` (`GpuOnly`: `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`; `CpuToGpu`: `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT`).
  - Generational handle tables for `BufferHandle`, `TextureHandle`, and `SamplerHandle` with stale-handle rejection.
  - Pre-warmed capacity, safe destruction, and leak-free resource release.

### 3.5 Command Recording & Submission
- `VulkanCommandList` implements `RhiCommandList`.
- Manages `VkCommandBuffer` lifecycle (`begin()`, `end()`).
- Translates dynamic state: `vkCmdSetViewport`, `vkCmdSetScissor`.
- Binds vertex and index buffers (`vkCmdBindVertexBuffers`, `vkCmdBindIndexBuffer`).
- Issues draw calls: `vkCmdDraw`, `vkCmdDrawIndexed`.
- Host updates: staging buffer allocation and `vkCmdCopyBuffer`.
- Pipeline barriers: explicit `VkImageMemoryBarrier2` / `VkBufferMemoryBarrier2` using Vulkan synchronization2.
- Submission: queue submission with `VkFence` or semaphores, tracking in-flight status.

### 3.6 SDL3 Surface & Swapchain
- Encapsulates surface creation via `SDL_Vulkan_CreateSurface(window->getNativeHandle())`.
- Queries surface capabilities, formats, and present modes.
- Creates `VkSwapchainKHR` with double/triple buffering (`VK_PRESENT_MODE_FIFO_KHR` or `VK_PRESENT_MODE_MAILBOX_KHR`).
- Retrieves swapchain images, creates `VkImageView` for each image.
- Per-frame acquisition semaphores and render-complete semaphores.
- Handles `VK_ERROR_OUT_OF_DATE_KHR` and `VK_SUBOPTIMAL_KHR` by recreating the swapchain on resize.
- Gracefully handles minimized windows (0x0 extent) by pausing swapchain rendering.

### 3.7 Render Graph
- Abstract, backend-agnostic directed acyclic graph.
- Pass registration: `addPass(name, setupLambda, executeLambda)`.
- Resource handles: `GraphBufferId`, `GraphTextureId`.
- Access tracking: read/write dependencies with usage flags.
- Graph compilation:
  - Adjacency list construction.
  - Cycle detection using depth-first search / Kahn's algorithm.
  - Topological sorting to produce deterministic execution order.
  - Resource lifetime analysis (tracks first-used and last-used pass for memory aliasing/reuse).
  - Resource state transitions (determines before-state and after-state for every pass).
- Execution:
  - Iterates compiled passes in topological order.
  - Emits resource barriers if RHI backend supports them.
  - Invokes pass execute callbacks passing `RhiCommandList&`.
  - Tested with `NullDevice` for complete headless CI validation.

---

## 4. Test Strategy
1. **Unit Tests**:
   - `tests/unit/render/render_graph_test.cpp`:
     - Linear dependency execution ordering.
     - Multi-branch diamond dependency ordering.
     - Cycle detection (must detect cycle and return error).
     - Unused pass culling.
     - Resource lifetime and state transition tracking.
     - Headless execution with `NullDevice`.
   - `tests/unit/rhi/vulkan_caps_test.cpp`:
     - Capability mapping from Vulkan properties to `GpuCaps`.
     - Memory type selection logic.
     - Resource descriptor boundary validation.
2. **Integration Tests**:
   - `tests/unit/rhi/vulkan_device_test.cpp`:
     - Instance and device creation.
     - Buffer creation, CPU staging, copy, and destruction.
     - Texture creation and view generation.
     - Command recording, barrier insertion, and queue submission.
     - Graceful skip if running in headless environment without Vulkan GPU.
3. **Multi-Preset Verification**:
   - All 6 presets (`debug`, `release`, `profile`, `editor`, `asan`, `tsan`).
   - Clean `clang-format` and `clang-tidy`.
