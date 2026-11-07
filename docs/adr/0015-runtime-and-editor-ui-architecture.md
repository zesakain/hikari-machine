# ADR-0015: Runtime UI (RmlUi) and Editor UI (Dear ImGui) Architecture

- **Status**: Accepted
- **Date**: 2026-11-07
- **Authors**: zesakain

## Context

Phase 6 requires two distinct UI subsystems:
1. **Runtime Game UI**: In-game HUD, menus, floating damage numbers, and modal dialogs. Must support document styling, responsive layouts, independent clock scaling (`UiClock`), and integration with Hikari's RHI and Render Graph.
2. **Editor UI**: Developer inspection tools, profiler views, inspector panels, and debugging overlays. Built exclusively when `EDITOR` (or `HIKARI_BUILD_EDITOR=ON`) is enabled. Non-editor runtime builds must contain zero editor code or ImGui symbols.

## Decision

1. **Runtime Game UI — RmlUi**:
   - Integrate **RmlUi v6.0+** (MIT License) in `engine/ui/` (`eng::ui`).
   - Pure C++20 HTML/CSS layout model.
   - Encapsulate RmlUi headers strictly inside private implementation units (`engine/ui/src/`).
   - Implement custom `RenderInterface` emitting Render Graph / RHI command lists. Headless execution on `NullDevice` skips graphics commands without allocating GPU resources.
   - Implement `SystemInterface` routing elapsed time from `UiClock` (ensuring UI animations remain smooth during slow-motion `ActionClock` gameplay transitions) and redirecting logging to `eng::log`.
   - Input: translate `eng::platform::InputSnapshot` into RmlUi mouse/keyboard events.

2. **Editor UI — Dear ImGui**:
   - Integrate **Dear ImGui v1.91+** (MIT License) under `editor/`.
   - Guarded strictly by the `EDITOR` compile definition.
   - Integrates with SDL3 for event routing and custom RHI Vulkan/Null rendering backend.
   - Zero ImGui code is included in release, non-editor builds.

## Consequences

### Positive
- Crisp separation between in-game visual presentation (CSS/RML) and developer tooling (ImGui).
- Complete isolation from ActionClock slow-motion effects.
- Clean RHI rendering integration adhering to Hard Rule 3.

### Negative
- Two UI libraries to pin and maintain in the build system.
