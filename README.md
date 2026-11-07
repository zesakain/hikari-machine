# Hikari-Machine

A 3D game engine targeting fast-paced action games, structured around a two-state Conductor architecture.

Repository: [https://github.com/zesakain/hikari-machine](https://github.com/zesakain/hikari-machine)  
License: [Apache 2.0](LICENSE)

## 1. What the Project Is

Hikari-Machine is an experimental general-purpose 3D game engine designed with a mode-based execution model. The core architectural feature is a two-state Conductor that alternates between an active state and a standby state over a single shared simulation world. The active state runs gameplay logic while the standby state incrementally prepares the next state's resources and state transitions.

## 2. Current Development Status

Current Phase: **Phase 5 — Physics Integration (Jolt) & Animation System**.

- **Implemented**:
  - Directory structure and module boundaries.
  - CMake build system with preset definitions (`debug`, `release`, `profile`, `editor`, `asan`, `tsan`).
  - Strict compiler warning policies (`-Wall -Wextra -Wconversion -Wshadow`) and C++20 enforcement.
  - Engine target policies: exceptions and RTTI disabled (`-fno-exceptions`, `-fno-rtti`).
  - Formatting configuration (`.clang-format`) and static analysis configuration (`.clang-tidy`).
  - Architecture documentation, Architecture Decision Records (ADRs 0001–0013), and error reference catalog.
  - Phase 1: Core engine primitives (Result, allocators, generational handles, clock, log, slot map, strong ID, events, job system, math).
  - Phase 2: Platform layer (SDL3) and Null RHI.
  - Phase 3: Shared World (Flecs ECS) and Two-State Conductor execution runtime.
  - Phase 4: Vulkan 1.3+ RHI backend, dynamic rendering, and DAG Render Graph.
  - Phase 5: Jolt Physics integration, fixed-timestep accumulator, and skeletal animation system.
- **Planned (Not Implemented)**:
  - Phase 6: Scripting (Lua) and UI systems.
- **Experimental**:
  - None at this stage.

## 3. Main Technologies

The planned technology stack consists of:

| Subsystem | Planned Technology | Selection Status |
| :--- | :--- | :--- |
| Language | C++20 | Accepted ([ADR-0002](docs/adr/0002-cpp20-engine-language.md)) |
| Build System | CMake (>= 3.25) + Ninja | Accepted ([ADR-0003](docs/adr/0003-cmake-ninja-build-system.md)) |
| ECS | flecs | Proposed (pending ADR and approval) |
| Physics | Jolt Physics | Proposed (pending ADR and approval) |
| Window / Input | SDL3 | Proposed (pending ADR and approval) |
| Graphics API | Vulkan 1.3 / Custom RHI | Proposed (pending ADR and approval) |
| Shaders | Slang / SPIR-V | Proposed (pending ADR and approval) |
| Scripting | Lua 5.4 + sol2 | Proposed (pending ADR and approval) |
| Runtime UI | RmlUi | Proposed (pending ADR and approval) |
| Editor UI | Dear ImGui | Proposed (pending ADR and approval) |

Per repository rules, third-party libraries require an approved ADR before introduction. Phase 0 contains zero third-party dependencies.

## 4. How to Build and Test

### Prerequisites

- C++20 compliant compiler: GCC (>= 12), Clang (>= 15), or MSVC (>= 19.34 / Visual Studio 2022).
- CMake >= 3.25.
- Ninja build tool.

### Build Commands

```bash
# Configure using a preset (e.g. debug, release, asan, tsan)
cmake --preset debug

# Build the configured preset
cmake --build --preset debug

# Run test suite
ctest --preset debug --output-on-failure
```

### Available Presets

- `debug`: Debug build (`-O0 -g`) with full assertions.
- `release`: Release build (`-O3 -DNDEBUG`) with optimizations enabled.
- `profile`: Release build with debug symbols (`-O2 -g -DNDEBUG`).
- `editor`: Build configuration enabling the `EDITOR` target flag.
- `asan`: AddressSanitizer and UndefinedBehaviorSanitizer enabled.
- `tsan`: ThreadSanitizer enabled.

## 5. Repository Structure

```
/
├── CMakeLists.txt              # Root build configuration
├── CMakePresets.json           # Standard build configurations
├── CMakeUserPresets.json.example # Local developer preset template
├── .clang-format               # Authoritative code formatting configuration
├── .clang-tidy                 # Static analysis rules
├── engine/                     # Engine source tree (never depends on game or editor)
│   ├── core/                   # Memory, math, jobs, handles, assertions, logging
│   ├── platform/               # OS abstraction, window, input dispatch
│   ├── rhi/                    # Render Hardware Interface (only module accessing gfx APIs)
│   │   ├── include/            # Abstract RHI interfaces
│   │   ├── null/               # Headless test backend
│   │   └── vulkan/             # Vulkan 1.3 backend
│   ├── render/                 # Render graph, materials, scene rendering
│   ├── world/                  # Simulation world, ECS storage
│   ├── physics/                # Rigid body and collision interfaces
│   ├── anim/                   # Skeletal animation, montages, cancel windows
│   ├── audio/                  # Audio mixing and voice management
│   └── framework/              # Conductor, Mode interfaces, system gating
├── editor/                     # Editor tools and ImGui panels (isolated by EDITOR flag)
├── game/                       # Game-specific modes, systems, and content
├── tools/                      # Offline data compilers and cooking utilities
├── data/                       # Schemas and human-readable content (TOML / JSON)
├── assets/                     # glTF models, textures, audio assets
├── docs/                       # Architecture specs, ADRs, errors, GPU notes
└── tests/                      # Unit, headless, golden, and benchmark tests
```

Public engine headers reside under `engine/<module>/include/<module>/`.

## 6. Documentation Links

- [Architecture Overview](docs/architecture/overview.md)
- [Layer Boundaries and Dependencies](docs/architecture/layers.md)
- [Two-State Conductor Design](docs/architecture/conductor.md)
- [Memory and Hot Path Rules](docs/architecture/memory_and_hot_path.md)
- [GPU Capabilities and Tier 0 Hardware Notes](docs/gpu/README.md)
- [Error Reference Catalog](docs/errors.md)
- [Architecture Decision Records (ADRs)](docs/adr/0001-record-architecture-decisions.md)
