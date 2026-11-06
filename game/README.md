# Game (`game`)

## Responsibility
Game-specific implementation: concrete `Mode` classes (e.g. Action Mode, Selection Mode), game gameplay systems, combat parameters, character controllers, and Lua scripts.

## Boundary
- Sits at the top of the dependency graph; depends on `engine`.
- Engine code must never `#include` anything from `game`.
