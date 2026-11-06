# Engine World (`engine/world`)

## Responsibility
Shared simulation state, Entity Component System (ECS) storage, component registries, query management, and entity lifecycle.

## What It Does Not Do
- Does not duplicate entity state across mode transitions.
- Contains no game-specific components or systems.

## Collaborators
- Uses `engine/core`.
- Used by `engine/physics`, `engine/anim`, `engine/framework`, and `game`.

## Conventions
- Public headers: `engine/world/include/world/`
- Tests: `tests/unit/world/` and `tests/headless/world/`
