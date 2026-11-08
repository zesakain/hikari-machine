# Engineering Rules

This file is the contract between this repository and any AI coding agent working in it.
Read it fully before making changes. If a rule here conflicts with a user instruction, stop and ask.

## 1. Project Context

- General-purpose 3D engine. First target: fast-paced action games. Other genres are validated later through a second, unrelated test game.
- Distinguishing feature: a **mode-based architecture**. A `Conductor` runs one active `Mode`; standby modes prepare the next one over a single shared `World`.
- Hardware-neutral. The dev rig (NVIDIA Pascal, dual GPU) is a *test floor*, never a design target.
- Language: C++20. Build: CMake + Ninja. ECS: flecs. Physics: Jolt. Window/input: SDL3. Graphics: Vulkan behind our own RHI. Scripting: Lua 5.4 + sol2. Game UI: RmlUi. Editor UI: Dear ImGui.
- Priorities, in order: **correctness, frame-time consistency, input latency, simplicity, features.**

## 2. Hard Rules (never violate)

1. `/engine` must never `#include` anything from `/game` or `/editor`. Dependencies flow game → engine only.
2. `/engine` must contain no game-specific vocabulary (no character names, "overheat", "combo", etc.). Express game needs as generic capabilities.
3. Only `/engine/rhi` may include graphics API headers (Vulkan, D3D12, Metal). No vendor `#ifdef` outside the RHI.
4. No raw `new`/`delete`, `malloc`/`free`, or `std::shared_ptr` in engine code. See Section 5.
5. No heap allocation, locks, file I/O, logging at error-spam rates, or script calls in the hot path (Section 6).
6. No exceptions and no RTTI in engine code. Errors are returned as values.
7. Do not add a third-party dependency without an ADR and explicit approval.
8. Do not create an abstraction with fewer than two real users. Stay concrete until the second user exists.
9. Never disable a test, sanitizer, warning, or benchmark to make a build pass. Fix the cause.
10. Never commit generated files, build output, or large binary assets outside `/assets` with LFS.

## 3. Repository Layout

```
/engine     core, platform, rhi (+vulkan, null), render, world, physics, anim, audio, framework
/editor     ImGui panels (built only with EDITOR)
/game       modes, systems, lua (separate consumer of the engine)
/tools      cooker, offline data tools
/data       toml/json
/assets     glTF, textures, audio (source)
/docs       architecture, ADRs, GPU notes
/tests      unit, headless, golden, benchmarks
```

- Layers (low to high): Core → Rendering/Simulation → Framework → Tools. A layer may only use layers below it.
- Modules in the same layer communicate through small interfaces or events, never through each other's internals.
- Public headers live in `engine/<module>/include/<module>/`. Everything else is private.

## 4. C++ Style

**Language**
- C++20. Prefer `constexpr`, `enum class`, `std::span`, `std::string_view`, `std::optional`, `std::variant`, concepts.
- No exceptions, no RTTI, no `dynamic_cast`. Avoid inheritance except for deliberate interfaces (`Mode`, RHI backends).
- Prefer composition and free functions over deep hierarchies. Prefer data (POD structs) over objects with behavior.
- Avoid templates that hurt compile time or error readability. No template metaprogramming for its own sake.
- `const` by default. `[[nodiscard]]` on functions returning errors or handles.
- No `using namespace` in headers. No macros except for platform detection, profiling zones, and assertions.

**Naming**
- Types: `PascalCase`. Functions and methods: `camelCase`. Variables and members: `camelCase`; private members suffixed `_`.
- Constants and enum values: `PascalCase` (`kMaxFrames` for compile-time constants).
- Files: `snake_case.h` / `snake_case.cpp`. One primary type per file.
- Namespaces: `eng::` for engine, `eng::rhi::`, `eng::world::`, and so on. Games use their own namespace.

**Formatting and hygiene**
- `clang-format` (repo config) is authoritative. Run before every commit.
- `clang-tidy` must be clean on changed files. Warnings are errors in CI (`-Wall -Wextra -Wconversion -Wshadow`).
- Headers: `#pragma once`, minimal includes, forward-declare where possible. Include what you use.
- Functions should fit on one screen. Split by responsibility, not by line count alone.
- Units go in names or types: `seconds`, `meters`, `radians`. Never raw ambiguous floats at API boundaries.

### Comment Standard

Comments carry what the code cannot: intent, constraints, and consequences. A comment that is wrong is worse than no comment, because a reader will trust it while debugging. Write less, and make every line verifiable.

**Never write:**
- Comments that restate the code (`// increment i`).
- Commented-out code. Version control remembers it.
- Vague notes (`// fix later`, `// magic`, `// don't touch`). Explain precisely or delete.
- Any claim you have not verified (see "Verified claims only" below).

#### Tier 1: Required, public API (headers)

Every public function and type documents only what applies to it, in this order:

- **Purpose:** what it does and why it exists, in one or two sentences.
- **Contract:** preconditions, postconditions, invariants.
- **Ownership and lifetime:** who owns returned pointers/handles, and what invalidates them.
- **Thread-safety:** which threads may call it, whether it locks, whether it is safe inside a job.
- **Cost:** whether it allocates, and whether it is allowed in the hot path.
- **Units and ranges:** for every numeric parameter (`seconds`, `meters`, valid range).
- **Errors:** every way it can fail, what is returned, and what the caller should do.

Omit any item that is trivially obvious. Add a short example only for non-obvious APIs. Keep the whole block as short as the API allows.

#### Tier 2: When needed, implementations

Comment only where the reader would otherwise get it wrong:

- **Why** this approach was chosen, and which alternative was rejected (link the ADR or issue).
- **Non-obvious logic:** algorithms, math (state the formula and its source), ordering requirements.
- **Hidden coupling:** "must run before X", "index shared with Y".
- **Workarounds:** the exact bug or quirk avoided, plus `// REMOVE WHEN: <condition>`.
- **Assumptions:** state them AND enforce them with `ENG_ASSERT("message")`, so a violation fails loudly.
- **Measured performance choices:** `// PERF: <what was measured, result>`.

#### Tier 3: Verified claims only

The following may be written only if the agent has actually observed it (ran it, saw it in a log or Tracy capture) or it already exists in the repo:

- Symptoms ("if wrong, enemies keep moving during skill mode").
- Tracy zone, plot, or counter names.
- Error codes (`W-xxxx`, `E-xxxx`). Every code must already exist in `/docs/errors.md`. Never invent a code, and never copy codes from examples in this file.
- Benchmark or timing numbers.

If unverified, omit it. If it is useful but unverified, put it in the PR description as "unverified, please confirm", not in the code.

#### Greppable tags

- `// TODO(name, #issue): what and why`. Never bare.
- `// FIXME(#issue): known bug, trigger condition`.
- `// HACK: why it is ugly, what the clean fix is`.
- `// PERF:`, `// SAFETY:`, `// INVARIANT:`, `// DEVIATION: <rule> because <reason>`.

#### Where information belongs

- Root cause and diagnosis of a hard bug: the **commit message** and a **regression test**, not the code comment.
- Module responsibility and collaborators: the **module README**, not a per-file header. Add a short file header only when the file's role is not clear from its name.
- Design rationale across modules: an **ADR**.

#### Maintenance

- A comment that contradicts the code is a bug. Update or delete affected comments in the same commit as the code change.
- Prefer precision over length. If a comment grows past roughly a dozen lines, the content probably belongs in an ADR or README.
- Clear English, full sentences, present tense.
## 5. Memory Management

**Ownership**
- Every allocation has exactly one clear owner. Ownership is expressed in types, not comments.
- Use `std::unique_ptr` only for rare, long-lived, non-pooled objects. No `shared_ptr` (hidden cost, unclear lifetime). If shared ownership seems needed, use handles (below) or reconsider the design.
- Non-owning access: references, `std::span`, `std::string_view`, or handles. Raw pointers mean "non-owning, nullable, not responsible for lifetime".

**Allocators (preferred order)**
1. **Stack / fixed-size local** for small temporary data.
2. **Frame allocator**: linear, reset at end of frame. For per-frame temporaries. Nothing allocated here may outlive the frame.
3. **Arena per subsystem or per mode/level**: bulk-freed together. Standby-mode preparation data lives in that mode's arena, discarded or committed on switch.
4. **Pool allocator** for fixed-size, long-lived objects (components, particles, jobs).
5. **General heap** only for startup, tools, and rare cold paths. Never in the hot path.

**Handles instead of pointers**
- Assets and GPU resources are referenced by generational handles (`index + generation`). Resolving a stale handle returns null/error, never undefined behavior.
- No pointer into a container that can reallocate is stored across frames.

**Rules**
- Allocation sites use an allocator interface passed in or owned by the subsystem. No hidden global `operator new` replacement in the hot path.
- Memory budgets are declared per subsystem and asserted in debug builds. Track with Tracy memory zones.
- RAII for every resource (files, GPU objects, locks). No manual cleanup pairs.
- Cache layout matters: prefer SoA/flecs component storage, keep components small and POD, avoid pointer chasing in loops.
- Run ASan/UBSan on every PR in CI, TSan on the job system and anything threaded. Leaks fail the build.
- Mode switch must be leak-neutral: a stress test switching modes thousands of times must show flat memory.

## 6. Hot Path Rules

The hot path is: input read, clock update, fixed-step simulation, physics, animation, render graph build, command recording, and present.

In the hot path:
- **No** heap allocation, `std::vector` growth, `std::string` construction, or `std::function` creation.
- **No** mutex contention. Use lock-free structures, per-thread data, or job dependencies.
- **No** blocking file or network I/O. Load asynchronously in the background, then hand off via a ready handle.
- **No** Lua calls per entity per frame. Script work is batched, event-driven, and budgeted.
- **No** virtual dispatch inside tight per-element loops. Dispatch once per batch.
- **No** logging above a rate limit. Use counters and Tracy plots instead of spamming logs.
- Reserve capacity at startup. Pre-warm pipelines, shaders, pools, and VFX during standby `prepare()`.

Budgets (enforced in benchmarks and CI):
- Frame time p99 ≤ 16.6 ms at 1080p on Tier 0 hardware.
- `Mode::prepare(budget)` never exceeds its budget (default 0.5 ms per frame).
- `Mode::commit()` ≤ 0.2 ms.
- No frame-time spike on mode transition or first use of a shader.
- Input is sampled at the start of the frame and affects the same frame's render.

## 7. Concurrency

- Parallelism through the job system only. No ad-hoc `std::thread` in engine code.
- Jobs declare their data dependencies. Shared mutable state is forbidden unless wrapped in a documented synchronization primitive.
- Render command recording happens in jobs; submission happens on one thread.
- Never hold a lock across a job boundary or a GPU call.
- Any new threaded code requires a TSan-clean test.

## 8. Architecture-Specific Rules

**Core: the Conductor and its Two Time States (original design, do not generalize away)**
- The core of the engine is the **Conductor**. It owns exactly **two time states, A and B**. Only the one the Conductor calls is *active*; the two never run together.
- The state that is not called is on **standby**. Standby is not idle waiting: it **prepares the world-facing setup of the other state** so the next switch costs almost nothing. This is the engine's primary mechanism, not an optimization.
- Duties are mirrored. Whichever state is standby prepares what the *other* state needs on entry. Reference example (lives in `/game`, never in `/engine`): with the fast-paced state active, the idle state precomputes its option/effect matrix against nearby targets, prepares its camera and pose, pre-warms VFX, and loads its UI and montages. With the idle state active, the fast-paced state prepares its movement pose, camera, release effects, and marks which entities to wake.
- Both states operate on **one shared World**. Nothing is duplicated. Physics and VFX continue across a switch; what changes is which state's gameplay logic is called.
- Slow-motion is applied **only** to the transition into a state. After entry, time runs at normal speed. It is never a pause or a freeze.
- Generalization to more than two states (N-state registry with allowed-next graph) is a **later extension**. The two-state Conductor is the base case and must stay a first-class, simple, fully tested path. Do not replace it with a generic N-mode abstraction until a second real game proves the need.

**Conductor and Modes (shared rules)**
- One world, one copy of entities. Modes never duplicate world state.
- Modes enable or disable **system groups** (phases/tags), never individual systems.
- `prepare()` output is a **cache, not truth**. Always revalidate in `commit()`: entities may have died or moved while standby ran.
- `prepare()` is incremental and budget-aware. `commit()` only validates and installs. Heavy work never goes in `commit()`.
- Clocks are separate (world, action, UI). Slow-motion scales clocks via transition curves; it never pauses the process or stops physics unless a mode explicitly says so.
- Never use a global "pause" to implement a mode. Gate system groups instead.

**ECS (flecs)**
- Components are plain data, no behavior, no owning pointers. Systems hold the logic.
- Do not create or destroy entities in the middle of iteration; use deferred operations.
- Register components with reflection metadata so the editor, serialization, and Lua bindings share one source.
- Keep system queries narrow and cache them.

**Rendering and RHI**
- All rendering decisions use `GpuCaps`, never GPU names or vendor IDs.
- Feature tiers: Tier 0 (baseline) must always work. Tier 1/2 features are optional paths with a Tier 0 fallback.
- Shaders: one source (Slang/HLSL), compiled to SPIR-V and other targets. No per-vendor shader forks.
- Passes are declared in the render graph with explicit inputs/outputs. No hand-placed barriers in feature code.
- Pipeline and shader permutations are cached on disk and pre-warmed in standby.
- A `null` RHI backend must keep working so headless CI can run simulation tests.

**Physics and animation**
- Physics uses a fixed timestep with interpolation for rendering.
- Character-controller parameters are data and tunable live. No magic numbers in code.
- Animation montages must be cancelable. Input buffering and cancel windows are generic engine features.

**Scripting (Lua)**
- Lua expresses gameplay rules only. Anything called every frame for many entities belongs in C++.
- Bindings are generated from reflection. Do not hand-write one-off bindings without a reason in the PR.
- Script GC runs incrementally with a per-frame budget. Hot reload must be safe and tested.

**Data**
- Content (tables, skills, enemy definitions) lives in TOML/JSON with a schema and validation. No content constants hardcoded in C++.
- Loading data validates and reports precise errors (file, key, line). Never crash on bad data; reject it with a message.

## 9. Error Handling and Assertions

- Recoverable errors: return `std::expected`-style results (or an equivalent `Result<T, Error>`). Callers must handle them.
- Programmer errors: `ENG_ASSERT` (compiled out in release) with a message. Invariants that must hold in release use `ENG_VERIFY`.
- No silent failure. Either handle, propagate, or assert.
- Logging: categorized (`Core`, `Render`, `Physics`, ...) with levels. Errors include context (what, where, why).
- External input (files, network, scripts) is untrusted: validate before use.

## 10. Testing Requirements

Every change ships with tests appropriate to it:
- **Unit tests** (doctest/Catch2) for core, math, allocators, handle systems.
- **Headless simulation tests** for Conductor, modes, and ECS gating. Must run without a GPU or window (uses the `null` RHI).
- **Golden tests** for data-driven logic (e.g., table results). Changing a golden file requires explaining why in the PR.
- **Stress tests**: mode-switch loops (thousands of cycles), allocator churn, handle reuse.
- **Benchmarks**: frame time, `prepare()`/`commit()` cost, input latency. A regression beyond threshold fails CI.
- **Determinism**: simulation with a fixed seed and fixed dt must produce identical results across runs.
- A bug fix includes a test that fails without the fix.

## 11. Build and Dependencies

- One command from clean checkout builds and tests: `cmake --preset <name> && cmake --build --preset <name> && ctest --preset <name>`.
- Presets: `debug`, `release`, `profile`, `editor`, `asan`, `tsan`.
- Dependencies are pinned (vcpkg manifest or CPM with exact versions/hashes).
- Editor code is guarded by the `EDITOR` flag. A non-editor build must contain zero editor code or ImGui symbols.
- Keep compile times in check: forward declarations, no heavy includes in public headers, consider unity/PCH only with measurements.

## 12. Git and Review Workflow

### Commit Discipline (strict)

**Never bundle unrelated work into one large commit.** Each commit has exactly one clear scope and one reason to exist. Work is split into separate, self-contained commits, in a logical order.

**Splitting rules:**
- **One scope per commit.** A scope is one module or one concern (e.g. `conductor`, `rhi/vulkan`, `core/memory`, `build`, `docs`, `tests`). If a commit message needs the word "and" to join unrelated things, split it.
- **Separate by kind of change.** These are always different commits, never mixed:
  1. Refactor / rename / move (no behavior change)
  2. Formatting-only changes
  3. New feature or behavior
  4. Bug fix
  5. Tests (may accompany the change they test only if small; otherwise a separate commit directly after)
  6. Build system, CI, and dependency changes
  7. Documentation, ADRs, comments-only changes
- **Refactor first, feature second.** Prepare the ground in its own commit, then add the feature on top, so each step is reviewable and revertible.
- **Every commit builds and passes tests on its own.** No "fix build" follow-ups. This keeps `git bisect` usable.
- **Each commit is independently revertible** without breaking others in the series.
- **Do not mix drive-by fixes** into a feature commit. Note them and commit them separately.
- **Do not commit generated files, build artifacts, or unrelated formatting churn.**
- **Stage deliberately** (`git add -p`). Review `git diff --staged` before each commit to confirm only the intended scope is included.
- **Rough size guide:** if a commit touches many modules or is hard to describe in one summary line, it is too big. Prefer many small commits over one large one.

**Commit message format:**
```
<scope>: <imperative summary, max ~72 chars>

Why: the problem or motivation, in plain words.
What: the approach taken, including non-obvious decisions and rejected alternatives.
Impact: behavior changes, API changes, performance or memory numbers if the hot path is touched.
Testing: how it was verified (tests added, presets run, benchmark results).
Refs: ADR / issue links.
```
Examples of well-scoped commit series for one feature ("add prepare budget to Conductor"):
```
conductor: extract mode transition into its own function      (refactor)
core/time: add Budget type with microsecond accounting        (new, tested)
conductor: enforce prepare() budget per frame                 (feature)
tests/conductor: add budget overrun and stale-cache tests     (tests)
docs: add ADR-0007 on cache validation in commit()            (docs)
```

**Pull requests:**
- Every PR states: what changed, why, how it was tested, performance impact (numbers if the hot path is touched).
- A PR is a *series* of scoped commits, not one squashed blob. Do not squash away the scope separation.
- If a PR grows beyond one coherent topic, split it into multiple PRs.
- Architecture-affecting changes require an ADR in `/docs/adr/NNNN-title.md` (context, decision, alternatives, consequences).
- Do not rewrite or reformat unrelated code in a feature PR.
- Do not change public API of frozen modules (`Conductor`, `Mode`, `World`, `Clock`) without an ADR.

## 13. How the Agent Should Work

**Before coding**
- Read the relevant module, its tests, and any related ADR. Match existing patterns.
- State your plan briefly for non-trivial changes. If requirements are ambiguous or affect architecture, ask before writing code.
- Check the layer rules: will this change introduce an upward or cross-module dependency?

**While coding**
- Make the smallest change that solves the problem. No speculative features, no "while I'm here" refactors.
- Prefer boring, readable code over clever code.
- If you touch a hot path, reason about allocations, cache behavior, and locks, and say so in the PR.
- If you must deviate from a rule, leave a comment `// DEVIATION: <rule> because <reason>` and flag it in the PR summary.

**Commits and comments while working**
- Commit as you go, in small scoped commits (Section 12). Never leave all work as one big uncommitted or single-commit blob.
- Write comments to the standard in Section 4 as you write the code, not afterward. Update stale comments in the same commit as the code change.

**Before declaring done**
- Build passes in `debug`, `release`, and at least one sanitizer preset.
- Commit history is split by scope and kind; each commit builds and passes tests on its own.
- Comments meet the maintenance/debugging standard (contracts, invariants, failure modes, symptoms, tracing hooks).
- `clang-format` and `clang-tidy` are clean.
- New and existing tests pass; benchmarks show no regression beyond threshold.
- Docs updated: public API comments, module README, ADR if needed.
- Summarize: what changed, what was tested, known limitations, follow-up work.

**Honesty rules**
- Never claim something works without running it. State what was and was not verified.
- Never fabricate API names, library behavior, or benchmark numbers. If unsure, check the source or documentation, or say you are unsure.
- Report failures and flaky tests as they are. Do not hide or paper over them.
- If a task seems to require breaking a hard rule, stop and explain instead of working around it.

## 14. Definition of Done (per change)

1. Meets the stated requirement, and nothing more.
2. Respects layer boundaries and hard rules.
3. Tested (unit, headless, golden, or benchmark as appropriate).
4. Sanitizer-clean, warning-clean, format-clean.
5. Within performance and memory budgets.
6. Documented (comments, README, ADR where needed).
7. Reviewed against this file.
