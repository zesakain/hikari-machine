# Phase 3 Implementation Plan: Shared World & Two-State Conductor Execution Runtime

This document defines the architectural specification, module boundaries, ownership model, execution runtime, state transition semantics, and verification strategy for **Phase 3: Shared World and Two-State Conductor Execution Runtime** in the Hikari Engine.

---

## 1. Architectural Mission & Identity

The central architectural identity of Hikari is the **Two-State Conductor** operating over a **Single Shared World**.
- Hikari is **not** structured around a generic scene manager or arbitrary hierarchical state machine.
- Execution is orchestrated by the Conductor owning exactly two operational states: **State A** and **State B**.
- At any point in time, one state is **Active** (executing gameplay simulation, processing input, driving the camera) and the other is on **Standby** (incrementally preparing the world-facing setup for the subsequent transition under a strict per-frame budget).
- Both states share **one World**. Entities, physical bodies, visual effects, and audio voices persist across state transitions and are never cloned or duplicated.
- Transition slow-motion is applied exclusively to the transition curve entering a state; once entry completes, time runs at full speed. Slow-motion is never a process pause or simulation freeze.

---

## 2. Module Boundaries & Layer Hierarchy

In strict accordance with [ADR-0004](docs/adr/0004-layered-engine-architecture.md), dependencies flow strictly downward:

```
Framework (Conductor, Mode, Clocks)
   │
   ▼
Simulation (World, Flecs ECS) & Rendering (Null RHI)
   │
   ▼
Platform (Window, InputSnapshot)
   │
   ▼
Core (Allocators, Handles, Time, Budget, Logging, Events, Jobs)
```

### A. Simulation Layer: `engine/world/`
- **Location**:
  - Public headers: `engine/world/include/world/`
  - Implementation: `engine/world/src/`
- **Responsibilities**:
  - Encapsulate Flecs (`flecs::world`) behind engine abstractions.
  - Manage entity lifecycle, component registration, and system queries.
  - Define and manage **System Group Phases** (Flecs tags) to gate systems on/off based on the active mode without recreating systems or mutating world topology.
  - Provide safe generational entity handle abstraction (`EntityHandle`).
  - Guarantee deterministic iteration and safe deferred mutation rules during query execution.
- **Forbidden**:
  - `engine/world` must never include anything from `engine/framework`, `game`, or `editor`.
  - Must not hold state-specific preparation caches.

### B. Framework Layer: `engine/framework/`
- **Location**:
  - Public headers: `engine/framework/include/framework/`
  - Implementation: `engine/framework/src/`
- **Responsibilities**:
  - `Mode`: Pure abstract interface representing an operational mode.
  - `Conductor`: The authoritative runtime orchestrator coordinating State A and State B.
  - Decoupled time domains: `WorldClock`, `ActionClock`, and `UiClock`.
  - Standby preparation budgeting, commit validation, atomic role reversal, transition slow-motion curves, and rollback recovery.
- **Forbidden**:
  - No game-specific vocabulary (e.g. character names, skills, combos).
  - No multi-state generalization (N-state state machine); two states A and B remain the first-class base architecture.

---

## 3. Ownership and Lifetime Model

### A. World Ownership
- The `World` is owned by the engine application runtime and outlives all modes.
- Components and entities reside directly within Flecs tables.
- Entities are referenced externally via `EntityHandle` (wrapping 64-bit `GenerationalHandle` or Flecs `ecs_entity_t` with generation verification).

### B. Mode Ownership
- Modes are instantiated at startup and owned by the `Conductor` (or game application shell) via `std::unique_ptr<Mode>`.
- The Conductor holds non-owning references or owning pointers to State A and State B.

### C. Preparation Data Lifetime
- Data computed during standby preparation is a **speculative cache** and belongs strictly to the standby `Mode`.
- Standby data lives in the standby Mode's private arena or heap structure; it is **never** written to the shared `World` during preparation.
- If an entity referenced in the preparation cache is destroyed or invalidated during active gameplay, the cache entry becomes stale.
- During `Mode::commit()`, the cache is validated against the live `World`. Any stale reference is dropped and accounted for, or triggers a safe fallback.

---

## 4. Execution Model & Incremental Preparation

### A. Frame Execution Sequence
Every frame follows a deterministic sequence:
1. **Input Sampling**: Poll OS platform events and record immutable `InputSnapshot`.
2. **Clock Update**: Tick `WorldClock`, `ActionClock` (applying transition curve if in flight), and `UiClock`.
3. **Active Mode Tick**: Invoke `activeMode->update(world, actionDelta)`.
4. **World Simulation Tick**: Progress Flecs world systems:
   - Continuous global systems (physics, animation, transforms) execute.
   - Active state's enabled system groups execute.
   - Standby state's system groups are inactive (gated).
5. **Standby Mode Preparation**:
   - Compute available microsecond budget using `Budget` primitive (`core/budget.h`).
   - Call `standbyMode->prepare(world, budget)`.
   - Standby mode advances its incremental work (e.g., target spatial queries, pose computations). If budget is exhausted, it saves progress and yields until next frame.
6. **Commit Evaluation**: If a mode transition was requested and `standbyMode->isPrepared()`, invoke atomic transition.
7. **Render & Presentation**: Record and submit graphics commands (via Null RHI in headless simulation).

### B. Preparation Contract (`Mode::prepare`)
- **Signature**: `Result<PreparationStatus> prepare(World& world, const Budget& budget)`
- **Status values**: `InProgress`, `Ready`, `Failed`.
- **Invariants**:
  - Must strictly honor `budget.isExhausted()`. Default budget target is `<= 0.5 ms`.
  - Must never mutate active simulation state or shared entity components.
  - Safe to pause across frames.
  - Safe to cancel at any time via `cancelPrepare()`.

---

## 5. State Transition Model & Commit Validation

### A. Lifecycle State Machine
The `Conductor` maintains an explicit state machine:

```
[Uninitialized]
       │
       ▼ (initialize)
   [Active] ◄────────────────────────────────────────┐
       │                                             │
       ▼ (requestTransition)                         │
  [Preparing]                                        │ (validation failure
       │                                             │  or cancellation:
       ▼ (preparation completes)                     │  preserve old active)
   [Prepared]                                        │
       │                                             │
       ▼ (commit)                                    │
  [Committing] ──(validation failed: abort/rollback)─┘
       │
       ▼ (validation succeeded)
 [Transitioning] (apply slow-motion curve)
       │
       ▼ (transition curve completes)
   [Active] (roles reversed: former standby is now active)
```

### B. Pre-Commit Validation (`Mode::commit`)
Commit is called on the main thread and must complete in `<= 0.2 ms`.
Before activating the new state:
1. Verify standby state status is `Ready`.
2. Validate cached entity handles: check that referenced entities are alive in `World`.
3. Validate required resources and configurations.
4. If validation succeeds:
   - Call `oldActiveMode->exit(world)`.
   - Call `newActiveMode->commit(world)`.
   - Call `newActiveMode->enter(world)`.
   - Toggle system group phase masks in `World`.
   - Atomically swap active/standby role pointers.
   - Start transition slow-motion curve on `ActionClock`.
5. If validation fails:
   - Abort transition immediately.
   - Old active state remains active with zero disruption.
   - Log warning `W-0101` (Conductor cache invalidation).
   - Return `Result<void, ModeError>`. No half-committed state is ever permitted.

---

## 6. Execution and Time Domains

- **`WorldClock`**: Monotonic, unscaled wall-clock progression of simulation time.
- **`ActionClock`**: Simulation time driving active gameplay systems. Scaled by transition curve $\alpha(t)$ upon entering a new state (e.g. dynamic slow-down to 0.2x ramping back to 1.0x over 300 ms). Never stops or freezes.
- **`UiClock`**: Unscaled monotonic real time ensuring UI animations, menus, and HUD elements remain fluid during slow-motion transitions.

---

## 7. Concurrency & Event System Integration

- **World Mutation Policy**: Flecs world mutations are confined to the main thread or processed via deferred Flecs operations. Worker threads must never execute unrestricted structural mutations on Flecs tables.
- **Job System Integration**: Standby modes may dispatch read-only computation jobs (e.g. candidate target evaluations) via `JobSystem`, writing results into the mode's private memory arena.
- **Event Dispatching**:
  - Conductor emits structured events via `core::EventDispatcher`:
    - `ModeTransitionRequestedEvent`
    - `ModeTransitionCommittedEvent`
    - `ModeTransitionCancelledEvent`
    - `ModeTransitionFailedEvent`

---

## 8. Testing & Verification Strategy

All Phase 3 tests will run headlessly without requiring a window or physical GPU:
1. **Shared World Unit Tests (`world_test.cpp`)**:
   - Initialization and clean destruction.
   - Entity creation, component assignment, query iteration.
   - System group phase gating: enabling/disabling system phases and verifying execution.
   - Safe generational entity handle resolution and stale handle detection.
2. **Conductor Lifecycle & Preparation Tests (`conductor_test.cpp`)**:
   - Initial activation of State A.
   - Incremental preparation of State B across multiple frames respecting budget.
   - Preparation cancellation and state preservation.
   - Successful commit and atomic role reversal (B active, A standby).
   - Reversal back (A active, B standby).
   - Rejection of invalid commit (stale entity handle) with preservation of active state.
3. **Time Domain Tests (`clock_domain_test.cpp`)**:
   - Independent progression of World, Action, and UI clocks.
   - Transition slow-motion curve evaluation without pausing simulation.
4. **Stress & Determinism Tests (`conductor_stress_test.cpp`)**:
   - 10,000 continuous mode transitions in a loop.
   - Zero memory growth (verified under ASan).
   - Zero race conditions (verified under TSan).
   - Deterministic entity state matching identical random seeds.

---

## 9. Required Architecture Decision Records (ADRs)

1. **ADR-0008**: Adoption of Flecs as Engine ECS Foundation (pinned v4.0.4, MIT license, single-source distribution in `engine/world/src/flecs/`, C++20 wrapper, exception/RTTI-free compilation).
2. **ADR-0009**: Shared World and Two-State Conductor Execution Model (role reversal, speculative preparation cache, atomic commit validation, rollback guarantee).
