# ADR-0012: Adoption and Integration of Jolt Physics

- **Status**: Accepted
- **Date**: 2026-11-07
- **Authors**: zesakain

## Context

Phase 5 requires a fast, robust, and deterministic 3D rigid-body physics engine. The subsystem must support:
- Rigid body simulation (static, kinematic, dynamic) with boxes, spheres, and capsules.
- Collision layer filtering, raycasting, and shape queries.
- Smooth integration with Hikari's single shared `World` and Two-State `Conductor` architecture.
- Strict compliance with engine constraints: C++20, `-fno-exceptions`, `-fno-rtti`, custom allocator support, and clean abstraction boundaries (zero third-party header leaks into consumer modules).
- Permissive license compatible with Apache 2.0.

## Decision

Adopt **Jolt Physics v5.6.0** (MIT License) as the core physics simulation engine for Hikari.

### 1. Build and Dependency Strategy
- Integrate Jolt via CMake `FetchContent` pinned to git tag `v5.6.0` with `GIT_SHALLOW TRUE`.
- Configure Jolt build options to match Hikari's architectural policies:
  - `CPP_EXCEPTIONS_ENABLED = OFF` and `CPP_RTTI_ENABLED = OFF`.
  - `OVERRIDE_CXX_FLAGS = OFF` (preserves engine compiler warning and optimization flags).
  - `INTERPROCEDURAL_OPTIMIZATION = OFF`.
  - `JPH_USE_DX12 = OFF`, `JPH_USE_VK = OFF`, `JPH_USE_MTL = OFF`, `JPH_USE_CPU_COMPUTE = ON` (avoids requiring external shader compiler tools such as `dxc`).
  - Unit tests, samples, and viewer targets disabled.

### 2. Module Boundaries & API Isolation
- Jolt headers (`<Jolt/...>`) are strictly isolated within `engine/physics/src/`. No Jolt types appear in public headers (`engine/physics/include/physics/`).
- Engine callers interact with physics bodies via `BodyHandle`, a generational handle backed by `GenerationalHandle`.
- Math conversions between Hikari's `eng::Vec3`/`eng::Quat` and Jolt's `JPH::Vec3`/`JPH::Quat` occur exclusively in private translation units.

### 3. Simulation Lifecycle & Two-State Conductor Interaction
- Continuous simulation: Physics updates run under `SystemGroup::Continuous`, advancing authoritative physical state uniformly.
- Standby isolation: Standby mode preparation (`Mode::prepare`) in the Two-State Conductor is strictly prohibited from advancing live physics simulation. Standby queries are read-only.
- Fixed-timestep accumulator: Physics updates occur at fixed discrete intervals (default 60 Hz = 0.01667 s) with a maximum substep cap (default 4) to prevent simulation spiral-of-death under CPU load spikes. Excess frame time remains in the accumulator.

### 4. ECS Synchronization & Resource Reclamation
- Transform synchronization: Dynamic rigid bodies pull simulated transforms into Flecs `TransformComponent` after physics stepping; kinematic/static bodies push ECS transforms to Jolt before stepping.
- Entity destruction: When an entity with a physics body is deleted from the Flecs `World`, its associated Jolt body is destroyed, preventing stale handle reuse.

## Consequences

### Positive
- Production-grade multi-core physics simulation with high performance and stability.
- Zero graphics/shader compiler dependencies required to build physics.
- Clean architectural encapsulation ensuring physics can be replaced or mocked headlessly.

### Negative
- Initial FetchContent download of Jolt (~16 MB git repository) during initial CMake configuration.
- Bridging data structures between Jolt and Flecs requires disciplined transform synchronization systems.
