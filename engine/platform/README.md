# Engine Platform (`engine/platform`)

## Responsibility
Operating system abstractions: window lifecycle, input event translation, timing sources, and display enumeration.

## What It Does Not Do
- Contains no rendering logic or graphics pipeline setup.
- Does not process high-level gameplay action mappings.

## Collaborators
- Collaborates with `engine/core`.
- Dispatches platform events to `engine/framework` and `engine/rhi`.

## Conventions
- Public headers: `engine/platform/include/platform/`
- Tests: `tests/unit/platform/`
