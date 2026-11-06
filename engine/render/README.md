# Engine Render (`engine/render`)

## Responsibility
High-level rendering: Render graph generation, pass scheduling, material systems, visibility culling, lighting evaluation, and camera models.

## What It Does Not Do
- Contains no direct Vulkan or GPU driver calls (communicates strictly through `engine/rhi`).
- Contains no gameplay logic or entity definitions.

## Collaborators
- Uses `engine/core`, `engine/platform`, `engine/rhi`, and `engine/world`.
- Used by `engine/framework` and `game`.

## Conventions
- Public headers: `engine/render/include/render/`
- Tests: `tests/unit/render/`
