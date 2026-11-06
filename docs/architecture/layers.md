# Layer Boundaries and Dependency Direction

## Architectural Layers

The repository enforces a strict, unidirectional layering hierarchy:

```
+-------------------------------------------------------------+
|                            Game                             |
+-------------------------------------------------------------+
|                           Editor                            |
+-------------------------------------------------------------+
|                 Tools (cooker, data tools)                  |
+=============================================================+
|                      Engine Framework                       |
|                 (Conductor, Mode, Clocks)                   |
+------------------------------+------------------------------+
|      Engine Simulation       |       Engine Rendering       |
| (World, Physics, Anim, Audio)| (Render Graph, Renderer, RHI)|
+------------------------------+------------------------------+
|                       Engine Platform                       |
|                   (OS, Window, Input)                       |
+-------------------------------------------------------------+
|                         Engine Core                         |
|     (Memory, Allocators, Handles, Math, Jobs, Logging)      |
+-------------------------------------------------------------+
```

## Layer Constraints

1. **Strict Downward Dependencies**:
   - A layer may only depend on layers below it.
   - Core may not depend on Platform, Simulation, Rendering, or Framework.
   - Simulation and Rendering may only depend on Platform and Core.
   - Framework may depend on Simulation, Rendering, Platform, and Core.
   - Game and Editor depend on Engine, but Engine must never depend on Game or Editor.

2. **No Game Vocabulary in Engine**:
   - `/engine` contains generic capabilities only. Game concepts (e.g. character names, special attack names, overheat meters, score combos) belong exclusively in `/game`.

3. **Graphics API Isolation (RHI)**:
   - Only `/engine/rhi` may include graphics API headers (such as Vulkan, D3D12, or Metal headers).
   - The rest of the engine, including `/engine/render`, interacts solely with abstract RHI interfaces.
   - Vendor `#ifdef` blocks are strictly forbidden outside the RHI implementations.
   - A `null` RHI backend is maintained to allow headless CI and automated simulation testing without a physical GPU.

4. **Editor Isolation**:
   - Editor code is strictly partitioned and guarded behind the `EDITOR` compilation flag.
   - Non-editor builds (runtime and headless) must contain zero editor symbols, headers, or Dear ImGui code.

5. **Header Placement**:
   - Public engine headers must reside in `engine/<module>/include/<module>/`.
   - Private module headers must remain inside the module's source directories and never be exposed in public include search paths.
