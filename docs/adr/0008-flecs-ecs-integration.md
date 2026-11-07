# ADR-0008: Adoption of Flecs as Foundation ECS

- **Status**: Accepted
- **Date**: 2026-11-07
- **Authors**: zesakain

## Context

Phase 3 requires a robust, high-performance Entity Component System (ECS) to serve as the foundation of Hikari's single shared `World`. The ECS must satisfy strict engine constraints:
- Archetype / contiguous SoA memory layout for cache-friendly system iteration.
- First-class support for hierarchical system groups, phases, and tags to enable the Conductor's mode-gated system scheduling.
- Strict C++20 compliance with zero exceptions (`-fno-exceptions`) and zero runtime type information (`-fno-rtti`).
- Clean C/C++ boundary with license compatibility (MIT license, compatible with Apache 2.0).

## Decision

Adopt **Flecs v4.0.4** as the engine's core ECS foundation:
1. **Integration Strategy**:
   - Utilize Flecs v4.0.4 single-source amalgamation (`flecs.c` and `flecs.h`) placed within `engine/world/src/flecs/`.
   - Compile `flecs.c` as an isolated static C library (`flecs_static`) without exposing compiler warnings to outer engine targets.
   - Encapsulate Flecs headers inside `engine/world/` so that higher engine layers and future scripting systems interact with clean Hikari interfaces.
2. **Phase & Pipeline Gating**:
   - Leverage Flecs system phases and tags to gate active and standby mode system groups.
   - Mode switches toggle Flecs system phases rather than creating/destroying individual systems or cloning worlds.
3. **Memory and Allocators**:
   - Flecs operates over memory arenas with minimal heap allocation during steady-state iteration.
   - Unrestricted world mutations are strictly forbidden on background worker threads; all structural changes are serialized or deferred.

## Consequences

### Positive
- High-performance, production-proven archetype ECS with rich query syntax.
- Built-in system phase tagging directly aligns with Hikari's two-state Conductor gating requirements.
- Amalgamated distribution guarantees reproducible, hermetic builds across all platforms without external network dependencies during CMake configuration.

### Negative
- Flecs codebase adds ~3 MB of amalgamated source code to `engine/world/src/flecs/`.
- Care must be taken to restrict Flecs world mutations to the main thread or deferred stages to prevent data races.
