# Runtime Session & World Lifecycle

## 1. Overview

Hikari Editor Milestone 5 introduces the **Runtime Session** architecture, enabling interactive simulation workflows:
- **Edit Mode:** The user authors entities, sets up transforms, configures colliders, adjusts rigid body properties, and scripts routines in the authoritative **Edit World**. Simulation systems (physics stepping, script callbacks) are idle.
- **Play:** When Play is triggered, the engine freezes and clones the Edit World into a separate, isolated **Runtime World** completely in memory. All active simulation pipelines (Jolt Physics, Lua Script Engine, Two-State Conductor) initialize for the Runtime World.
- **Pause & Resume:** The simulation can be paused at any instant, freezing physics and script execution while keeping the editor interface and entity inspection fully responsive. Resuming restarts simulation without hitching.
- **Step:** While paused, the user can advance simulation by a single discrete fixed timestep.
- **Stop:** When Stop is triggered, the simulation ceases immediately. The Runtime World, its Jolt physics bodies, and Lua execution state are safely and completely destroyed. The editor seamlessly returns to Edit Mode with the Edit World **100% intact and untouched**.

---

## 2. World Lifecycle Architecture

```
                                  [ User Authors Scene ]
                                            │
                                            ▼
                                  ┌───────────────────┐
                                  │    Edit World     │ (Authoring State)
                                  └─────────┬─────────┘
                                            │
                     Play Pressed           │ In-Memory Staging via
                                            │ SceneSerializer (zero disk I/O)
                                            ▼
                                  ┌───────────────────┐
                                  │   Runtime World   │ (Isolated Ephemeral State)
                                  ├───────────────────┤
                                  │ Jolt PhysicsWorld │
                                  │ Lua ScriptEngine  │
                                  │ Conductor Runtime │
                                  └─────────┬─────────┘
                                            │
               Simulation advances (gravity,│ physics, scripts, mutations)
                                            ▼
                                  [ Modified Positions ]
                                  [ Ephemeral Entities ]
                                            │
                     Stop Pressed           │
                                            ▼
                               [ Runtime World Destroyed ]
                               [ Jolt Bodies Destroyed   ]
                               [ Script State Released   ]
                                            │
                                            ▼
                                  ┌───────────────────┐
                                  │    Edit World     │ (Completely Unaltered!)
                                  └───────────────────┘
```

### Absolute World Isolation
1. **Zero State Leakage:** Runtime simulations (e.g., falling physics cubes, projectile spawns, script mutations) operate exclusively upon `RuntimeWorld`. No pointers, references, or raw entity IDs are shared with `EditWorld`.
2. **Ephemeral Lifetimes:** All entities, bodies, and components created during simulation are contained within `RuntimeWorld`. Upon Stop, they are destroyed together with `RuntimeWorld`.
3. **Pristine Edit World:** Transforms, entity counts, component properties, and selection states in the authoring Edit World remain identical to their pre-Play state.
4. **Scene Dirty State Guard:** Mutations occurring during simulation do not mark the authored `.hikari` scene dirty (`requestMarkDirty` is rejected while `isRuntimeActive == true`).

---

## 3. Simulation State Machine

The session is governed by an explicit four-state machine:

```
          ┌─────────────────────────────────────────────────────────┐
          │                                                         │
          ▼                                                         │
     ┌─────────┐             play()               ┌─────────┐       │
     │  Edit   │ ───────────────────────────────> │ Playing │       │
     └─────────┘                                  └────┬────┘       │
          ▲                                            │            │
          │                                   pause()  │            │
          │                                            ▼            │
          │ stop()                                ┌─────────┐       │ stop()
          │                                       │ Paused  │ ──────┘
          │                                       └────┬────┘
          │                                            │
          │                  resume() / play()         │
          └────────────────────────────────────────────┘
```

### State Definitions & Valid Transitions
| Current State | Action | Next State | Description |
| :--- | :--- | :--- | :--- |
| **`Edit`** | `play()` | **`Playing`** | Stages Edit World into memory, allocates Runtime World & simulation systems, begins ticking. |
| **`Playing`** | `pause()` | **`Paused`** | Halts simulation stepping; freezes physics and scripts. UI remains active. |
| **`Paused`** | `resume()` | **`Playing`** | Resumes simulation ticking from frozen state. |
| **`Paused`** | `step(dt)` | **`Paused`** | Advances exactly one discrete fixed step while remaining paused. |
| **`Playing`** | `stop()` | **`Edit`** | Destroys Runtime World and systems, returns editor to Edit Mode. |
| **`Paused`** | `stop()` | **`Edit`** | Destroys Runtime World and systems, returns editor to Edit Mode. |
| **`Stopped`** | `play()` | **`Playing`** | Starts a fresh simulation run from current Edit World. |

---

## 4. Conductor and Physics Integration

### Two-State Conductor Integration
- In Edit Mode, `ConductorPanel` monitors the authoring or standalone conductor state.
- In Runtime Mode (`Playing` or `Paused`), `RuntimeSession` instantiates a dedicated runtime `Conductor` operating over the `RuntimeWorld`.
- The user can trigger runtime state transitions between State A and State B (and standby preparation) during active simulation.

### Physics Body Creation
- When `play()` stages the scene into `RuntimeWorld`, `RuntimeSession` queries all entities possessing `RigidBodyComponent`.
- For each entity, a corresponding Jolt rigid body is instantiated in `runtimePhysicsWorld` with shape parameters read from `ColliderComponent` and transform from `PhysicsTransformComponent`.
- The continuous `PhysicsPreSync`, `PhysicsStep`, and `PhysicsPostSync` pipelines advance physics and synchronize transforms with Flecs.
- Upon `stop()`, reverse-order teardown ensures the `RuntimeWorld` is destroyed while `runtimePhysicsWorld` is still alive, allowing Flecs `OnRemove` observers to cleanly destroy Jolt bodies before `runtimePhysicsWorld` is freed.

---

## 5. Editor Workspace & Controls

- **Main Toolbar:**
  - `[>] Play`: Enabled in `Edit` and `Stopped`. Disabled while running.
  - `[||] Pause`: Enabled in `Playing`. Displays green active indicator.
  - `[>] Resume`: Enabled in `Paused`. Displays amber resume indicator.
  - `[>>] Step`: Enabled in `Paused`. Advances simulation by 16 ms.
  - `[X] Stop`: Enabled in `Playing` and `Paused`. Displays red stop accent.
- **Badges:**
  - Toolbar & Status Bar: `[EDIT MODE]`, `[PLAYING]`, `[PAUSED]`.
  - Scene Hierarchy: `[LIVE RUNTIME WORLD]` badge appears when simulation is active.
  - Entity Inspector: `[RUNTIME SIMULATION ENTITY]` badge indicates ephemeral inspection.
- **Selection Safety:**
  - `SelectionModel` is cleared upon Play and Stop to ensure no cross-world dangling entity handles exist.
- **Shortcuts:**
  - `F5` or `Ctrl+P`: Play / Pause / Resume toggle.
  - `Shift+F5` or `Ctrl+X`: Stop simulation.
  - `F10`: Single simulation step (while paused).
