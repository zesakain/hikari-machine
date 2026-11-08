# Rendering Pipeline & Shader Architecture

This document describes Hikari's GPU rendering pipeline and shader module subsystem implemented in **Milestone 8A**.

---

## 1. Architectural Motivation & Context

Prior to Milestone 8A, a comprehensive technical audit (`docs/architecture/rendering_pipeline_audit.md`) revealed that while Hikari possessed GPU vertex buffers, index buffers, and render graph abstractions, actual 3D geometry visible in the editor viewport was rasterized on the CPU into Dear ImGui 2D draw lists. The RHI lacked graphics pipelines, shader modules, pipeline layouts, and push constants.

Milestone 8A establishes the backend-independent RHI graphics pipeline and shader foundation required for true GPU-driven 3D rendering.

---

## 2. Abstraction Layer

The pipeline abstraction lives entirely in `/engine/rhi` and is completely decoupled from any specific graphics API:

### 2.1 Core Types & Handles
- `ShaderHandle = ResourceHandle<ShaderTag>`: Generational handle indexing an immutable shader bytecode module.
- `PipelineLayoutHandle = ResourceHandle<PipelineLayoutTag>`: Generational handle defining the descriptor set layouts and push constant ranges.
- `PipelineHandle = ResourceHandle<PipelineTag>`: Generational handle referencing a compiled GPU graphics pipeline state object (PSO).

### 2.2 Shader Modules (`engine/rhi/include/rhi/shader.h`)
`ShaderDesc` encapsulates SPIR-V bytecode:
```cpp
struct ShaderDesc {
    ShaderStageFlags stage{ShaderStageFlags::None};
    std::span<const uint32_t> spirvCode{};
    std::string_view entryPoint{"main"};
    std::string_view debugName{};
};
```
Bytecode is strictly validated prior to device submission:
- Verifies SPIR-V magic word `0x07230203U`.
- Ensures length is at least the 5-word standard header.
- Asserts valid stage flags and non-empty entry point strings.

### 2.3 Pipeline Layouts & Push Constants (`engine/rhi/include/rhi/pipeline.h`)
- Push constant ranges define stage visibility, byte offset, and size (up to 128 bytes baseline).
- Descriptor set layout bindings declare sets, bindings, descriptor types (`UniformBuffer`, `SampledImage`, etc.), and counts.
- `PipelineLayoutDesc` combines push constants and descriptor layouts.

### 2.4 Graphics Pipeline State (`GraphicsPipelineDesc`)
Comprehensive state specification:
- **Shaders**: `vertexShader` and optional `fragmentShader` handles.
- **Vertex Input Layout**: Bindings (stride, rate) and attribute locations/formats/offsets.
- **Input Assembly**: `PrimitiveTopology` (TriangleList, TriangleStrip, LineList, PointList).
- **Rasterization State**: `PolygonMode` (Fill, Line, Point), `CullMode` (None, Front, Back), `FrontFace` (CCW, CW), depth bias.
- **Depth/Stencil State**: Depth test/write enables, compare operators (`Less`, `Equal`, etc.).
- **Color Blend State**: Blend enable, color/alpha blend factors, ops, and write masks.
- **Dynamic Rendering Formats**: Color attachment formats and depth attachment format for render pass-free rendering.

---

## 3. Backends

### 3.1 Vulkan 1.3 Backend (`engine/rhi/src/vulkan/`)
- **Dynamic Rendering**: Pipelines are compiled using `VkPipelineRenderingCreateInfo` in `VkGraphicsPipelineCreateInfo::pNext`, matching Vulkan 1.3 `vkCmdBeginRendering` workflows without requiring legacy `VkRenderPass` or `VkFramebuffer` handles.
- **Dynamic Viewport & Scissor**: Configured with `VK_DYNAMIC_STATE_VIEWPORT` and `VK_DYNAMIC_STATE_SCISSOR`, allowing viewport resize events without expensive pipeline recreations.
- **Command Recording & Draw Safety**:
  - `bindPipeline(PipelineHandle)` binds the underlying `VkPipeline` and caches the active pipeline state.
  - `pushConstants(...)` delegates to `vkCmdPushConstants`.
  - Draw commands (`draw`, `drawIndexed`) verify that a valid graphics pipeline is bound. Attempting to record draw commands without a bound pipeline logs a diagnostic warning and safely suppresses invalid driver submission.

### 3.2 Null RHI Backend (`engine/rhi/src/null/`)
- Fully supports headless CI, unit tests, and determinism benchmarks without a physical GPU.
- Implements slot-mapped generational lifecycle tracking for shaders, pipeline layouts, and graphics pipelines.
- `NullCommandList` tracks active pipeline bindings and records unbound draw warnings for headless verification.

---

## 4. Testing & Verification

The graphics pipeline foundation is covered by:
1. **Hermetic Test Shaders (`tests/unit/rhi/test_shaders.h`)**: Minimal, validated SPIR-V bytecodes for vertex (with push constants and attributes) and fragment shaders embedded directly in source, removing build-machine dependencies on external GLSL compilers.
2. **`null_rhi_test`**:
   - `testShaderLifecycleAndValidation()`
   - `testPipelineLayoutLifecycleAndValidation()`
   - `testGraphicsPipelineLifecycleAndValidation()`
   - `testCommandListPipelineBindingAndValidation()`
3. **`vulkan_rhi_test`**:
   - Real GPU shader module creation (`vkCreateShaderModule`).
   - Descriptor set layout and pipeline layout creation (`vkCreatePipelineLayout`).
   - Graphics pipeline compilation with dynamic rendering (`vkCreateGraphicsPipelines`).
   - Command recording with pipeline binding, push constants, and indexed drawing.

---

## 5. Next Steps: Milestone 8B

Milestone 8B will consume this foundation to implement true GPU-driven offscreen viewport rendering:
1. Creating an offscreen color texture and depth attachment at viewport resolution.
2. Building an offscreen render target framebuffer/dynamic rendering pass.
3. Updating `SceneRenderer` to bind pipelines, upload camera matrices via push constants, and draw meshes on the GPU.
4. Exposing the offscreen color texture to Dear ImGui via `ImGui_ImplVulkan_AddTexture` for display in `ViewportPanel`.
