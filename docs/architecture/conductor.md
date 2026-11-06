# Two-State Conductor Architecture

## Concept & Purpose

The engine's execution flow is driven by the **Conductor**, an architectural orchestrator that manages exactly two operational states: **State A** and **State B**.

At any point in time:
- One state is **active**: It executes its simulation systems, evaluates game logic, processes input, and renders the current view.
- The other state is in **standby**: It is not idle. Instead, it uses a strict per-frame budget to incrementally prepare the world-facing setup for the upcoming transition back to itself.

The duties are mirrored: whichever state is on standby prepares what the other state needs upon entry.

## Single Shared World

- Both states operate directly on a single shared `World`.
- Entities and components are never cloned or duplicated across states.
- Dynamic physical interactions, audio voices, and visual particles persist across state transitions.
- A state switch does not recreate the scene; it alters which system groups (phases or tags) are actively scheduled by the framework.

## Preparation vs. Commitment

The transition mechanism is divided into two distinct phases:

### 1. Incremental Standby Preparation (`Mode::prepare`)
- Runs during frames where the state is on standby.
- Given a strict execution budget (default budget <= 0.5 ms per frame).
- Performs heavy, amortized computations:
  - Precomputing option and collision matrices against nearby targets.
  - Setting up target cameras, blends, and orientation vectors.
  - Pre-warming visual effects pipelines, shaders, and particle pools.
  - Preloading upcoming UI elements and animation montages.
- **Invariable Rule**: The output of `prepare()` is a **cache**, not authoritative truth. Entities may have moved or been destroyed while the standby state was preparing.

### 2. State Commitment (`Mode::commit`)
- Called synchronously on the main thread during a mode transition.
- Must be lightweight and fast (target execution <= 0.2 ms).
- **Responsibility**: It validates the cache generated during `prepare()` against current world state and installs cameras, input listeners, and system group masks.
- If an entity in the cache is missing or stale, it is dropped or recomputed. If the cache is invalid or missing, a fallback rebuild is logged as a warning (`W-0101`).

## Clock Model & Slow-Motion

- Clocks are decoupled: World clock, Action/Gameplay clock, and UI clock run independently.
- Slow-motion is applied **only** to the transition phase entering a state, utilizing defined transition curves.
- Once state entry completes, the clock resumes normal operational speed.
- Slow-motion is never implemented as a global process pause or tick freeze. Simulation continues smoothly.

## Intentional Two-State Design

The two-state Conductor is the first-class design for this engine. Generalizing to an N-state registry with arbitrary transitions is deferred until multi-genre production requirements prove the necessity through real application needs.
