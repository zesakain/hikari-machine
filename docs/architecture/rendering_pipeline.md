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

## 5. Milestone 8B.1: Offscreen Rendering Infrastructure

Milestone 8B.1 establishes the offscreen render target and dynamic rendering attachment infrastructure required for GPU scene rendering into non-swapchain textures.

### 5.1 Generalized Dynamic Render Pass Begin Info (`engine/rhi/include/rhi/command_list.h`)
The `RenderPassBeginInfo` structure is generalized to accept arbitrary GPU-owned texture handles while maintaining seamless backward compatibility for swapchain presentation:
```cpp
struct RenderPassBeginInfo {
    TextureHandle colorTarget{};            // Invalid handle = default swapchain presentation
    TextureHandle depthTarget{};            // Optional depth texture handle
    float clearColor[4]{0.08F, 0.12F, 0.20F, 1.0F};
    float clearDepth{1.0F};
    bool clearTarget{true};
    bool clearDepthTarget{true};
    uint32_t renderWidth{0};                // 0 = infer from target dimensions
    uint32_t renderHeight{0};               // 0 = infer from target dimensions
};
```

### 5.2 Vulkan 1.3 Image Layout Lifecycle & Transitions (`engine/rhi/src/vulkan/`)
- **Render Pass Entry (`beginRenderPass`)**:
  - Validates attachment handles, usage flags (`ColorAttachment`, `DepthStencilAttachment`), and formats (`isColorFormat`, `isDepthFormat`).
  - Ensures dimensions of color and depth targets match.
  - Inserts dynamic pipeline barriers (`VkImageMemoryBarrier2`) transitioning the offscreen color image from its current layout (`UNDEFINED` or previous layout) to `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` (`COLOR_ATTACHMENT_WRITE` access).
  - Transitions the offscreen depth image to `VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL` (`DEPTH_STENCIL_ATTACHMENT_WRITE` access).
  - Configures `VkRenderingAttachmentInfo` for color and depth, specifying clear/load/store ops.
  - Begins dynamic rendering via `vkCmdBeginRendering` and configures dynamic viewport and scissor.
- **Render Pass Exit (`endRenderPass`)**:
  - Invokes `vkCmdEndRendering`.
  - Inserts dynamic pipeline barrier transitioning the offscreen color target to `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` (`SHADER_READ` access). This guarantees that downstream consumers (e.g. Dear ImGui viewport panel or post-processing passes) can safely sample the texture without hazard.
  - Transitions the offscreen depth target to `VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL`.
  - Updates the tracked layout in `VulkanTextureSlot`.
- **Swapchain Fallback Preservation**:
  - When `colorTarget` is default-initialized (`{}`), the command list seamlessly selects the existing swapchain presentation path, maintaining 100% backward compatibility.

### 5.3 Format Compatibility Validation
- During `draw` and `drawIndexed`, both `NullCommandList` and `VulkanCommandList` validate that the formats declared in the bound `GraphicsPipelineDesc` match the active attachments:
  - Pipeline color attachment format matches active render pass color attachment format.
  - Pipeline depth attachment format matches active render pass depth attachment format.
- Mismatches log diagnostic warnings and safely suppress execution to prevent GPU driver faults.

### 5.4 Null Backend Parity & Negative Testing (`engine/rhi/src/null/`)
- `NullDevice` validates texture descriptors (`TextureFormat::Undefined` rejection, dimension limit checks) and implements `getTextureDesc(TextureHandle)`.
- `NullCommandList` implements comprehensive state validation:
  - Tracks active color and depth targets and formats.
  - Detects orphan `endRenderPass()` calls outside an active pass.
  - Detects nested `beginRenderPass()` calls.
  - Validates matching dimensions between color and depth attachments.
  - Flags invalid handles and format mismatches.

---

## 6. Next Steps: Milestone 8B.2

Milestone 8B.2 will connect this offscreen rendering infrastructure to the editor scene viewport:
1. Update `SceneRenderer` to manage viewport-resolution color and depth targets.
2. Bind graphics pipelines and upload scene camera/view-projection uniform buffers or push constants.
3. Draw meshes using GPU vertex/index buffers.
4. Expose the offscreen color texture to Dear ImGui via descriptor set / `ImGui_ImplVulkan_AddTexture` for presentation in `ViewportPanel`.
5. Verify end-to-end rendering before retiring the fallback CPU rasterizer.
