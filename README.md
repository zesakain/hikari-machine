# Hikari-Machine

An experimental C++20 3D game engine targeting fast-paced action games, architected around a **Two-State Conductor over a Shared World**.

Repository: [https://github.com/zesakain/hikari-machine](https://github.com/zesakain/hikari-machine)  
License: [Apache 2.0](LICENSE)

---

## 1. Project Overview

Hikari-Machine is an experimental, mode-based 3D game engine under active development. Its defining architectural concept is the **Two-State Conductor operating over a single Shared World**:

- **Single Shared World:** All entities, components, and physics states reside in a unified Flecs ECS world. Switching gameplay modes does not require duplicating, reconstructing, or serializing the world state. Continuous simulation systems operate independently of gameplay mode transitions.
- **Two Time States (A and B):** The Conductor maintains exactly two alternating execution states (e.g., fast-paced combat and tactical precision). Exactly one state is active and authoritative at any given instant.
- **Asynchronous Standby Preparation:** The non-active state incrementally prepares its entry data, such as camera parameters, target queries, UI layouts, and resource requirements, within a configurable per-frame time budget.
- **Validated State Transitions:** Before switching roles, the Conductor validates the prepared state. Successful transitions commit the prepared state and support controlled slow-motion dilation during the transition curve. Failed validation preserves the currently active state.

> **Note on Project Scope:** Hikari-Machine is an experimental engine and architecture testbed. It is not a commercial, production-ready replacement for mature engines like Unreal Engine or Unity.

---

## 2. Current Status & Verified Features

The engine contains functional implementations across core systems, rendering, physics, scripting, UI, and editor tooling:

- **Core Primitives & Memory**:
  - Exception-free error handling with `Result<T, Error>`.
  - Custom allocators (Linear, Arena, Pool) and generational slot maps (`SlotMap`).
  - Lock-free work-stealing `JobSystem` with job dependencies.
  - Ring-buffer logger and monotonic high-resolution clock domains (`WorldClock`, `ActionClock`, `UiClock`).
- **Platform & Hardware Abstraction**:
  - SDL3 platform backend for windowing, OS event dispatch, and input handling.
  - Abstract RHI (`RhiDevice`, `RhiCommandList`, buffers, textures, samplers, pipelines, shaders).
  - Headless `NullDevice` for 100% offscreen simulation testing and CI.
  - Vulkan 1.3 dynamic rendering backend (`VulkanDevice`, `VulkanCommandList`, dedicated `VulkanMemoryManager`).
  - DAG-based `RenderGraph` scheduler with topological ordering and automated barrier placement.
- **Simulation & Framework**:
  - Unified simulation `World` encapsulating Flecs ECS with system group gating (`Continuous`, `StateA`, `StateB`).
  - Two-State `Conductor` runtime coordinating active and standby `Mode` lifecycles with time dilation.
  - Jolt Physics integration (`PhysicsWorld`) with fixed-timestep simulation and bidirectional ECS component synchronization (`PhysicsTransformComponent`, `RigidBodyComponent`, `ColliderComponent`).
  - Skeletal animation evaluation system (`Skeleton`, `AnimationClip`, `SkeletalEvaluator`) evaluating joint matrices on CPU.
  - Sandboxed Lua 5.4 scripting engine with bounded memory allocations, instruction step limits, and ECS bindings.
  - RmlUi 6.0 runtime UI system with custom RHI render interface and unscaled real-time clock domain.
  - `RuntimeSession` orchestrating simulation lifecycles with an isolated, in-memory `RuntimeWorld` cloned from the authoring `EditWorld`.
  - Native `.hikari` versioned JSON scene serialization with atomic file saving and transactional error recovery.
- **3D Asset Pipeline**:
  - glTF 2.0 (`.gltf`) and Binary glTF (`.glb`) model importer via vendored `cgltf` (v1.14), supporting baked node transforms, inverse-transpose normal generation, and PBR material metadata extraction.
  - Wavefront OBJ (`.obj`) asset importer with backward compatibility.
  - Central `AssetManager` with 64-bit `AssetId` hashing, memory caching, and GPU vertex/index buffer upload deduplication (`GpuMesh`).
- **Developer Visual Editor & IDE** (`HIKARI_BUILD_EDITOR=ON`):
  - Dear ImGui docking workspace with layout persistence (`hikari_editor_layout.ini`).
  - Centralized UI design tokens and theme palettes (Hikari Dark, Midnight, Light) with live runtime switching.
  - Viewport GPU scene rendering: Vulkan 1.3 offscreen dynamic rendering target sampled directly by Dear ImGui (zero CPU readback) with software wireframe fallback toggle.
  - 6-DOF fly camera navigation (WASD + QE + Shift boost) and orbit camera mode with gimbal-lock-free mouse look and pitch clamping.
  - Interactive viewport tools: 3D perspective ground grid, orientation tripod, ray-picking entity selection, 3D transform gizmos (Translate, Rotate, Scale), and asset drag-and-drop spawning.
  - Modular editor panels: Scene Hierarchy (entity creation, inline rename, duplication, deletion, component filters), Entity Inspector (component reflection and multi-selection batch property editing), Content Browser (navigation history, breadcrumbs, templates, safe deletion, native file import), Conductor controls, Performance Metrics, and Log Console.

---

## 3. Technology Stack

| Subsystem | Technology / Library | Actual Status in Codebase |
| :--- | :--- | :--- |
| **Language** | C++20 (`-std=c++20`, `-fno-exceptions`, `-fno-rtti`) | Implemented & enforced globally |
| **Build System** | CMake (>= 3.25) + Ninja | Implemented with multi-preset support |
| **Window & Input** | SDL3 | Integrated |
| **ECS** | Flecs (v3.2.11 vendored C source) | Integrated with system group gating |
| **Physics** | Jolt Physics | Integrated with fixed-timestep accumulator |
| **Graphics API** | Vulkan 1.3 (Dynamic Rendering, Synchronization2) / Null RHI | Integrated |
| **GPU Memory** | Dedicated `VulkanMemoryManager` | Implemented (custom sub-allocation) |
| **Render Graph** | Custom DAG pass scheduler | Implemented |
| **Asset Import** | `cgltf` (v1.14 vendored) for glTF/GLB; custom OBJ loader | Integrated into `AssetManager` |
| **File Dialogs** | `tinyfiledialogs` | Integrated for native asset import picker |
| **Scripting** | Lua 5.4.7 (Sandboxed C API with memory bounds) | Integrated (direct C API used; `sol2` omitted due to `-fno-exceptions`) |
| **Game UI** | RmlUi 6.0 | Integrated with custom RHI render interface |
| **Editor UI** | Dear ImGui 1.91.8 (Docking branch, Vulkan Dynamic Rendering) | Integrated (`HIKARI_BUILD_EDITOR=ON`) |
| **Shaders** | SPIR-V bytecodes & GLSL (`assets/shaders/`) | Precompiled & embedded in backends |
| **Audio** | N/A | Not implemented (`engine/audio` is currently a placeholder) |

---

## 4. Current Limitations & Incomplete Areas

- **Material & Texture Shading**: `AssetManager` parses glTF PBR metallic-roughness factors and texture metadata, but `SceneRenderer` currently renders geometry using untextured directional diffuse lighting and vertex colors. GPU texture sampler descriptor set binding is not yet implemented.
- **Entity Hierarchy & Transform Concatenation**: Flecs entities are currently evaluated with absolute world transforms. Multi-node scene hierarchy propagation (parent-to-child matrix concatenation) and collapsible tree nodes in the Hierarchy panel are not yet implemented.
- **Skeletal Animation GPU Skinning**: Joint matrices and animation curves are evaluated on the CPU via `SkeletalEvaluator`; GPU vertex skinning shaders are not yet connected.
- **Audio**: `engine/audio/` is a directory placeholder; audio mixing and spatial playback are not implemented.
- **Tools & Game Slices**: `tools/` and `game/` are currently directory placeholders; asset cooking utilities and concrete gameplay slices are not yet developed.

---

## 5. Repository Layout

```
/
├── CMakeLists.txt              # Root build configuration & CPack packaging
├── CMakePresets.json           # Presets: debug, release, profile, editor, asan, tsan
├── AGENTS.md                   # Authoritative engineering guidelines and rules
├── engine/                     # Core engine subsystems (never depends on game/editor)
│   ├── core/                   # Allocators, job system, math, logging, handles, Result
│   ├── platform/               # SDL3 windowing, events, input snapshotting
│   ├── rhi/                    # Abstract RHI, Null mock, and Vulkan 1.3 dynamic backend
│   ├── render/                 # RenderGraph, GpuMesh, GltfImporter, ObjImporter, SceneRenderer
│   ├── world/                  # Shared World, Flecs ECS integration, system group gating
│   ├── physics/                # Jolt Physics integration, fixed-step accumulator
│   ├── anim/                   # Skeletal animation, evaluation, cancel windows
│   ├── script/                 # Sandboxed Lua 5.4 scripting engine
│   ├── ui/                     # RmlUi runtime UI system & RHI render interface
│   ├── framework/              # Two-State Conductor, Mode interfaces, SceneSerializer, RuntimeSession
│   └── audio/                  # Audio mixing (placeholder directory)
├── editor/                     # Dear ImGui visual editor & IDE (HIKARI_BUILD_EDITOR)
│   ├── include/editor/         # Editor public interfaces, theme tokens, selection service
│   └── src/                    # EditorEngine, WorkspaceManager, panels (Viewport, Hierarchy, Inspector, etc.)
├── assets/                     # Models (.glb, .obj), scenes (.hikari), and shaders (.vert, .frag)
├── docs/                       # Architecture documentation, ADRs, error catalog
├── src/                        # Standalone application entrypoint (main.cpp)
├── tests/                      # Automated unit, headless, and integration test suites
├── game/                       # Game gameplay slice (placeholder directory)
└── tools/                      # Offline asset cooking utilities (placeholder directory)
```

---

## 6. How to Build and Run

### Prerequisites

- **Compiler**: C++20 compliant compiler:
  - GCC >= 12 (Linux)
  - Clang >= 15 (Linux)
- **Build Tools**: CMake >= 3.25, Ninja.
- **System Libraries (Linux)**:
  - Vulkan SDK and driver with Vulkan 1.3 support.
  - SDL3 development libraries.
  - FreeType development libraries (`libfreetype6-dev`).
  - Platform libraries: `libasound2-dev`, `libpulse-dev`, `libx11-dev`, `libwayland-dev`.

### Build & Test Commands

```bash
# 1. Configure and build the visual editor
cmake --preset editor
cmake --build --preset editor

# 2. Run the test suite
ctest --preset editor --output-on-failure
```

### Available Presets

| Preset | Build Type | Flags / Definitions | Primary Use |
| :--- | :--- | :--- | :--- |
| `editor` | Debug | `-O2 -g -DNDEBUG -DHIKARI_BUILD_EDITOR=ON` | Visual Editor & IDE development |
| `debug` | Debug | `-O0 -g`, assertions enabled | Local development and debugging |
| `release` | Release | `-O3 -DNDEBUG`, optimizations enabled | Performance testing and distribution |
| `profile` | RelWithDebInfo | `-O2 -g -DNDEBUG`, symbols included | Profiling and Tracy analysis |
| `asan` | Debug | `-O1 -g -fsanitize=address,undefined` | Memory safety and leak verification |
| `tsan` | Debug | `-O2 -g -fsanitize=thread` | Concurrency and race verification |

### Running the Executable

```bash
# Launch the visual editor
./build/editor/bin/hikari

# Run headless simulation loop for 60 frames (Null RHI, no window)
./build/release/bin/hikari --headless --frames 60

# Display CLI options and version
./build/release/bin/hikari --help
```

---

## 7. Testing and Validation

All changes are validated across all six build presets. The test suite contains 35 automated test suites:

- **Core Primitives**: `smoke_test`, `result_test`, `assert_test`, `linear_allocator_test`, `arena_allocator_test`, `pool_allocator_test`, `generational_handle_test`, `time_test`, `log_test`, `slot_map_test`, `id_test`, `event_test`, `json_test`, `job_system_test`, `math_test`.
- **Platform & RHI**: `platform_test`, `null_rhi_test`, `vulkan_rhi_test` (validates physical GPU selection, offscreen dynamic rendering, and pipeline compatibility).
- **Simulation & Conductor**: `world_test`, `conductor_test`, `clock_domain_test`, `scene_test`, `runtime_session_test`, `conductor_stress_test` (multi-thousand cycle transitions).
- **Render & Assets**: `render_graph_test`, `mesh_test`, `asset_manager_test`, `gltf_importer_test` (31 tests covering glTF 2.0 / GLB), `scene_renderer_test` (GPU pixel readback and depth occlusion).
- **Subsystems**: `physics_test`, `anim_test`, `script_test`, `ui_test`.
- **Editor Tooling** (`editor` preset): `editor_test` (31 sub-tests covering selection, docking, themes, gizmos, ray picking, camera navigation), `content_browser_test`.

### Validation Baseline
- Real-machine testing is verified on Linux under Wayland/Xwayland on physical NVIDIA hardware (`NVIDIA P106-100`, Driver 550+).
- Headless execution runs in CI on any x86_64 environment using `NullDevice` without GPU dependencies.
