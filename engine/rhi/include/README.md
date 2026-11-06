# RHI Public Interface (`engine/rhi/include`)

## Responsibility
Contains the abstract, API-agnostic interfaces, descriptor structs, and resource handles for the RHI (`Device`, `CommandBuffer`, `Buffer`, `Texture`, `Shader`, `Pipeline`, `GpuCaps`).

## Invariant
Must never include native graphics API headers (`vulkan.h`, etc.).
