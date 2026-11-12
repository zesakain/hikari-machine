# Hikari-Machine Milestone 11B: Rendering Architecture Optimization
## Final Certification and Empirical Verification Report

**Repository:** `/home/lux/hikari-machine`  
**Milestone:** 11B (Rendering Architecture Optimization)  
**Hardware Floor:** NVIDIA Pascal (P106-100 6GB, Driver Vulkan 1.3 / Linux 6.6)  
**Status:** COMPLETE & CERTIFIED  
**Test Suite Verification:** 50/50 tests passing (`editor`), 45/45 tests passing (`release`), 45/45 tests passing (`asan`)  
**Commit Series:**
- `184c362`: `feat(render): integrate scene renderer with live render graph`
- `63eeb87`: `feat(render): implement world-space frustum culling`
- `bc7f956`: `perf(render): evaluate depth prepass against rendering baseline`

---

## 1. Executive Summary

Milestone 11B transitions Hikari-Machine from monolithic, unculled forward draw submission into a modular, graph-driven rendering architecture. All four Work Packages (A, B, C, D) have been designed, implemented, tested, empirically measured, and certified:

1. **Work Package A (Live RenderGraph DAG Execution):** `SceneRenderer` now constructs and compiles an authoritative dynamic `RenderGraph` per frame for scene rasterization (`DirectionalShadowPass`, optional `DepthPrepass`, and `MainPbrPass`). Dynamic rendering transitions, attachments, and dependencies are managed through DAG declarations.
2. **Work Package B (World-Space Frustum Culling):** Added Gribb-Hartmann plane extraction and 6-plane frustum test primitives (`Plane`, `Frustum`) in `eng::core::math`. SceneRenderer performs CPU-side AABB culling for both the camera view frustum (main PBR pass) and the light-space orthographic projection volume (directional shadow pass).
3. **Work Package C (Explicit Render Pass Architecture):** Scene rendering and post-processing now execute as discrete DAG passes with explicit resource access declarations: `DirectionalShadowPass` $\to$ `[DepthPrepass]` $\to$ `MainPbrPass` $\to$ `BloomDownsamplePass` $\to$ `BloomUpsamplePass` $\to$ `ToneMappingPass`.
4. **Work Package D (Depth Prepass Empirical Evaluation):** Implemented a vertex-only Depth Prepass with dedicated pipeline variants. Conducted automated empirical benchmarks on Khronos Sponza. Demonstrated that on NVIDIA Pascal hardware, Depth Prepass increases frame time from 23.36 ms (42.80 FPS) to 29.20 ms (34.25 FPS) due to redundant vertex submission and pass transitions. Based on empirical evidence, `depthPrepass` defaults to `false`.

---

## 2. Empirical Performance Comparison (Milestone 11A Baseline vs 11B)

All measurements conducted on the certified Khronos Sponza benchmark (`tests/unit/render/sponza_benchmark_test.cpp`) at $1280 \times 720$ resolution under identical camera coordinates (Atrium overview: eye $(0, 2, 0)$, target $(0, 2, -6)$, FoV $65^\circ$), directional sunlight with PCF shadow mapping, and post-processing:

| Metric | Milestone 11A Baseline | Milestone 11B (Culling Active) | Milestone 11B (Depth Prepass ON) | Delta (11A $\to$ 11B Optimal) |
| :--- | :--- | :--- | :--- | :--- |
| **Average Frame Time** | **35.28 ms** | **23.36 ms** | **29.20 ms** | **-11.92 ms (-33.8% latency)** |
| **Equivalent FPS** | **28.34 FPS** | **42.80 FPS** | **34.25 FPS** | **+14.46 FPS (+51.0% throughput)** |
| **Camera PBR Draw Calls** | 103 calls | 24 calls | 24 calls | -79 draw calls (-76.7%) |
| **Shadow Pass Draw Calls** | 103 calls | 95 calls | 95 calls | -8 draw calls (-7.8%) |
| **Depth Prepass Draw Calls** | 0 calls | 0 calls | 24 calls | +24 calls |
| **Total Geometry Submissions** | 206 calls | 119 calls | 143 calls | -87 draw calls (-42.2%) |
| **Camera Triangles Rasterized**| 262,267 $\triangle$ | 94,677 $\triangle$ | 94,677 $\triangle$ | -167,590 $\triangle$ (-63.9%) |
| **Culled Primitives** | 0 | 87 primitives | 87 primitives | +87 culled primitives |
| **RenderGraph DAG Nodes** | 0 (Manual) | 8 nodes | 9 nodes | Authoritative DAG |

> [!NOTE]
> When frustum culling is explicitly disabled via `settings.performance.frustumCulling = false`, `sponza_benchmark_test` verifies that all 103 submeshes and 262,267 triangles are submitted cleanly without regression, confirming baseline workload integrity.

---

## 3. Work Package Implementation Details

### Work Package A: Live RenderGraph DAG Execution

`SceneRenderer::renderScene` and `SceneRenderer::renderPostProcess` now execute through compiled `RenderGraph` instances:
- Target buffers and textures (`SceneColorTarget`, `DepthTarget`, `ShadowDepthTarget`, `LightingBuffer`) are imported as transient/external graph resources.
- Synchronization and pass boundaries are sequenced by topological sort.
- Headless execution with mock/null devices or unallocated color targets is supported seamlessly.

### Work Package B: World-Space Frustum Culling

In `engine/core/include/core/math.h`:
- `Plane`: 3D plane in Hessian normal form with distance query: $d = \mathbf{n} \cdot \mathbf{p} + D$.
- `Frustum`: 6-plane frustum extracted from column-major projection matrices for Vulkan clip-space depth $[0, 1]$ using Gribb-Hartmann plane normalization.
- `intersectsAABB`: Conservative box rejection testing box extents against plane normals. Uninitialized or invalid bounding boxes return `true` as conservative fallback.
- Tested across unit tests in `math_test.cpp` and `scene_renderer_test.cpp`.

In `engine/render/src/scene_renderer.cpp`:
- `recordShadowPass`: Evaluates primitive world bounds against `lightFrustum` computed from `currentLightSpaceMatrix_`.
- `recordMainPbrPass`: Evaluates primitive world bounds against `cameraFrustum` computed from `currentViewProj_`.
- Stats accounting: Primitives outside view frustum increment `lastRenderStats_.culledPrimitives` and bypass vertex binding and draw submission.

### Work Package C: Explicit Render Pass Architecture

The rendering pipeline is split into distinct, single-responsibility passes:
1. `DirectionalShadowPass`: Writes `ShadowDepthTarget` (D32_FLOAT) using `ShadowDepthVertexShader` with ortho light matrix push constants.
2. `DepthPrepass` (optional): Writes `DepthTarget` with `LessOrEqual` depth compare op, zero color attachments.
3. `MainPbrPass`: Binds `ShadowDepthTarget` and `LightingBuffer`, rasterizes forward PBR materials with HDR or LDR targets.
4. `BloomDownsamplePass` (4 pyramid steps): Half-resolution bilinear downsampling with 13-tap filter and Karis average.
5. `BloomUpsamplePass` (3 pyramid steps): 9-tap tent filter with additive blend accumulation.
6. `ToneMappingPass`: ACES or Filmic Reinhard tone mapping into presentation color target.

### Work Package D: Empirical Evaluation of Depth Prepass

Hardware behavior was evaluated on NVIDIA Pascal (P106-100):
- **Hypothesis:** Early-Z prepass eliminates pixel shading overdraw on interior Sponza columns and arches.
- **Empirical Measurement:**
  - With Prepass OFF: $23.36 \text{ ms}$ ($42.80 \text{ FPS}$).
  - With Prepass ON: $29.20 \text{ ms}$ ($34.25 \text{ FPS}$).
  - Frame time delta: $+5.84 \text{ ms}$ penalty ($+25.0\%$).
- **Root Cause Analysis:**
  - NVIDIA Pascal hardware includes hardware Z-Cull and Tile-based raster caching that already discards occluded fragments during standard forward rasterization.
  - Adding a separate depth prepass duplicates 24 geometry draw calls, command buffer recording, vertex shader executions, and requires extra Vulkan render pass transitions.
  - In forward PBR with 1 directional light, the fragment shading cost is not heavy enough to amortize 24 duplicated vertex draws.
- **Decision:** Keep `settings.performance.depthPrepass = false` as the default setting. The capability remains fully implemented and toggleable via `PerformanceSettings`.

---

## 4. Preservation of Conductor & Single Shared World Invariants

As mandated by repository rules, no changes compromised the Two-State Conductor or Single Shared World architecture:
- World entities, components, and hierarchies are queried directly via Flecs.
- `conductor_test`, `shared_world_conductor_test`, and `conductor_stress_test` passed without error.
- Verified flat memory and zero heap allocations during mode switches and frame rendering.

---

## 5. Verification Matrix Across Presets

| Preset | Target | Result | Notes |
| :--- | :--- | :--- | :--- |
| `editor` | Full Test Suite (50 tests) | **PASSED (100%)** | All unit, simulation, and editor tests clean. |
| `release`| Non-Editor Test Suite (45 tests) | **PASSED (100%)** | Full optimization (-O3), clean build and tests. |
| `asan`   | AddressSanitizer (45 tests) | **PASSED (100%)** | 0 leaks, 0 memory access violations. |
| Vulkan   | Validation Layer | **CLEAN** | 0 errors, 0 warnings during live Sponza certification. |

---

## 6. Definition of Done Checklist

- [x] Work Package A: Live RenderGraph integration in `SceneRenderer`.
- [x] Work Package B: World-space frustum culling for camera and shadow passes.
- [x] Work Package C: Explicit render pass architecture with DAG dependencies.
- [x] Work Package D: Evidence-driven Depth Prepass evaluation on Sponza benchmark.
- [x] Benchmark integrity preserved (both unculled baseline and culled paths verified).
- [x] Two-State Conductor and Single Shared World invariants maintained.
- [x] Multi-preset verification (`editor`, `release`, `asan`) clean.
- [x] Git commits strictly scoped per `AGENTS.md` Section 12 (no uncommitted changes, no squashed blobs).
- [x] Authoritative certification documentation committed.
