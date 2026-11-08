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

## 2. Current Development Status

The engine has completed foundational implementation across Phases 1 through 6:

- **Phase 1: Core Engine Primitives [Implemented & Tested]**
  - Exception-free error handling with `Result<T, Error>`.
  - Custom memory allocators (Linear, Arena, Pool) with thread safety policies.
  - Generational handles (`SlotMap`) preventing use-after-free and stale resource references.
  - Monotonic high-resolution clocks and budgeted microsecond time accumulators.
  - Lock-free work-stealing `JobSystem` supporting job dependencies and batch dispatch.
  - Type-safe synchronous event dispatcher and high-performance ring-buffer logger.
  - POD 3D linear algebra primitives (`Vec3`, `Quat`, `Mat4`, `Transform`).
- **Phase 2: Platform Layer & Null RHI [Implemented & Tested]**
  - SDL3 platform abstraction for windowing, OS event dispatch, and input handling.
  - Deterministic per-frame `InputSnapshot` for keyboard, mouse, and gamepads.
  - Abstract Render Hardware Interface (`RhiDevice`, `RhiCommandList`, buffers, textures, samplers).
  - Headless `NullDevice` and `NullCommandList` enabling 100% offscreen CI simulation testing.
  - Opaque native handle pass-through bridges (`NativeDeviceHandles`, `NativeCommandListHandles`).
- **Phase 3: Shared World & Two-State Conductor [Implemented & Tested]**
  - Unified simulation `World` encapsulating Flecs ECS.
  - System group execution gating (`Continuous`, `StateA`, `StateB`).
  - Two-State `Conductor` runtime coordinating active and standby `Mode` lifecycles (`enter`, `prepare`, `commit`, `exit`).
  - Independent clock domains (`WorldClock`, `ActionClock`, `UiClock`) with transition interpolation.
- **Phase 4: Vulkan RHI Backend & Render Graph [Implemented & Tested]**
  - Vulkan 1.3 concrete backend (`VulkanDevice`, `VulkanCommandList`, `VulkanSwapchain`, `VulkanMemoryManager`).
  - Modern Vulkan 1.3 dynamic rendering (`vkCmdBeginRendering` / `vkCmdEndRendering`) without legacy render passes or framebuffers.
  - Synchronization2 pipeline barriers and automated swapchain layout transitions.
  - DAG-based `RenderGraph` scheduler with topological pass ordering, automatic memory aliasing, and barrier placement.
- **Phase 5: Physics Integration & Animation [Implemented & Tested]**
  - Jolt Physics integration (`PhysicsWorld`) with fixed-timestep simulation accumulator.
  - Bidirectional Flecs ECS transform and velocity synchronization systems (`PhysicsTransformComponent`, `RigidBodyComponent`, `ColliderComponent`).
  - Skeletal animation evaluation system (`Skeleton`, `AnimationClip`, `SkeletalEvaluator`, `AnimationComponent`, `SkinningPaletteComponent`).
  - Montage animation buffering and cancel window structures.
- **Phase 6: Scripting, UI Systems, CI/CD Pipeline & Visual Editor [Implemented & Tested]**
  - Sandboxed Lua 5.4 scripting engine with bounded memory allocations, instruction step limits, and ECS bindings.
  - RmlUi 6.0 runtime UI system with custom backend interfaces and unscaled real-time clock domain.
  - Dear ImGui developer visual editor with Vulkan 1.3 dynamic rendering mesh pipeline.
  - Standalone executable `bin/hikari` with CLI flags (`--headless`, `--frames <N>`, `--version`, `--help`).
  - Automated multi-preset GitHub Actions CI workflows and CPack binary packaging (`.tar.gz`, `.zip`).

---

## 3. Engine Architecture

Hikari-Machine is organized into strict hierarchical layers. Dependencies flow strictly downward; engine modules never depend on game code or editor tooling:

```
┌────────────────────────────────────────────────────────┐
│                   Application / Game                   │
│             src/main.cpp  |  game/ (Modes)             │
├────────────────────────────────────────────────────────┤
│                       Framework                        │
│          Two-State Conductor  |  Mode Registry         │
├──────────────────────────┬─────────────────────────────┤
│      Scripting & UI      │      Editor Tooling         │
│ Lua 5.4 Sandbox | RmlUi  │ Dear ImGui (HIKARI_BUILD_EDITOR)
├──────────────────────────┴─────────────────────────────┤
│                  Simulation Layer                      │
│     Shared World (Flecs) | Jolt Physics | Animation    │
├────────────────────────────────────────────────────────┤
│                   Rendering & RHI                      │
│      Render Graph | Abstract RHI | Vulkan 1.3 Backend  │
├────────────────────────────────────────────────────────┤
│                    Platform Layer                      │
│        SDL3 Windowing | Events | Input Snapshots       │
├────────────────────────────────────────────────────────┤
│                      Core Layer                        │
│   Memory Allocators | Jobs | Math | Handles | Logging  │
└────────────────────────────────────────────────────────┘
```

### Architectural Principles
- **No Graphics API Leakage:** Vulkan headers (`<vulkan/vulkan.h>`) are strictly encapsulated inside private implementation files under `engine/rhi/src/vulkan/`. Zero graphics API types leak into public engine headers.
- **Exceptions and RTTI Disabled:** Engine code is compiled with `-fno-exceptions` and `-fno-rtti`. All operations return `Result<T, Error>`.
- **Zero Heap Allocations in Hot Path:** Simulation, input sampling, render graph recording, and mode commits run without dynamic heap allocations or mutex contention.
- **Strict Editor Isolation:** The editor tooling (`editor/`) is compiled exclusively when `HIKARI_BUILD_EDITOR=ON` (`EDITOR=1`). Non-editor builds contain zero Dear ImGui code or symbols.

---

## 4. Technology Stack

| Subsystem | Technology | Actual Status | Architectural Reference |
| :--- | :--- | :--- | :--- |
| **Language** | C++20 (`-std=c++20`, `-fno-exceptions`, `-fno-rtti`) | Implemented & Enforced | [ADR-0002](docs/adr/0002-cpp20-engine-language.md) |
| **Build System** | CMake (>= 3.25) + Ninja | Implemented | [ADR-0003](docs/adr/0003-cmake-ninja-build-system.md) |
| **Window & Input** | SDL3 | Integrated | [ADR-0007](docs/adr/0007-sdl3-platform-backend.md) |
| **ECS** | Flecs (v3.2.11 vendored C source) | Integrated | [ADR-0008](docs/adr/0008-flecs-ecs-integration.md) |
| **Conductor Runtime** | Two-State Conductor over Shared World | Implemented | [ADR-0009](docs/adr/0009-two-state-conductor-and-shared-world.md) |
| **Graphics API** | Vulkan 1.3 (Dynamic Rendering, Sync2) / Null RHI | Integrated | [ADR-0010](docs/adr/0010-vulkan-rhi-backend.md) |
| **Render Graph** | Custom DAG pass scheduler | Implemented | [ADR-0011](docs/adr/0011-render-graph-architecture.md) |
| **Physics** | Jolt Physics | Integrated | [ADR-0012](docs/adr/0012-jolt-physics-integration.md) |
| **Animation** | Custom Skeletal Evaluator & cancel windows | Implemented | [ADR-0013](docs/adr/0013-animation-system-architecture.md) |
| **Scripting** | Lua 5.4.7 (Sandboxed C API with memory bounds) | Integrated | [ADR-0014](docs/adr/0014-lua-scripting-and-sol2-integration.md) |
| **sol2** | C++ Lua binding library | Not Integrated | Direct C API used in implementation |
| **Runtime UI** | RmlUi 6.0 | Integrated | [ADR-0015](docs/adr/0015-runtime-and-editor-ui-architecture.md) |
| **Editor UI** | Dear ImGui 1.91.8 (Vulkan Dynamic Pipeline) | Integrated | [ADR-0017](docs/adr/0017-functional-visual-editor-and-ide-architecture.md) |
| **GPU Memory** | Dedicated `VulkanMemoryManager` | Implemented | [ADR-0010](docs/adr/0010-vulkan-rhi-backend.md) |
| **VMA** | Vulkan Memory Allocator | Not Integrated | Engine uses custom dedicated manager |
| **Shaders** | SPIR-V pre-compiled bytecodes | Implemented | Shaders embedded directly in backends |
| **Slang** | Slang shader compiler | Planned | Proposed for future offline pipeline |
| **Audio** | Custom mixing / voice management | Planned | `engine/audio` contains architecture outline |

---

## 5. Visual Editor & IDE Development Status

The engine includes a functional developer visual editor (`editor/`) built with Dear ImGui (docking branch) and rendered through Vulkan 1.3 dynamic rendering:

### Implemented & Verified Capabilities
- **Vulkan Dynamic Pipeline:** Real vertex (`ImDrawVert`) and index (`ImDrawIdx`) uploads drawn directly into the swapchain dynamic render pass with alpha blending and font texture descriptor sets.
- **Docking Workspace Architecture:** Full multi-panel docking system (`ImGuiDockNodeFlags_DockSpace`), customizable workspace layout presets (Default Studio, Wide Viewport, Inspector Focus), and layout persistence to disk (`hikari_editor_layout.ini`).
- **Main Menu Bar:** Menus for File, Edit, View, GameObject, Component, Window (panel toggles & layouts), Tools, and Help.
- **Main Toolbar:** Simulation controls (Play, Pause, Step), live Conductor State A/B dynamic badges, one-click state transition triggers, and real-time FPS readout.
- **Scene Hierarchy & Entity Management (Milestone 2):**
  - **Centralized Selection Service (`SelectionModel`):** Single-click selection, additive Ctrl+Click multi-selection, contiguous Shift+Click range selection, Ctrl+A Select All, and Escape Deselect.
  - **Generational Validation:** Automatically purges destroyed or stale entity handles with safe primary selection fallback.
  - **Entity Creation & Listing:** Creates Empty Entities, Entities with Transform, 3D Physics Cubes, and 3D Physics Spheres with collision-free unique name generation.
  - **Inline Rename:** Double-click or F2 inline renaming with validation and whitespace trimming.
  - **Safe Entity Duplication:** Ctrl+D duplication with component cloning and spatial offset.
  - **Entity Deletion:** Single and multi-entity deletion with immediate selection cache invalidation.
  - **Search & Filtering:** Real-time case-insensitive search and component filter chips (`[All]`, `[Tr]`, `[RB]`, `[Script]`).
- **Entity Inspector (Milestone 3):** Full-featured ECS component and property editor:
  - **Component Management:** Categorized `+ Add Component` menu (Physics, Scripting) with duplicate prevention, and component removal via card headers and right-click context menus (`Remove Component`, `Reset to Default`).
  - **Entity Header:** In-place entity renaming with automatic ECS synchronization on Enter or defocus, entity ID telemetry, and color-coded component archetype tags (`[Transform]`, `[RigidBody]`, `[Collider]`, `[Script]`).
  - **PhysicsTransformComponent:** 3D position (`DragFloat3`), Euler angles rotation (`DragFloat3` in degrees converted to/from unit quaternions), and scale with degenerate dimension validation.
  - **RigidBodyComponent:** Kinematic motion type combo (`Static`, `Kinematic`, `Dynamic`) and collision layer combo (Static, Moving, Player, Sensor).
  - **ColliderComponent:** Geometric primitive shapes (`Box`, `Sphere`, `Capsule`), shape-specific dimensions (half-extents, radius, half-height), and center offsets.
  - **ScriptComponent:** Editable Lua script routine name, enabled/disabled execution toggle, update interval, and real-time status indicators (`[Active]`, `[Unassigned]`, `[Disabled]`).
  - **Multi-Selection Editing:** Detection of common components across all selected entities, per-axis mixed-state indicators (`---`), batch property modifications applied to all selected entities without affecting unselected entities, batch Add Component to all, and batch Remove Component from all.
- **Native Scene Serialization & Persistence (Milestone 4):**
  - **Native `.hikari` File Format:** Versioned JSON format containing scene metadata, stable entity identifiers (`StableIdComponent`), and attached ECS component data.
  - **Supported Components:** Full serialization and deserialization for `PhysicsTransformComponent` (position, quaternion rotation, scale), `RigidBodyComponent` (motion type, collision layer), `ColliderComponent` (box, sphere, capsule dimensions and offsets), and `ScriptComponent` (routine name, enabled flag, update interval).
  - **Transactional Deserialization:** Corrupted or invalid scene files (syntax errors, unsupported schema versions, duplicate entity IDs, degenerate scale) are rejected in memory before mutating the active simulation world.
  - **Atomic File Saving:** Two-step save process writing to a temporary file (`.tmp`) before atomic filesystem renaming, preventing partial file writes or corruption on crash.
  - **Editor File Workflows:** File menu actions (`New Scene`, `Open Scene...`, `Save Scene`, `Save Scene As...`), modal confirmation dialogs for unsaved changes, path prompt dialogs, error alerts, and keyboard shortcuts (`Ctrl+N`, `Ctrl+O`, `Ctrl+S`, `Ctrl+Shift+S`).
  - **Dirty State Tracking (`*`):** Real-time dirty flag tracking on entity creation, renaming, component mutations, and deletion with visual indicators on the toolbar and status bar (`Scene: Name*`).
  - **Selection Safety:** Selection cache invalidation and clearing upon loading or creating scenes to prevent stale entity handle references.
  - **Sample Scene:** Verified reference scene available at `assets/scenes/sample.hikari`.
- **Runtime Session & Simulation Lifecycle (Milestone 5):**
  - **Explicit Simulation State Machine:** Governed by four distinct states (`Edit`, `Playing`, `Paused`, `Stopped`) with strict valid transition gating.
  - **Absolute World Isolation:** Simulations execute in an isolated, in-memory `RuntimeWorld` cloned from the authoring `EditWorld` via `SceneSerializer`. Physical body movements, gravity drops, script updates, and runtime-spawned entities operate exclusively in the runtime environment and never alter or leak into the authoring scene.
  - **Pristine Edit World on Stop:** Triggering Stop safely destroys the `RuntimeWorld`, Jolt rigid bodies, and runtime script states, immediately returning the editor to Edit Mode with the `EditWorld` 100% unaltered.
  - **Transport Toolbar & Shortcuts:** State-gated Play, Pause, Resume, Step, and Stop controls on the main toolbar, live mode badges (`[EDIT MODE]`, `[PLAYING]`, `[PAUSED]`), and desktop shortcuts (`F5` / `Ctrl+P` Play/Pause/Resume, `Shift+F5` Stop, `F10` Step).
  - **Active Context Synchronization:** Scene Hierarchy and Inspector dynamically bind to the active simulation world, displaying `[LIVE RUNTIME WORLD]` and `[RUNTIME SIMULATION ENTITY]` telemetry badges during simulation while safeguarding against accidental dirtying of scene files.
- **Interactive 3D Scene Viewport & Camera System (Milestone 6):**
  - **Independent Editor Camera:** Decoupled 3D editor camera (`EditorCamera`) with perspective and orthographic projections, vertical FOV, near/far clipping planes, and aspect-ratio-aware matrix updates.
  - **Studio Camera Navigation:** Orbit (Alt + LMB or MMB), Pan (Shift + MMB or Shift + RMB), Zoom (mouse wheel or Alt + RMB), and First-Person Fly mode (hold RMB + W/S/A/D/Q/E + Shift boost). Gimbal-lock-free orientation with pitch clamping ($[-89^\circ, +89^\circ]$).
  - **Perspective 3D Ground Grid & Orientation Tripod:** Perspective-aware infinite ground grid on the $Y=0$ plane with distance attenuation, major grid lines at 5m, Red/Green/Blue XYZ ground axes, origin indicator, and an interactive 3D orientation tripod in the top-right corner.
  - **3D Scene Geometry Rendering:** Direct perspective 3D rendering of active World entities with `PhysicsTransformComponent`. 3D shaded and wireframe box/cube representations with directional key light diffuse shading, 3D wireframe/shaded spheres with equatorial/meridian ellipses, and vivid cyan selection cages with overhead entity tags.
  - **Viewport Ray-Picking Selection:** Canvas mouse clicks unproject into 3D world-space rays via the inverse view-projection matrix, testing depth-sorted intersections against entity bounding boxes (OBB/AABB) and spheres. Directly synchronizes with the centralized `SelectionModel`, updating Scene Hierarchy and Inspector immediately.
  - **Interactive 3D Transform Gizmos:** Translate, Rotate, and Scale gizmo handles rendered directly on the primary selected entity. Features hover detection (golden highlight), axis-constrained mouse dragging, snapping (0.1m, 0.5m, 1.0m; 15°; 0.25), and World vs. Local coordinate modes.
  - **Runtime World Preview Integration:** Previews the active simulation world seamlessly (`EditWorld` in Edit mode, `RuntimeWorld` during Playing/Paused simulation). Viewport selections interact safely with active world entities, and selection handles are invalidated on state transitions.
- **Content Browser:** Virtual project asset filesystem navigation tree (`assets/`, `data/`, `game/`, `docs/`).
- **Log Console Panel:** Streams live ring-buffer records from `Logger::defaultLogger()` with severity filtering (All, Info, Warning, Error), search, and clear controls.
- **Profiler & Telemetry:** FPS, frame times, draw calls, uploaded vertex/index counts, pass counters, and live Flecs entity count.
- **Diagnostic Window ("HIKARI EDITOR"):** Displays physical GPU device name, Vulkan dynamic rendering status, font atlas status, and operational health.

### Current Limitations & Incomplete Features
- **Asset Mesh Pipeline:** Mesh geometry from external asset files (glTF, OBJ) and PBR materials are planned for upcoming graphics milestones. The viewport currently renders analytical 3D primitives (cubes, spheres, bounding cages, and collision volumes).

---

## 6. How to Build and Run

### Prerequisites

- **Compiler:** C++20 compliant compiler:
  - GCC >= 12 (Linux)
  - Clang >= 15 (Linux)
- **Build Tools:** CMake >= 3.25, Ninja.
- **System Libraries (Linux):**
  - Vulkan SDK and driver (Vulkan 1.3+ support).
  - SDL3 development libraries.
  - FreeType development libraries (`libfreetype6-dev`).
  - Platform libraries: `libasound2-dev`, `libpulse-dev`, `libx11-dev`, `libwayland-dev`.

### Build Commands

```bash
# 1. Configure using a preset (debug, release, profile, editor, asan, tsan)
cmake --preset editor

# 2. Compile the target
cmake --build --preset editor

# 3. Execute the automated test suite
ctest --preset editor --output-on-failure
```

### Available Presets

| Preset | Build Type | Flags / Features | Primary Use |
| :--- | :--- | :--- | :--- |
| `debug` | Debug | `-O0 -g`, assertions enabled | Local development and debugging |
| `release` | Release | `-O3 -DNDEBUG`, optimized | Performance testing and distribution |
| `profile` | RelWithDebInfo | `-O2 -g -DNDEBUG`, symbols included | Profiling and Tracy analysis |
| `editor` | RelWithDebInfo | `-O2 -g -DNDEBUG`, `HIKARI_BUILD_EDITOR=ON` | Visual Editor & IDE development |
| `asan` | Debug | `-O1 -g -fsanitize=address,undefined` | Memory safety and leak verification |
| `tsan` | RelWithDebInfo | `-O2 -g -fsanitize=thread` | Concurrency and race verification |

### Running the Application

```bash
# Run the visual editor build (GUI window with Dear ImGui IDE)
./build/editor/bin/hikari

# Run headless simulation loop for 60 frames (no window, Null RHI)
./build/release/bin/hikari --headless --frames 60

# Display CLI help and version
./build/release/bin/hikari --help
```

---

## 7. Testing and Validation

Every change to the repository is validated across all 6 build presets. The test suite contains 27 automated test suites under `tests/unit/`:

- **Core Primitives:** `smoke_test`, `result_test`, `assert_test`, `linear_allocator_test`, `arena_allocator_test`, `pool_allocator_test`, `generational_handle_test`, `time_test`, `log_test`, `slot_map_test`, `id_test`, `event_test`, `job_system_test`, `math_test`.
- **Platform & RHI:** `platform_test`, `null_rhi_test`, `vulkan_rhi_test` (verifies physical GPU device selection, swapchain presentation, and dynamic clears).
- **Simulation & Conductor:** `world_test`, `conductor_test`, `clock_domain_test`, `conductor_stress_test` (multi-thousand cycle transitions).
- **Subsystems:** `render_graph_test`, `physics_test`, `anim_test`, `script_test`, `ui_test`, `editor_test` (built and tested in the `editor` preset).

### Hardware Validation Baseline
- Real-machine validation is verified on Linux under Wayland/Xwayland on physical NVIDIA hardware (`NVIDIA P106-100`, Driver 550+).
- Offscreen headless execution runs on any x86_64 Linux environment via the `NullDevice` without requiring a physical GPU.

---

## 8. Packaging and CI/CD

- **Automated CI Workflow (`.github/workflows/ci.yml`):**
  - Triggered on push and pull request to `main`.
  - Runs authoritative `clang-format` style check.
  - Compiles and runs test suites across all 6 presets in parallel: `debug`, `release`, `profile`, `editor`, `asan`, and `tsan`.
- **Release Packaging Workflow (`.github/workflows/release.yml`):**
  - Triggered on version tags (`v*`).
  - Packages release binaries, headers, and asset directories into `.tar.gz` and `.zip` bundles using CPack.
  - Automatically generates and publishes SHA256 checksums alongside release archives.

---

## 9. Repository Layout

```
/
├── CMakeLists.txt              # Root build configuration & CPack packaging
├── CMakePresets.json           # Presets: debug, release, profile, editor, asan, tsan
├── .clang-format               # Authoritative formatting rules
├── .clang-tidy                 # Static analysis rules
├── .github/workflows/          # CI and Release GitHub Actions workflows
├── engine/                     # Core engine subsystems (never depends on game/editor)
│   ├── core/                   # Memory allocators, job system, math, logging, handles
│   ├── platform/               # SDL3 windowing, events, input snapshotting
│   ├── rhi/                    # Abstract RHI, Null mock, and Vulkan 1.3 backend
│   ├── render/                 # DAG Render Graph scheduler
│   ├── world/                  # Shared World, Flecs ECS integration, system gating
│   ├── physics/                # Jolt Physics integration, fixed-step accumulator
│   ├── anim/                   # Skeletal animation, evaluation, cancel windows
│   ├── script/                 # Sandboxed Lua 5.4 scripting engine
│   ├── ui/                     # RmlUi runtime UI system & render interface
│   ├── framework/              # Two-State Conductor, Mode interfaces, clock domains
│   └── audio/                  # Audio mixing (architecture outline)
├── editor/                     # Dear ImGui visual editor & IDE (HIKARI_BUILD_EDITOR)
├── game/                       # Game modes and gameplay logic (engine consumer)
├── tools/                      # Offline data compilers and asset cooking utilities
├── data/                       # Schemas and human-readable content (TOML / JSON)
├── assets/                     # glTF models, textures, audio assets
├── docs/                       # Architecture documentation, ADRs, error catalog
├── src/                        # Standalone application entrypoint (main.cpp)
└── tests/                      # Unit and integration test suites
```

---

## 10. Development Roadmap

1. **Scene Viewport Rendering:** Implement dedicated offscreen color and depth render targets to rasterize 3D scene geometry into the editor viewport panel.
2. **Editor Docking & Windowing:** Upgrade Dear ImGui to the docking branch to support multi-window drag-and-drop docking and customizable workspace layouts.
3. **Asset Pipeline & Cooker (`tools/`):** Implement offline data compilation and asset cooking for glTF meshes, KTX2 textures, and TOML data tables.
4. **Scene Persistence:** Implement serialization of Flecs ECS entity archetypes to disk.
5. **Audio Subsystem:** Implement the audio mixing and playback backend in `engine/audio/`.
6. **Action Game Testbed:** Develop a dedicated action-combat validation slice in `game/` demonstrating the Two-State Conductor under real combat gameplay conditions.
