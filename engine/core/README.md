# Engine Core (`engine/core`)

## Responsibility
Foundational primitives: memory allocators, generational handles, job system interface, math primitives, assertions, logging, and error types.

## What It Does Not Do
- Contains no platform-specific windowing or OS event loops.
- Contains no graphics or simulation logic.
- Has zero dependencies on other engine modules.

## Collaborators
Used by all higher engine modules, game, editor, and tools.

## Conventions
- Public headers: `engine/core/include/core/`
- Tests: `tests/unit/core/`
