# Engine Architecture Overview

## Mission & Scope

Hikari-Machine is a 3D game engine designed with a mode-based execution model. The initial focus is on fast-paced action titles, with architecture designed to scale to other genres without embedding game-specific assumptions into the engine runtime.

## Core Architectural Pillars

1. **Two-State Conductor**: The engine's runtime is managed by a Conductor that operates exactly two time states: State A and State B. At any moment, one state is active and executing simulation logic, while the other is in standby, incrementally preparing the assets and state for the subsequent transition.
2. **Single Shared World**: Both states operate over a unified simulation world. Entities, physics state, and visual effects persist across mode boundaries. Simulation state is not duplicated.
3. **Hardware Neutrality**: System behavior and rendering paths depend on explicit capability flags (`GpuCaps`), not vendor strings or device IDs. Baseline execution targets Tier 0 specifications.
4. **Layer Isolation**: The dependency hierarchy flows strictly downward: Framework -> Simulation / Rendering -> Platform / Core. The engine never includes game or editor headers, and graphics APIs are confined to the Render Hardware Interface (RHI).
5. **Zero Allocation in the Hot Path**: Frame-critical loops (input polling, fixed simulation, command recording, presentation) forbid dynamic heap allocations, mutex contention, blocking I/O, and unbounded logging.

## Priority Model

When evaluating trade-offs, engineering decisions follow this strict priority order:

1. **Correctness**: Undefined behavior, memory corruption, and stale cache reads are unacceptable.
2. **Frame-time consistency**: Minimize tail latency (p99 frametime spikes) over peak theoretical throughput.
3. **Input latency**: Sample input at frame start and reflect it in the current frame's output.
4. **Simplicity**: Favor data-oriented POD structs and composition over deep hierarchies and speculative abstractions.
5. **Features**: Features are added only after the architectural foundations are proven and measured.
