# Engine Animation (`engine/anim`)

## Responsibility
Skeletal hierarchy evaluation, animation blending, state machines, animation montages, and root motion extraction.

## What It Does Not Do
- Does not trigger combat damage directly; exposes generic event tracks and cancel windows.
- Contains no mesh skinning shaders (mesh skinning is submitted through `engine/render`).

## Collaborators
- Uses `engine/core` and `engine/world`.
- Provides evaluated poses to `engine/render`.

## Conventions
- Public headers: `engine/anim/include/anim/`
- Tests: `tests/unit/anim/`
