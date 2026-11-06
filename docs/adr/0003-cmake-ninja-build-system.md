# ADR-0003: Selection of CMake and Ninja Build System

- **Status**: Accepted
- **Date**: 2026-11-06
- **Authors**: zesakain

## Context

Building a multi-platform 3D engine requires a flexible meta-build system that integrates cleanly with IDEs, CI runners, sanitizer toolchains, and package managers across Linux, Windows, and macOS.

## Decision

Adopt **CMake (minimum version 3.25)** with **Ninja** as the primary build system and generator.

Enforce the following build system conventions:
- Target-based modern CMake (`target_include_directories`, `target_compile_options`, `target_link_libraries`).
- No global directory pollution or unconstrained global compiler flags.
- Use of standardized `CMakePresets.json` for reproducible developer and CI builds (`debug`, `release`, `profile`, `editor`, `asan`, `tsan`).
- Single-command build and test workflow: `cmake --preset <name> && cmake --build --preset <name> && ctest --preset <name>`.

## Consequences

### Positive
- Universal support across Visual Studio, VS Code, CLion, and command-line environments.
- High-concurrency incremental builds via Ninja.
- `CMakePresets.json` guarantees consistent compiler flags, sanitizer definitions, and output directories between local development and CI.

### Negative
- CMake syntax can be verbose and complex if modern target scoping is not strictly adhered to.

## Alternatives Considered

- **Meson**: Fast and clean, but has less ubiquitous support across enterprise IDEs and game studio toolchains.
- **Premake / XMake**: Non-standard in larger multi-platform production pipelines.
- **Direct Ninja / Makefile**: Lack of native IDE project generation.
