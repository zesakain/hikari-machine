# ADR-0016: Engine Application Target, Binary Build Pipeline, and CI/CD Automation

- **Status**: Accepted
- **Date**: 2026-11-07
- **Authors**: zesakain

## Context

Phase 6 requires transforming Hikari from a collection of libraries into a fully distributable, tested engine with an executable application target, deterministic packaging, and an automated continuous integration / continuous delivery (CI/CD) pipeline.

## Decision

### 1. Primary Engine Executable (`hikari`)
- Establish `hikari` as the primary executable target in `src/main.cpp` (or `engine/main.cpp`).
- Output path: `${CMAKE_BINARY_DIR}/bin/hikari`.
- Initializes subsystems in strict dependency order: Core -> Platform -> RHI -> World -> Physics -> Anim -> Script -> UI -> Conductor.
- Supports `--headless` CLI flag for headless simulation, automated benchmarking, and CI environments.
- Performs orderly shutdown in reverse dependency order with zero resource leaks.

### 2. Binary Output & Packaging
- Binary layout:
  - `bin/`: Executable binaries (`hikari`, test executables).
  - `lib/`: Static/shared libraries.
  - `share/hikari/`: Assets, default data, licenses, build metadata.
- CPack configuration: generates `.tar.gz` and `.zip` distribution bundles with SHA256 checksums.

### 3. CI/CD Matrix & Automation
- GitHub Actions workflow (`.github/workflows/ci.yml`):
  - Matrix across all supported presets: `debug`, `release`, `profile`, `editor`, `asan`, `tsan`.
  - Runs formatting checks (`clang-format`), static analysis (`clang-tidy`), CTest execution, and uploads build artifacts.
- Release workflow (`.github/workflows/release.yml`):
  - Triggered on version tag push (`v*`).
  - Verifies clean test run, packages release binaries, generates SHA256 checksums, and attaches release assets.

## Consequences

### Positive
- Production-grade binary artifacts built and tested deterministically on every commit.
- Fully automated regression prevention across memory sanitizers and thread sanitizers.
- Clean separation between build tree and installed runtime package.

### Negative
- Requires maintaining GitHub Actions workflow configurations and CPack packaging rules.
