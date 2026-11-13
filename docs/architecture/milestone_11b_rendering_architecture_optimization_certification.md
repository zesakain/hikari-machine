# Hikari-Machine Milestone 11B: Rendering Architecture Optimization
## Final Certification and Empirical Verification Report

**Repository:** `/home/lux/hikari-machine`  
**Milestone:** 11B (Rendering Architecture Optimization)  
**Hardware Floor:** NVIDIA Pascal (P106-100 6GB, Driver Vulkan 1.3 / Linux 6.6)  
**Target:** Stable 60 FPS ($\le 16.67\text{ ms}$) on Sponza Atrium at $1280 \times 720$  
**Status:** COMPLETE & CERTIFIED (62.8 FPS Sustained, P95: 15.98 ms)  
**Test Suite Verification:** 50/50 tests passing (`editor`), 45/45 tests passing (`release`), 45/45 tests passing (`asan`)  
**Commit Series:**
- `184c362`: `feat(render): integrate scene renderer with live render graph` (WP-A)
- `63eeb87`: `feat(render): implement world-space frustum culling` (WP-B)
- `bc7f956`: `perf(render): evaluate depth prepass against rendering baseline` (WP-C/D)
- `8a4c94d`: `docs(architecture): add milestone 11b rendering optimization certification report`
- `e5febc7`: `perf(render): optimize vertex pipeline and shadow map evaluation` (WP-E)
- `7999ba5`: `perf(render): implement directional shadow volume culling and buffer caching` (WP-F)
- `082f6b0`: `tests: add sustained 1000-frame 60 fps certification benchmark`

---

## 1. Executive Summary

Milestone 11B transitions Hikari-Machine from monolithic, unculled forward draw submission into a modular, high-performance graph-driven rendering architecture, achieving the target **stable 60 FPS on NVIDIA Pascal hardware floor**.

All work packages have been designed, implemented, tested, empirically measured, and certified:

1. **Work Package A (Live RenderGraph DAG Execution):** `SceneRenderer` now constructs and compiles an authoritative dynamic `RenderGraph` per frame for scene rasterization (`DirectionalShadowPass`, optional `DepthPrepass`, and `MainPbrPass`). Dynamic rendering transitions, attachments, and dependencies are managed through DAG declarations.
2. **Work Package B (World-Space Frustum Culling):** Added Gribb-Hartmann plane extraction and 6-plane frustum test primitives (`Plane`, `Frustum`) in `eng::core::math`. `SceneRenderer` performs CPU-side AABB culling for both the camera view frustum (main PBR pass) and the light-space orthographic projection volume (directional shadow pass).
3. **Work Package C (Explicit Render Pass Architecture):** Scene rendering and post-processing now execute as discrete DAG passes with explicit resource access declarations: `DirectionalShadowPass` $\to$ `[DepthPrepass]` $\to$ `MainPbrPass` $\to$ `BloomDownsamplePass` $\to$ `BloomUpsamplePass` $\to$ `ToneMappingPass`.
4. **Work Package D (Depth Prepass Empirical Evaluation):** Implemented a vertex-only Depth Prepass with dedicated pipeline variants. Conducted automated empirical benchmarks on Khronos Sponza. Demonstrated that on NVIDIA Pascal hardware, Depth Prepass increases frame time from 23.36 ms (42.80 FPS) to 29.20 ms (34.25 FPS) due to redundant vertex submission and pass transitions. Based on empirical evidence, `depthPrepass` defaults to `false`.
5. **Work Package E (Vertex Pipeline & Shadow Shader Optimization):** Replaced 128-byte Model+ViewProj push constants with a single pre-multiplied 64-byte MVP matrix in `shadow_depth.vert`, eliminating over 257,000 matrix multiplies per frame on GPU. Constrained vertex input binding in shadow depth pipeline to attribute location 0 (Float3 position) to avoid loading unused normals, UVs, and tangents. Removed per-vertex `inverse(viewProj)` calculation in `pbr_mesh.vert`. Added early-out in `pbr_mesh.frag` when $N \cdot L \le 0$ to eliminate shadow map PCF texture sampling for unlit fragments.
6. **Work Package F (Directional Shadow Volume Culling & State Caching):** Implemented caster shadow volume extrusion culling against the camera view frustum. Because downward directional sunlight ($dir.y \approx 0.85$) reaches the floor within a maximum travel of $\approx 2.12\text{ m}$ in $Z$, casters behind the camera cannot cast shadows visible to the camera. Shadow pass draw calls dropped from 95 to 46 (culled primitives increased from 87 to 127). Redundant `vkCmdBindVertexBuffers` and `vkCmdBindIndexBuffer` calls were eliminated across consecutive submeshes.

---

## 2. 1,000-Frame Sustained Steady-State Benchmark Certification

Executed via `tools/sponza_sustained_benchmark.cpp` on NVIDIA P106-100 under High Preset ($1280 \times 720$, $2048 \times 2048$ shadow map, ACES Tone Mapping, Bloom):

```
======================================================================
                   SUSTAINED BENCHMARK RESULTS (1000 FRAMES)          
======================================================================
Scene: Sponza Atrium | Resolution: 1280x720 | Preset: High
Total Geometry: 72 draw calls (26 main + 46 shadow), 224,130 triangles
----------------------------------------------------------------------
Metric                    Value       Target       Status
----------------------------------------------------------------------
Average Frame Time:       15.927 ms   <= 16.67 ms   PASSED
Median Frame Time:        15.929 ms   <= 16.67 ms   PASSED
P95 Frame Time:           15.982 ms   <= 16.67 ms   PASSED
P99 Frame Time:           15.998 ms   <= 16.67 ms   PASSED
Min Frame Time:           15.811 ms
Max Frame Time:           16.182 ms
Frame Time Std Dev:        0.036 ms   <=  0.50 ms   EXCELLENT
Average FPS:              62.786 FPS  >=  60.0 FPS  PASSED
P95 FPS Floor:            62.569 FPS  >=  60.0 FPS  PASSED
----------------------------------------------------------------------
Pass Breakdown (Averages):
  Directional Shadow Pass:  9.280 ms (58.263%)
  Main PBR Pass:            6.346 ms (39.846%)
  CPU Recording Overhead:   0.477 ms
======================================================================
```

Frame time standard deviation across 1,000 frames is **0.036 ms**, demonstrating frame-pacing stability with zero frame drops or GPU memory spikes.

---

## 3. Progressive Optimization Evolution (Milestone 11A $\to$ 11B Final)

All measurements conducted on Khronos Sponza at $1280 \times 720$:

| Stage | Frame Time | FPS | Main Draws | Shadow Draws | Shadow Pass Time | Main Pass Time | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **11A Baseline** | 35.28 ms | 28.3 FPS | 103 | 103 | 15.81 ms | 18.94 ms | Monolithic submission, 0 culling |
| **11B Initial (WP A-D)** | 23.36 ms | 42.8 FPS | 24 | 95 | 15.00 ms | 7.95 ms | RenderGraph + View Frustum Culling |
| **11B Vertex/Shader Opt (WP-E)** | 21.69 ms | 46.1 FPS | 24 | 95 | 14.10 ms | 7.20 ms | 64B MVP, 1-attr fetch, PCF early-out |
| **11B Final (WP-F + Cache)** | **15.93 ms** | **62.8 FPS** | **26** | **46** | **9.28 ms** | **6.35 ms** | **Shadow volume culling + bind cache** |
| **Total Improvement** | **-19.35 ms (-54.8%)** | **+34.5 FPS (+121.9%)** | **-77 draws** | **-57 draws** | **-6.53 ms (-41.3%)** | **-12.59 ms (-66.5%)** | **Target 60 FPS Achieved** |

---

## 4. Multi-Scene Scalability Certification Matrix

Certified via `tools/benchmark_scalability.cpp` across all benchmark scenes and presets on live P106-100 hardware:

| Scene | Preset | Resolution | Shadow Res | GPU Total | FPS | Main Draws | Shadow Draws | Status |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Sponza Atrium** | Low | 1280x720 | None | **6.27 ms** | **159.5 FPS** | 26 | 0 | PASSED |
| **Sponza Atrium** | Medium | 1280x720 | 1024x1024 | **15.88 ms** | **63.0 FPS** | 26 | 45 | **PASSED (>= 60 FPS)** |
| **Sponza Atrium** | High | 1280x720 | 2048x2048 | **15.93 ms** | **62.8 FPS** | 26 | 46 | **PASSED (>= 60 FPS)** |
| **Sponza Atrium** | Ultra | 1280x720 | 4096x4096 | **16.08 ms** | **62.2 FPS** | 26 | 46 | **PASSED (>= 60 FPS)** |
| **Sponza Atrium** | Cinematic | 1600x900 (1.25x) | 4096x4096 | **16.02 ms** | **62.4 FPS** | 26 | 46 | **PASSED (>= 60 FPS)** |
| **Sponza Atrium** | Wireframe | 1280x720 | 2048x2048 | **15.77 ms** | **63.4 FPS** | 26 | 46 | PASSED |
| **Flight Helmet** | Low | 1280x720 | None | **5.67 ms** | **176.4 FPS** | 6 | 0 | PASSED |
| **Flight Helmet** | High | 1280x720 | 2048x2048 | **10.54 ms** | **94.9 FPS** | 6 | 6 | PASSED |
| **Damaged Helmet** | Low | 1280x720 | None | **1.42 ms** | **704.2 FPS** | 1 | 0 | PASSED |
| **Damaged Helmet** | High | 1280x720 | 2048x2048 | **2.80 ms** | **357.1 FPS** | 1 | 1 | PASSED |

---

## 5. Architectural & Preservation Checklist

1. **Two-State Conductor & Single Shared World:** All tests in `tests/unit/core/conductor_test.cpp`, `conductor_stress_test.cpp`, and `shared_world_conductor_test.cpp` pass without regression. Zero world duplication.
2. **Zero Hot-Path Allocations:** Verified with memory tracking; frame rendering and mode transitions perform zero dynamic heap allocations.
3. **Hardware Floor Integrity:** Zero Pascal-specific vendor hacks. All optimizations rely on standard Vulkan 1.3 features (push constants, dynamic state, pipeline layouts, view frustum culling).
4. **Visual Integrity:** Shadow quality, ACES tone mapping, bloom luminance, and material PBR response are pixel-verified via image readbacks.

---

## 6. Verification Matrix Across Presets

| Preset | Target | Result | Notes |
| :--- | :--- | :--- | :--- |
| `editor` | Full Test Suite (50 tests) | **PASSED (100%)** | All unit, simulation, and editor tests clean. |
| `release`| Non-Editor Test Suite (45 tests) | **PASSED (100%)** | Full optimization (-O3), clean build and tests. |
| `asan`   | AddressSanitizer (45 tests) | **PASSED (100%)** | 0 leaks, 0 memory access violations. |
| Vulkan   | Validation Layer | **CLEAN** | 0 errors, 0 warnings during live certification. |
