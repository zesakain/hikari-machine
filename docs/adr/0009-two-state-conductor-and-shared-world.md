# ADR-0009: Two-State Conductor Execution Runtime and Transition Semantics

- **Status**: Accepted
- **Date**: 2026-11-07
- **Authors**: zesakain

## Context

ADR-0005 established the Two-State Conductor as the engine's core operational model. To turn this design into a concrete, robust execution runtime, the exact lifecycle states, commit validation rules, incremental preparation contracts, failure rollback semantics, and time-domain boundaries must be formally specified.

## Decision

Implement the Conductor execution runtime with the following architectural rules:

1. **Authoritative Two-State Ownership**:
   - The `Conductor` maintains an active slot and a standby slot, alternating between State A and State B.
   - At no point do both states execute active gameplay logic simultaneously.
   - Both states operate over a single, shared `World`. Entities are continuous and never duplicated.

2. **Incremental Preparation as a Speculative Cache**:
   - The standby state's `prepare(world, budget)` runs incrementally across frames within a strict microsecond budget.
   - The standby state must treat all prepared data as a **speculative cache** stored in private state memory, never written directly to the shared `World`.
   - Preparation is safe to pause, resume, or cancel at any frame.

3. **Atomic Commit Validation and Rollback Guarantee**:
   - A state transition is never executed until pre-commit validation succeeds in `commit()`.
   - Validation checks:
     - Preparation status is `Ready`.
     - All referenced entity handles are alive in `World`.
     - Required assets and configurations are verified.
   - If validation fails, the transition is aborted immediately:
     - The currently active state remains active with zero interruption or corruption.
     - The failed preparation cache is invalidated.
     - Warning `W-0101` is logged, and an explicit error is returned.
     - The runtime is guaranteed never to enter a partially committed state.

4. **Decoupled Time Domains**:
   - `WorldClock`: Unscaled monotonic progression of the simulation.
   - `ActionClock`: Drives active gameplay logic. Subject to transition slow-motion curves upon entering a new state; resumes 1.0x speed after entry. Never pauses or freezes the process.
   - `UiClock`: Independent unscaled real time for UI responsiveness.

## Consequences

### Positive
- Amortized, predictable frame times with zero allocation or stutter on mode switch.
- Robust against stale references caused by entity destruction during standby preparation.
- Clear, verifiable runtime behavior with atomic commit guarantees.

### Negative
- Standby modes must implement incremental budget accounting and cache validation logic.
