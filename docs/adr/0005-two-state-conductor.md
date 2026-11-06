# ADR-0005: Two-State Conductor as Foundation Execution Model

- **Status**: Accepted
- **Date**: 2026-11-06
- **Authors**: zesakain

## Context

Action games frequently switch between operational modes (e.g., active high-speed combat vs. tactical target selection or interaction modes). Traditional designs either pause the engine entirely, snapshot and duplicate entity states, or incur severe frame spikes when activating heavy UI and asset setups.

## Decision

Adopt the **Two-State Conductor** as the engine's core operational model:
- The Conductor owns exactly two states: State A and State B.
- Exactly one state is **active** at any moment, running simulation and gameplay systems.
- The state not active is in **standby**, incrementally preparing the world-facing resources and state transitions for the next switch under a strict per-frame budget.
- Both states share **one World**; entities, physics, and visual effects are continuous and never duplicated.
- Transition slow-motion is applied exclusively to the transition curve entering a state; normal time resumes upon entry.
- Do not generalize into an arbitrary N-state state machine or state graph until multi-genre production requirements prove the necessity through real application needs.

## Consequences

### Positive
- Predictable, amortized mode switches without allocation spikes or blocking stalls.
- Zero state duplication keeps memory footprints lean and avoids sync bugs.
- Clear mental model and verification paths for simulation continuity.

### Negative
- Requires standby states to treat their prepared data as a speculative cache that must be re-validated at commit time.
