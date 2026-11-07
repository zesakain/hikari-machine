# ADR-0014: Adoption of Lua 5.4 and sol2 for Gameplay Scripting

- **Status**: Accepted
- **Date**: 2026-11-07
- **Authors**: zesakain

## Context

Phase 6 requires a lightweight, safe, and budgeted scripting system for high-level gameplay rules, entity behaviors, and tuning.
Per Section 8 in `AGENTS.md`:
- "Lua expresses gameplay rules only. Anything called every frame for many entities belongs in C++."
- "Lua GC runs incrementally with a per-frame budget."
- "No Lua calls per entity per frame in the hot path."
- "No exceptions and no RTTI in engine code."
- "Only `/engine/rhi` may include graphics API headers. Dependencies flow game -> engine only."
- Lua must never own the Shared World or bypass the Two-State Conductor.

## Decision

Adopt **Lua 5.4** (C core library, MIT License) and **sol2 v3.3.1** (C++ binding library, MIT License) in `engine/script/` (`eng::script`).

### 1. Build & Exception Isolation
- Build Lua 5.4 as an ANSI C static library. In ANSI C, Lua errors use `setjmp`/`longjmp`, avoiding C++ exceptions entirely.
- In `sol2`, enable `SOL_NO_EXCEPTIONS=1` and `SOL_ALL_SAFETIES_ON=1`.
- Public engine headers (`engine/script/include/script/`) contain zero `<lua.h>` or `<sol/sol.hpp>` includes. All Lua state, sol objects, and bindings are encapsulated behind an opaque PIMPL [`ScriptEngine`](engine/script/include/script/script_engine.h).

### 2. Sandbox & Security Policy
- Restrict standard libraries: standard OS, file I/O, debug introspection, and native package loading (`package.loadlib`) are prohibited in the runtime sandbox.
- Allowed libraries: `base` (safe subset: `print`, `type`, `tostring`, `tonumber`, `assert`, `error`), `math`, `table`, `string`, and `coroutine`.
- `print` is redirected directly to Hikari's structured logging system (`eng::log::info("Script", message)`).
- Instruction count and step limits are configured to abort runaway scripts without crashing the engine process.

### 3. Conductor & Shared World Interaction
- Script execution is strictly scheduled through the active mode or controlled gameplay systems.
- Standby mode preparation is prohibited from executing scripts that mutate the Shared World.
- Flecs entity interaction is mediated through safe generational [`EntityHandle`](engine/world/include/world/world.h) wrappers; scripts cannot access raw pointers to Flecs world memory or internal system groups.

## Consequences

### Positive
- High-level, rapid gameplay iteration without recompilation.
- Strict isolation preventing crashes, memory leaks, or sandbox escapes from Lua scripts.
- Clean C++20 API returning `Result<T, Error>` for all script operations.

### Negative
- Requires maintaining binding tables and reflection wrappers between C++ ECS components and Lua tables.
- Garbage collection must be stepped incrementally each frame to avoid frame-time spikes.
