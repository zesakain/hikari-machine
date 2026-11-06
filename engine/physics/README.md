# Engine Physics (`engine/physics`)

## Responsibility
Fixed-timestep simulation of rigid bodies, collision queries, raycasts, character controller physics, and broadphase management.

## What It Does Not Do
- Contains no animation blending or skeletal mesh deformation.
- Contains no game-specific combat rules or damage calculators.

## Collaborators
- Uses `engine/core` and `engine/world`.
- Collaborates with `engine/framework` for fixed-tick scheduling.

## Conventions
- Public headers: `engine/physics/include/physics/`
- Tests: `tests/unit/physics/`
