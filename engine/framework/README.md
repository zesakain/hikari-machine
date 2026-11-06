# Engine Framework (`engine/framework`)

## Responsibility
Core application flow: Two-State Conductor orchestration, Mode lifecycle interfaces (`prepare`, `commit`), independent clock management, and system group scheduling.

## What It Does Not Do
- Contains no concrete game modes or game-specific rules.
- Does not duplicate `World` state.

## Collaborators
- Coordinates `engine/world`, `engine/physics`, `engine/render`, and `engine/platform`.
- Drives mode implementations defined in `game`.

## Conventions
- Public headers: `engine/framework/include/framework/`
- Tests: `tests/headless/conductor/`
