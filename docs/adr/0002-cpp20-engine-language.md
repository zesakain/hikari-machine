# ADR-0002: Adoption of C++20 as Primary Engine Language

- **Status**: Accepted
- **Date**: 2026-11-06
- **Authors**: zesakain

## Context

A high-performance 3D engine requires deterministic memory management, zero-cost abstractions, direct hardware interaction, and broad multi-platform compiler support. The team evaluated modern C++ standards to select the baseline language standard.

## Decision

Adopt **C++20** as the primary language standard for the engine, tools, tests, and game code.

The following subset and policies are enforced:
- **Enabled Language Features**:
  - `constexpr` algorithms and data structures.
  - Concepts and constraints for template validation without metaprogramming bloat.
  - `std::span` and `std::string_view` for non-owning memory references.
  - Designated initializers and structured bindings.
  - `std::optional` and `std::variant` for explicit value semantics.
- **Explicitly Forbidden Features**:
  - C++ Exceptions (`-fno-exceptions`, `/EHs-c-`): Errors are propagated as value types (`Result<T, Error>`).
  - Runtime Type Information (`-fno-rtti`, `/GR-`): Type resolution uses explicit reflection tags and enum discrimination.
  - `std::shared_ptr`: Ownership must be unique or handle-based.
  - Metaprogramming that degrades build times or obscures compiler diagnostics.

## Consequences

### Positive
- Standardized non-owning memory views (`std::span`) reduce bounds errors and eliminate unnecessary copies.
- Concepts make template requirements readable and produce clear compiler diagnostics.
- Disabling RTTI and exceptions reduces binary size and prevents runtime overhead in the hot path.

### Negative
- Requires modern toolchains (GCC 12+, Clang 15+, MSVC 2022+).
- Standard library components that rely on exceptions must be avoided or guarded.

## Alternatives Considered

- **C++17**: Lacks standard `std::span`, concepts, and rich `constexpr` library utilities.
- **C++23**: Toolchain support is still inconsistent across platform SDKs and embedded/console compilers.
- **Rust**: While offering memory safety guarantees, existing C/C++ game engine ecosystems (Vulkan, Jolt, SDL3, flecs) integrate more seamlessly with modern C++20 without FFI friction.
