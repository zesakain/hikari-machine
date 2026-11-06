# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
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
