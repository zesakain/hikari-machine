# Engine RHI (`engine/rhi`)

## Responsibility
Render Hardware Interface (RHI): Hardware abstraction for GPU commands, shader pipelines, buffers, textures, dynamic state, and synchronization.

## What It Does Not Do
- Contains no high-level scene management, render graph algorithms, or material systems.
- Never exposes native graphics API types (Vulkan/D3D12/Metal) in public headers.

## Collaborators
- Uses `engine/core` and `engine/platform`.
- Used by `engine/render`.

## Isolation Rule
This is the only module in the repository permitted to include graphics API headers.
All API-specific code resides in backend directories (`vulkan/`, `null/`).

## Conventions
- Public headers: `engine/rhi/include/rhi/`
- Tests: `tests/unit/rhi/`
