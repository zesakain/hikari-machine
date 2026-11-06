# Memory Management and Hot-Path Principles

## Ownership and Allocators

### 1. Ownership Model
- Every allocated block has a single, unambiguously declared owner encoded directly in type semantics.
- `std::shared_ptr` is prohibited in engine code to prevent hidden cache misses, uncontrolled lifetimes, and non-deterministic deallocation costs.
- `std::unique_ptr` is reserved for long-lived, non-pooled, subsystem-level objects.
- Non-owning references are passed as raw non-owning pointers (`T*`), references (`T&`), `std::span<T>`, or `std::string_view`.
- Raw `new`, `delete`, `malloc`, and `free` are prohibited in engine targets.

### 2. Allocator Hierarchy
Subsystems allocate exclusively via passed-in or owned allocator interfaces, adhering to this priority:

1. **Stack / Fixed Local Storage**: For short-lived calculation scratch spaces.
2. **Frame Allocator**: Linear bump allocator reset once at the end of each frame. Per-frame temporaries must never survive across frame boundaries.
3. **Subsystem / Mode Arena**: Bulk-freed when a mode, scene, or level terminates. Standby mode preparation data lives in the mode arena and is cleared or committed on transition.
4. **Pool Allocator**: Dedicated pools for fixed-size, high-frequency objects (components, jobs, particle nodes).
5. **General Heap**: Restricted to engine initialization, offline tools, and cold-path configuration loading.

### 3. Generational Handles
- Resources that can be created, destroyed, or recycled at runtime (assets, entities, GPU buffers, textures) are referenced by generational handles (`index + generation`).
- Resolving a stale handle returns null or a structured error value, preventing use-after-free and dangling pointer undefined behavior.
- Direct raw pointers into dynamically resizeable containers may not be held across frame boundaries.

## Hot-Path Rules

The hot path consists of:
- Input polling and dispatch
- Clock updates
- Fixed-step simulation loops
- Physics broadphase and narrowphase steps
- Skeletal animation pose evaluation
- Render graph construction
- Command buffer recording
- Frame presentation

### Constraints in the Hot Path
- **No Heap Allocations**: No calls to general allocators, no `std::vector` capacity growth, no `std::string` allocations, and no `std::function` closures.
- **No Mutex Contention**: Synchronization uses lock-free structures, per-thread staging buffers, or job system dependency declarations.
- **No Blocking I/O**: File loads, network exchanges, and compilation happen on asynchronous background worker jobs. Hot paths inspect ready state handles.
- **No Per-Entity Script Calls**: Script updates must be batched, event-driven, and subject to strict time budgets.
- **No Unbounded Logging**: High-frequency telemetry uses Tracy markers, atomic counters, or ring-buffer metrics rather than disk or console logging.
- **No Virtual Dispatch in Tight Loops**: Batches are grouped by concrete type; dispatch occurs per batch rather than per element.
