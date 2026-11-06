# GPU Hardware Notes and Capability Tiers

## Hardware Neutrality

Hikari-Machine is designed to run across multiple GPU architectures without embedding vendor-specific hacks or hardware-name branching into high-level systems.

The development environment (e.g. NVIDIA Pascal, dual GPU) serves solely as a **test floor**, not an architectural design target. All rendering decisions and pipeline permutations are selected via structured capability flags (`GpuCaps`).

## Capability Tiers

### Tier 0: Baseline Target (Mandatory Support)
- **API Target**: Vulkan 1.3 core specification.
- **Hardware Profile**: Desktop GPUs with feature sets equivalent to modern baseline architectures (e.g., Pascal / Polaris and newer).
- **Core Requirements**:
  - Unified memory allocation via Vulkan Memory Allocator (VMA).
  - Dynamic rendering (`VK_KHR_dynamic_rendering`).
  - Synchronization 2 (`VK_KHR_synchronization2`).
  - Descriptor indexing / bindless sampling baseline where supported, with robust fallback descriptor sets.
- **Rule**: All features must function correctly and performantly on Tier 0 hardware before advanced tiers are enabled.

### Tier 1 / Tier 2: Enhanced Paths (Optional)
- Mesh shading (`VK_EXT_mesh_shader`).
- Hardware ray tracing (`VK_KHR_ray_tracing_pipeline` / query).
- Advanced variable rate shading.
- **Rule**: Advanced tier features are strictly optional paths. A Tier 0 fallback is mandatory for every high-tier feature.

## RHI and Headless Execution

A `null` RHI backend is maintained alongside the Vulkan backend:
- Executes simulation logic and command recording validations without an active GPU context or windowing server.
- Used in continuous integration to validate render graphs, resource handles, and shader compilation pipelines.
