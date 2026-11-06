# ADR-0004: Layered Engine Architecture and Boundary Isolation

- **Status**: Accepted
- **Date**: 2026-11-06
- **Authors**: zesakain

## Context

Game engine codebases often suffer from cyclical dependencies, leaking gameplay-specific concepts into low-level systems, or coupling simulation logic directly to graphics APIs. This prevents testing simulation headless and makes code refactoring treacherous.

## Decision

Enforce a strictly layered module architecture with unidirectional downward dependencies:

1. **Hierarchy**:
   - `Framework` (Conductor, Mode, Clocks) -> depends on Simulation, Rendering, Platform, Core.
   - `Simulation` (World, Physics, Anim, Audio) & `Rendering` (Render Graph, Material, RHI) -> depend on Platform, Core.
   - `Platform` (OS, Window, Input) -> depends on Core.
   - `Core` (Allocators, Handles, Math, Jobs, Logging) -> bottom layer with no engine dependencies.
2. **Isolation Rules**:
   - `/engine` must never include anything from `/game` or `/editor`.
   - `/engine` contains no game-specific vocabulary.
   - Only `/engine/rhi` may include graphics API headers (Vulkan, etc.).
   - Editor code is compiled only under `EDITOR` and kept entirely out of non-editor builds.
   - Public headers live exclusively in `engine/<module>/include/<module>/`.

## Consequences

### Positive
- Headless simulation tests can run without initializing graphics or window systems.
- Subsystems can be tested in isolation with mocked dependencies or null backends.
- Architecture resists degradation and cyclical coupling.

### Negative
- Requires discipline in placing APIs and prevents quick ad-hoc shortcuts between modules.
