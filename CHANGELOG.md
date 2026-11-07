# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Core Slot Map (Phase 1.5)**:
  - `SlotMap<T>` in `<core/slot_map.h>` providing a stable-reference, generational object container indexed by `GenerationalHandle` over caller-provided backing memory.
  - Zero heap allocation and no dynamic resizing; explicit capacity bounds and safe failure modes.
  - O(1) in-place construction (`emplace`) and destruction (`erase`) supporting non-copyable and non-movable non-trivial types.
  - Generation tracking with safe overflow handling (`kRetiredGeneration`) preventing ABA stale-handle resurrection.
  - Comprehensive unit test suite in `tests/unit/core/slot_map_test.cpp`.
- **Core Structured Logging (Phase 1.4)**:
  - `LogLevel` enum and string conversion in `<core/log.h>` providing `Trace`, `Debug`, `Info`, `Warning`, `Error`, and `Critical` severity tiers.
  - `LogRecord` structured record layout capturing monotonic timestamp, severity, category tag, source location, and fixed-capacity message payload.
  - `Logger` in `<core/log.h>` and `src/log.cpp` implementing a non-blocking, bounded ring buffer with configurable severity filtering, zero hot-path heap allocation, non-blocking overflow rejection, and dropped record accounting.
  - `flush()` for cold-path FIFO emission to streams with dropped-record overflow diagnostics.
  - Ergonomic logging macros (`ENG_LOG`, `ENG_LOG_INFO`, `ENG_LOG_WARN`, `ENG_LOG_FORMAT`, etc.) bound to `Logger::defaultLogger()`.
  - Comprehensive unit test suite in `tests/unit/core/log_test.cpp`.
- **Core Clock & Time Subsystem (Phase 1.3)**:
  - `Duration` in `<core/time/duration.h>` providing nanosecond-precision, integer-based time duration representation with unit conversions, overflow bounds, and arithmetic.
  - `Timestamp` in `<core/time/timestamp.h>` providing monotonic point-in-time representation backed by `std::chrono::steady_clock` with duration arithmetic.
  - `Clock` in `<core/time/clock.h>` providing independent timekeeper instances with real vs. scaled delta tracking, pause/resume, and negative scale protection.
  - `Budget` in `<core/time/budget.h>` providing execution-time quota tracking with microsecond accounting for standby mode preparation and incremental tasks.
  - Master include header `<core/time.h>`.
  - Comprehensive unit test suite in `tests/unit/core/time_test.cpp`.
- **Fixed-Size Pool Allocator & Generational Handles (Phase 1.2)**:
  - `PoolAllocator` in `<core/pool_allocator.h>` providing fixed-size, O(1) allocation and deallocation over caller-provided backing memory with intrusive free-list storage, configurable alignment, and double-deallocation rejection.
  - `GenerationalHandle` in `<core/generational_handle.h>` providing a lightweight 64-bit handle primitive (32-bit index + 32-bit generation) with explicit invalid sentinels, validity checks, and bit packing.
  - Comprehensive unit test suites in `tests/unit/core/pool_allocator_test.cpp` and `tests/unit/core/generational_handle_test.cpp`.
- **Core Memory Allocators (Phase 1.1)**:
  - Alignment utilities in `<core/align.h>` (`isPowerOfTwo`, `alignUp`, `isAligned`).
  - `LinearAllocator` in `<core/linear_allocator.h>` providing alignment-aware bump allocation over caller-provided memory with overflow safety.
  - `ArenaAllocator` in `<core/arena_allocator.h>` providing grouped memory management with scoped checkpoint rollback (`save()`, `restore()`) and bulk reset.
  - Comprehensive unit test suites in `tests/unit/core/linear_allocator_test.cpp` and `tests/unit/core/arena_allocator_test.cpp`.
- **Core Error Handling & Assertions (Phase 1.0)**:
  - `ENG_ASSERT` and `ENG_VERIFY` diagnostic assertion macros in `<core/assert.h>`.
  - Allocation-free `Error` and `ErrorCode` types in `<core/error.h>` with source location diagnostics.
  - Exception-free `Result<T, E>` and `Result<void, E>` in `<core/result.h>` supporting move-only types.
  - `hikari::engine_core` module target exporting `engine/core/include`.
  - Comprehensive unit test suite in `tests/unit/core/` covering success, failure, move-semantics, and assertion behaviors.

## [0.1.0] - 2026-11-06

### Added
- **Repository Foundation & Architecture (Phase 0)**:
  - Initial repository layout and module boundaries (`engine/{core,platform,rhi,render,world,physics,anim,audio,framework}`, `editor`, `game`, `tools`, `data`, `assets`, `tests`).
  - Architecture specifications: `docs/architecture/overview.md`, `layers.md`, `conductor.md`, `memory_and_hot_path.md`.
  - Architecture Decision Records: ADR-0001 through ADR-0006.
  - Reserved error reference catalog: `docs/errors.md`.
  - GPU hardware profile and Tier 0 target notes: `docs/gpu/README.md`.
  - Formal Engineering Rules contract: `AGENTS.md`.
- **Build System**:
  - Root `CMakeLists.txt` enforcing C++20 standard, strict warnings (`-Wall -Wextra -Wconversion -Wshadow -Wpedantic`), no-exceptions (`-fno-exceptions`, `_HAS_EXCEPTIONS=0`), and no-RTTI (`-fno-rtti`, `/GR-`).
  - `CMakePresets.json` with 6 standard presets: `debug`, `release`, `profile`, `editor`, `asan`, and `tsan`.
  - Example local configuration template: `CMakeUserPresets.json.example`.
- **Testing & Tooling**:
  - Dependency-free build smoke test: `tests/smoke_test.cpp` exercising C++20 concepts, `std::span`, `std::string_view`, `std::optional`, and generational handle layouts.
  - Authoritative code style: `.clang-format`.
  - Static analysis configuration: `.clang-tidy`.
  - CI workflow: `.github/workflows/ci.yml`.
