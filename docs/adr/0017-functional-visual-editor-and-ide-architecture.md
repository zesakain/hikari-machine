# ADR-0017: Functional Visual Editor & IDE Architecture

## Status
Accepted

## Context
Hikari previously contained a basic Dear ImGui context setup that recorded draw lists without rasterizing them to the GPU, leaving the editor window visually blank. The editor requires an interactive, complete IDE desktop layout (menu bar, toolbar, hierarchy, inspector, viewport, content browser, console, metrics, conductor monitor) while strictly obeying:
1. Engine headers must not include editor code.
2. Only `/engine/rhi` or private implementations may touch graphics API headers.
3. No Vulkan types may leak into public engine headers.
4. Non-editor builds must compile zero editor code or ImGui symbols.
5. Headless test presets (such as unit tests) must run cleanly without GPU devices or window surfaces.

## Decision
1. **Opaque Native Handle Bridge:**
   - Introduced `NativeDeviceHandles` in `engine/rhi/include/rhi/rhi_device.h` and `NativeCommandListHandles` in `engine/rhi/include/rhi/command_list.h` using standard layout POD structures containing opaque pointers (`void*`) and standard integers (`uint32_t`).
   - Completely avoids leaking Vulkan types or requiring `<vulkan/vulkan.h>` in public RHI interfaces.
   - Headless `NullDevice` and `NullCommandList` return empty structs with `nullptr`, ensuring headless execution passes without hardware.

2. **Dear ImGui Vulkan Dynamic Rendering Backend:**
   - Integrated `backends/imgui_impl_vulkan.cpp` into `imgui_static` inside `/editor`.
   - Initialized with Vulkan 1.3 Dynamic Rendering (`UseDynamicRendering = true`), dynamic scissor and viewport, and automated font atlas descriptor pool allocation.
   - Dispatches draw calls via `ImGui_ImplVulkan_RenderDrawData` directly into the open dynamic render pass before `vkCmdEndRendering`.

3. **Modular IDE Panels:**
   - **Main Menu Bar:** File, Edit, View, Window, Entity, Assets, Tools, Help.
   - **Main Toolbar:** Play, Pause, Step simulation controls, Conductor state badge, and FPS telemetry.
   - **Hierarchy Panel:** Full Flecs ECS entity query, search filter, selection, and entity creation/destruction.
   - **Inspector Panel:** Inspects and modifies `PhysicsTransformComponent`, `RigidBodyComponent`, `ScriptComponent`, and component lifecycle.
   - **Scene Viewport Panel:** Viewport resolution, aspect ratio, dark canvas, coordinate crosshairs, grid, and honest empty-scene baseline indicators.
   - **Content Browser Panel:** Virtual asset filesystem tree (`assets/`, `data/`, `game/`, `docs/`).
   - **Log Console Panel:** Ring-buffer peek viewer with severity filters (Info, Warning, Error), search, and clear.
   - **Profiler & Telemetry Panel:** Real-time FPS, frame time, draw calls, uploaded vertices/indices, pass counter, and ECS entity count.
   - **HIKARI EDITOR Diagnostic Window:** Displays active GPU device, font atlas, and operational state.

## Consequences
- The editor renders visibly with full typography, styled windows, dockable panels, and responsive widgets on physical hardware.
- Headless simulation tests continue to execute deterministically without Vulkan dependencies.
- Non-editor builds (`debug`, `release`, `profile`, `asan`, `tsan`) remain 100% free of ImGui symbols or overhead.
