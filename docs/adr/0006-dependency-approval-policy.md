# ADR-0006: Third-Party Dependency Approval and Vendor Policy

- **Status**: Accepted
- **Date**: 2026-11-06
- **Authors**: zesakain

## Context

Uncontrolled adoption of third-party libraries causes dependency bloat, compiler warning leaks, hidden heap allocations, license incompatibilities, and long-term maintenance liabilities.

## Decision

Enforce a strict dependency management policy across the repository:
1. **Zero Unapproved Dependencies**: No third-party dependency may be introduced, vendored, or linked without an approved ADR and explicit approval.
2. **Phase 0 Baseline**: Phase 0 maintains zero third-party dependencies.
3. **Approval Criteria**: Any proposed dependency must:
   - Provide an ADR documenting necessity, alternatives evaluated, build impact, memory behavior, and license compatibility (Apache 2.0 compatible).
   - Support C++20 and compile warning-free under our enforced warning policy or be wrapped cleanly with isolated warning suppression.
   - Support exception-free (`-fno-exceptions`) and RTTI-free (`-fno-rtti`) compilation.
   - Allow custom allocator injection if it performs allocations in engine contexts.
4. **Candidate Technologies**:
   - Proposed libraries (flecs, Jolt, SDL3, volk, VMA, Slang, Lua, sol2, RmlUi, Dear ImGui) are documented as future candidates and remain uninstalled until evaluated in their respective phases under approved ADRs.

## Consequences

### Positive
- Prevents stealth dependency creep and maintains full control over memory and hot-path behavior.
- Keeps build times minimal and builds reproducible.

### Negative
- Requires formal evaluation and documentation before introducing even lightweight utility libraries.
