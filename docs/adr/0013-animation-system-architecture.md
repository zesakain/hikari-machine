# ADR-0013: Skeletal Animation System Architecture

- **Status**: Accepted
- **Date**: 2026-11-07
- **Authors**: zesakain

## Context

Phase 5 requires a skeletal animation system to sample, blend, and evaluate bone poses for 3D character movement and action gameplay. The animation subsystem must:
- Represent hierarchical skeletons with parent-child bone relationships and inverse bind poses.
- Sample keyframed animation tracks (translation, rotation, scale) with linear and spherical linear (SLERP) interpolation.
- Support runtime playback state control (play, pause, stop, seek, speed, loop) and cross-fade blending between animation clips.
- Decouple pose evaluation from GPU rendering and Vulkan shaders.
- Integrate smoothly with the Shared World (Flecs ECS) and Two-State Conductor.
- Avoid introducing external third-party math or animation graph bloat.

## Decision

Implement a focused, modular skeletal animation pipeline in `engine/anim/`:

### 1. Data Model & Hierarchy
- `Bone`: POD representation containing parent index (`int16_t`, -1 for root), bone name, local rest pose `Transform`, and inverse bind pose `Mat4`.
- `Skeleton`: Ordered array of bones where parent indices are strictly less than child indices (`parentIndex < childIndex`), ensuring topological order for simple linear forward kinematics evaluation without recursion or stack allocation.
- `AnimationClip`: Named collection of bone animation tracks with keyframe times and values.

### 2. Sampling and Interpolation
- Translation and scale tracks interpolate linearly (`lerp`).
- Rotation tracks use spherical linear interpolation (`slerp`) with shortest-path quaternion correction.
- Keyframe boundary handling clamps out-of-range times safely or wraps them for looping clips.

### 3. Playback and Blending
- `AnimationController`: Maintains playback state, current playhead time, speed, looping mode, and blend weight.
- Blending: Interpolates between local bone transforms of two animation clips using normalized blend factor $\alpha \in [0, 1]$ before forward kinematics propagation.

### 4. ECS Integration & Two-State Conductor
- Flecs ECS components:
  - `SkeletonComponent`: References shared `Skeleton` resource.
  - `AnimationPlaybackComponent`: Manages active clip, playback controller, and cross-fade targets.
  - `EvaluatedPoseComponent`: Stores computed model-space bone matrices accessible by gameplay or future rendering systems.
- Gated evaluation: Evaluated poses are updated each frame based on the active `ActionClock` (or `WorldClock`), respecting time scaling during slow-motion transition entries without double evaluation.

## Consequences

### Positive
- Zero external dependencies: Built cleanly on C++20 standard library and Hikari core math primitives.
- Deterministic forward kinematics evaluated in $O(N)$ linear time cache-friendly traversal.
- Decoupled from rendering: Produces clean pose data usable by CPU simulation or future GPU skinning passes.

### Negative
- Advanced state-machine graphs, root motion extraction, and procedural inverse kinematics (IK) are deferred to future milestones.
