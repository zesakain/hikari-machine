# ADR-0010: Vulkan RHI Backend Architecture

- **Status**: Accepted
- **Date**: 2026-11-07
- **Authors**: zesakain

## Context

Hikari requires a high-performance, modern graphics API backend for desktop platforms. The first target is fast-paced action gameplay on PC hardware, with our test floor being NVIDIA Pascal architecture (dual GPU GTX 1030).
Per Hard Rule 3 in `AGENTS.md`: "Only `/engine/rhi` may include graphics API headers (Vulkan, D3D12, Metal). No vendor `#ifdef` outside the RHI."
Per Section 8: "All rendering decisions use `GpuCaps`, never GPU names or vendor IDs. Feature tiers: Tier 0 (baseline) must always work."

## Decision

1. **Vulkan 1.3+ Baseline with Dynamic Rendering**:
   - The Vulkan backend targets Vulkan 1.3 core features: `VK_KHR_dynamic_rendering` (eliminating `VkRenderPass` and `VkFramebuffer` boilerplate) and `VK_KHR_timeline_semaphore` (unified queue synchronization).
   - All Vulkan headers (`<vulkan/vulkan.h>`) are strictly encapsulated inside private implementation files under `engine/rhi/src/vulkan/`. Zero Vulkan types leak into public RHI headers.

2. **Device Discovery & Capability Mapping**:
   - Physical devices are enumerated and scored based on device type (prefer Discrete GPU over Integrated) and feature availability.
   - Discovered limits and feature flags are mapped directly into the backend-independent `GpuCaps`, `GpuLimits`, and `GpuFeatures` structs.
   - Hardware tiers are assigned algorithmically: Pascal (GTX 1030) maps to `HardwareTier::Tier0`.

3. **Validation Layers & Debug Messenger**:
   - Validation layer `VK_LAYER_KHRONOS_validation` and `VK_EXT_debug_utils` are queried dynamically at runtime.
   - In development/debug builds, validation is enabled if available. If unavailable (e.g. system without validation packages installed), the backend falls back gracefully to standard execution without silently failing initialization.

4. **Resource Management & Generational Handles**:
   - All GPU resources (`VkBuffer`, `VkImage`, `VkImageView`, `VkSampler`) are managed through generational handle tables (`BufferHandle`, `TextureHandle`, `SamplerHandle`).
   - Stale or invalid handles are detected and rejected in O(1) time without triggering undefined behavior.

5. **GPU Memory Allocation**:
   - A dedicated and block-managed `VulkanMemoryManager` handles device local and host-visible/coherent allocations via Vulkan physical device memory type indexing.
   - Host staging buffers are recycled for CPU-to-GPU memory copies during command buffer execution.

6. **SDL3 Surface & Swapchain**:
   - Swapchain creation is driven via SDL3 (`SDL_Vulkan_CreateSurface`).
   - Double/triple buffering is supported with fallback to `VK_PRESENT_MODE_FIFO_KHR`.
   - Window minimization (0x0 extent) is safely handled by skipping swapchain presentation until window restoration.
   - Null RHI remains completely independent and runnable in headless CI without windowing or swapchains.

## Consequences

### Positive
- Production-grade Vulkan execution matching the architectural baseline.
- Zero graphics API leakage outside the RHI layer.
- Retains 100% testability on headless CI via `NullDevice`.

### Negative
- Requires maintaining Vulkan-specific synchronization, barrier translation, and swapchain recreation logic.
