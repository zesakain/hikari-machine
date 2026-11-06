# Editor (`editor`)

## Responsibility
Developer tooling, scene inspectors, performance telemetry displays, and Dear ImGui panels.

## Invariant & Boundary
- Gated exclusively by the `EDITOR` compilation flag.
- Never linked into runtime or headless builds.
- Engine code must never include headers from this directory.
