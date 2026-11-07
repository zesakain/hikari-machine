# ADR-0011: Render Graph Architecture

- **Status**: Accepted
- **Date**: 2026-11-07
- **Authors**: zesakain

## Context

Modern rendering pipelines comprise numerous interdependent passes (depth prepass, forward lighting, shadows, post-processing, UI). Hand-managing pipeline barriers, transient resource allocations, and pass execution order leads to synchronization hazards, resource aliasing bugs, and poor cache utilization.
Per Section 8 of `AGENTS.md`: "Passes are declared in the render graph with explicit inputs/outputs. No hand-placed barriers in feature code."

## Decision

Implement a lightweight, backend-independent Render Graph in `engine/render`:

1. **Declarative Pass Model**:
   - Rendering features declare passes using a two-phase setup/execute model:
     ```cpp
     graph.addPass("DepthPrepass",
         [&](RenderGraphBuilder& builder) {
             builder.writeTexture(depthTarget, TextureUsageFlags::DepthStencilAttachment);
         },
         [=](RhiCommandList& cmd) {
             // draw commands
         });
     ```
   - Passes declare explicit input/output resource handles (`GraphBufferId`, `GraphTextureId`).

2. **Topological Ordering & Cycle Detection**:
   - The graph builds an adjacency matrix of pass dependencies based on resource read-after-write (RAW), write-after-read (WAR), and write-after-write (WAW) hazards.
   - Directed cycle detection is performed via depth-first search. If a cycle is detected, graph compilation fails with `ErrorCode::InvalidState` and descriptive diagnostics.
   - Valid graphs are sorted topologically using Kahn's algorithm into a deterministic linear execution order.

3. **Resource Lifetime & Transition Planning**:
   - Pass dependency analysis determines the first and last pass using each resource.
   - Resources marked transient can share or alias memory outside their live intervals.
   - The graph calculates state transitions between passes (e.g. from `ColorAttachment` to `Sampled`), allowing backend-specific barrier generators to emit optimal synchronization without feature-level barrier placement.

4. **Hardware Independence & Headless Execution**:
   - The Render Graph depends only on the abstract `RhiDevice` and `RhiCommandList`.
   - The Render Graph can be fully tested and executed headlessly using `NullDevice`.

## Consequences

### Positive
- Centralized synchronization and hazard prevention.
- Eliminates manual barrier placement in rendering feature code.
- Fully testable in headless simulation environments.

### Negative
- Small graph compilation overhead per frame (minimized by pre-allocating graph nodes and reusing pass definitions).
