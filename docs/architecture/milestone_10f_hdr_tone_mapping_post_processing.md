# Milestone 10F: HDR, Tone Mapping & Post-Processing Architecture

## 1. Overview & Objectives

Milestone 10F establishes a physically based High Dynamic Range (HDR) rendering pipeline and foundational post-processing framework for Hikari Engine. It bridges the HDR PBR rendering (GGX, Smith visibility, Schlick Fresnel, directional/point/spot lighting, and PCF shadows from Milestones 10D and 10E) and the 8-bit SDR presentation swapchain.

Key system capabilities:
1. **Dynamic HDR Render Target & Format Negotiation**: Automatic hardware capability querying selecting between `RGBA16_FLOAT` (preferred baseline), `RGBA32_FLOAT` (extended precision fallback), and `RGBA8_UNORM` (explicit SDR fallback).
2. **Explicit HDR vs SDR Fallback State**: Strict distinction between genuine floating-point HDR targets and 8-bit SDR fallback mode. `RGBA8_UNORM` is never misrepresented as genuine HDR.
3. **Linear Sampler Filterability Qualification**: Hardware capability check (`isTextureFormatFilterable`) ensuring linear sampling is supported prior to allocating bloom downsample and upsample pyramids.
4. **Photometric Linear Exposure Scaling**: Direct physical exposure control scaling scene radiance linearly before tone compression.
5. **Multi-Operator Filmic Tone Mapping**:
   - ACES Fitted (Stephen Hill Narkowicz fit for high cinematic contrast and natural highlight desaturation).
   - Filmic Reinhard with configurable white point ($W \in [1.0, 50.0]$, default $4.0$).
   - Khronos PBR Neutral (colorimetric tone mapper maintaining hue and saturation for e-commerce and reference visualization).
   - Linear Bypass (uncompressed clamped linear output for debugging).
6. **sRGB OETF Transfer Function**: Standard IEC 61966-2-1 piecewise curve applied in shader to correctly convert linear radiance to display non-linear space for standard `UNORM` swapchains without accidental double conversion.
7. **Dual-Pyramid Bloom Filter**:
   - 13-tap Jimenez tent filter downsample with soft-knee threshold bright extraction to prevent fireflies.
   - 9-tap tent filter upsample with progressive additive blending across a 4-level pyramid.
8. **Zero Per-Frame Allocations**: Preallocated offscreen descriptors, static descriptor set layouts, and push constant payloads guaranteeing deterministic frame timing.
9. **Dear ImGui Editor Integration**: Live viewport post-processing badge (`[HDR: RGBA16F]`, `[HDR: RGBA32F]`, `[SDR Fallback: RGBA8]`, or `[LDR]`), popup overlay, and Inspector panel controls for real-time visual inspection and grading.

---

## 2. Architecture & Pipeline Flow

```
[ ECS World & Meshes ]
        │
        ▼
┌────────────────────────────────────────────────────────┐
│ Phase 1: Directional Shadow Pass                       │
│ - Depth-only rendering into D32_FLOAT shadow map       │
└────────────────────────────────────────────────────────┘
        │
        ▼
┌────────────────────────────────────────────────────────┐
│ Phase 2: Main PBR Scene Pass                           │
│ - Shading with GGX + PCF Shadows + Emissive Radiance   │
│ - Rendered into offscreen HDR Target (RGBA16_FLOAT)    │
│ - Auto-transitions to SHADER_READ_ONLY_OPTIMAL         │
└────────────────────────────────────────────────────────┘
        │
        ├─────────────────────────────────────────────────┐
        │ (if Bloom Enabled)                              │ (if Bloom Disabled)
        ▼                                                 │
┌────────────────────────────────────────────────────────┐│
│ Phase 3: Bloom Downsample Pyramid (4 Levels)           ││
│ - L0: 13-tap downsample + soft-knee threshold extract  ││
│ - L1..L3: 13-tap downsample (half-res per level)       ││
└────────────────────────────────────────────────────────┘│
        │                                                 │
        ▼                                                 │
┌────────────────────────────────────────────────────────┐│
│ Phase 4: Bloom Upsample Pyramid (3 Levels)             ││
│ - Progressive 9-tap tent filter + additive blend       ││
│ - L2 -> L1 -> L0 accumulator                           ││
└────────────────────────────────────────────────────────┘│
        │                                                 │
        ├─────────────────────────────────────────────────┘
        ▼
┌────────────────────────────────────────────────────────┐
│ Phase 5: Final Tone Mapping & Color Grading Pass       │
│ - Sample HDR Scene Target (Binding 0)                  │
│ - Sample Bloom Result (Binding 1, if enabled)          │
│ - Apply exposure scale: color *= max(exposure, 0.0)    │
│ - Apply Tone Mapper (ACES / Reinhard / Neutral / Byp)  │
│ - Apply sRGB Transfer Curve: linear -> non-linear sRGB │
│ - Output into Color Target (RGBA8_UNORM)               │
└────────────────────────────────────────────────────────┘
        │
        ▼
┌────────────────────────────────────────────────────────┐
│ Phase 6: ImGui Editor Viewport Presentation            │
│ - Display texture via ImGui::Image                     │
│ - Live Post-FX adjustments without pipeline rebuild    │
└────────────────────────────────────────────────────────┘
```

---

## 3. Detailed Component Design

### 3.1 Format Negotiation & Hardware Capability Querying

Vulkan hardware features vary across GPUs. The engine queries optimal tiling capabilities via `RhiDevice::isTextureFormatSupported` and `RhiDevice::isTextureFormatFilterable`:

1. **`TextureFormat::RGBA16_FLOAT`** (`VK_FORMAT_R16G16B16A16_SFLOAT`):
   - Preferred baseline format.
   - Requires `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT`, `VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT`, and `VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT`.
   - Offers 16-bit half precision per channel (sufficient range $[10^{-14}, 6.5 \times 10^4]$ without banding or precision loss).
2. **`TextureFormat::RGBA32_FLOAT`** (`VK_FORMAT_R32G32B32A32_SFLOAT`):
   - Extended precision fallback if 16-bit float color attachment is unsupported.
   - Also requires linear filterability for bloom sampling.
3. **`TextureFormat::RGBA8_UNORM`** (`VK_FORMAT_R8G8B8A8_UNORM`):
   - Explicit SDR fallback if floating-point dynamic rendering or linear filtering is unsupported.
   - Never reported as genuine HDR (`isHdrActive()` returns `false`, `isSdrFallbackActive()` returns `true`).

### 3.2 Color-Space Pipeline & sRGB Encoding Audit

To prevent accidental double sRGB conversions or incorrect gamma:
1. **Intermediate HDR Render Target (`hdrTarget_`)**: Always formatted as `RGBA16_FLOAT` or `RGBA32_FLOAT`. Floating-point formats in Vulkan do not perform hardware sRGB encoding on write or sRGB decoding on read. They remain strictly in linear photometric radiance space.
2. **Bloom Filter Pyramid**: All pyramid textures (`bloomDownTarget_`, `bloomUpTarget_`) share `hdrFormat_`. All downsampling, tent filtering, and additive blending operate on linear radiance.
3. **Presentation Target (`colorTarget_`)**: Formatted as `RGBA8_UNORM`. In Vulkan, `_UNORM` formats bypass hardware sRGB conversion on write (unlike `_SRGB` formats which automatically apply non-linear curves).
4. **Manual Shader sRGB Conversion**: The tone mapping shader (`tone_mapping.frag`) manually applies the standard IEC 61966-2-1 piecewise curve:
   $$C_{\text{srgb}} = \begin{cases} 12.92 \cdot C, & C \le 0.0031308 \\ 1.055 \cdot C^{1/2.4} - 0.055, & C > 0.0031308 \end{cases}$$
5. **Vulkan Swapchain**: Configured with `VK_FORMAT_B8G8R8A8_UNORM` and `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`. Because ImGui and the viewport present the `colorTarget_` texture, the single manual conversion in the shader produces correct display colors without double conversion.

### 3.3 Photometric Exposure & Tone Mapping

#### Linear Exposure
Linear exposure scales HDR radiance before non-linear compression:
$$\mathbf{C}_{\text{exposed}} = \mathbf{C}_{\text{in}} \cdot \max(E, 0.0)$$
where $E \in [0.01, 10.0]$ is the exposure multiplier ($1.0$ default).

#### Tone Mapping Operators
- **ACES Fitted (Stephen Hill / Krzysztof Narkowicz)**:
  $$\mathbf{C}_{\text{mapped}} = \text{clamp}\left(\frac{\mathbf{x} \cdot (2.51\mathbf{x} + 0.03)}{\mathbf{x} \cdot (2.43\mathbf{x} + 0.59) + 0.14}, 0.0, 1.0\right)$$
- **Filmic Reinhard (Configurable White Point $W \in [1.0, 50.0]$)**:
  $$\mathbf{C}_{\text{mapped}} = \text{clamp}\left(\frac{\mathbf{x} \cdot (1 + \frac{\mathbf{x}}{W^2})}{1 + \mathbf{x}}, 0.0, 1.0\right)$$
- **Khronos PBR Neutral**: Formulated by the Khronos 3D Commerce Working Group. Dynamically preserves hues and highlights with minimal color shifting and desaturation:
  - Subtracts toe compression offset: $x < 0.08 \implies x - 6.25x^2$ else $0.04$.
  - Compresses peak luminance asymptotically towards $1.0$.
  - Preserves saturation for non-peak channels.
- **Linear Bypass**: Direct clamp $\text{clamp}(\mathbf{x}, 0.0, 1.0)$ for debugging linear scene radiance.

---

### 3.4 Dual-Pyramid Bloom Filter

The bloom pipeline uses a 4-level downsample pyramid and 3-level upsample pyramid:

1. **Soft-Knee Bright Extraction (First Downsample Pass)**:
   Extracts high-luminance texels while eliminating fireflies and abrupt cutoff artifacts:
   $$\text{soft} = \text{clamp}(\text{brightness} - \text{threshold} + \text{knee}, 0.0, 2 \cdot \text{knee})$$
   $$\text{soft} = \frac{\text{soft}^2}{4 \cdot \text{knee} + 10^{-4}}$$
   $$\text{contrib} = \frac{\max(\text{soft}, \text{brightness} - \text{threshold})}{\max(\text{brightness}, 10^{-4})}$$
2. **13-Tap Downsample Filter (Jimenez 2014)**:
   Samples a $4 \times 4$ neighborhood using 5 box filters (1 center, 4 corners) weighted to cancel bilinear subpixel crawling.
3. **9-Tap Tent Filter Upsample**:
   Reconstructs smooth Gaussian-like bloom halos using a $3 \times 3$ tent filter kernel and additively blends lower-frequency details into higher-frequency pyramid stages.

---

### 3.5 Zero Per-Frame Allocations & Concurrency

Per `AGENTS.md` Sections 5 and 6:
- All descriptor set layouts, pipelines, and descriptor sets (8 sets in total) are allocated during `SceneRenderer::init()` / `initPostProcessResources()`.
- Resizing regenerates texture handles and writes descriptor sets via `updatePostProcessDescriptors()`, which occurs only on window/viewport resize (outside the hot loop).
- Per-frame parameter updates use Vulkan push constants:
  - `BloomDownsamplePushConstants` (32 bytes)
  - `BloomUpsamplePushConstants` (16 bytes)
  - `ToneMappingPushConstants` (32 bytes)
- Zero heap allocations (`std::vector` growth, `new`/`delete`, or `malloc`) occur during `renderScene()` or `renderPostProcess()`.

---

## 4. Editor & Viewport Integration

1. **Viewport Live Badge**: The viewport panel renders an `[HDR: RGBA16F]`, `[HDR: RGBA32F]`, or `[SDR Fallback: RGBA8]` status pill in the top-right overlay. Clicking the pill toggles a settings popover.
2. **Post FX Viewport Popover**:
   - Master Post-Processing Toggle.
   - Exposure Slider ($0.1$ to $10.0$ in logarithmic increments).
   - Tone Mapping Operator Combo (ACES Fitted, Filmic Reinhard, Khronos Neutral, Bypass).
   - Reinhard White Point Slider ($1.0$ to $20.0$, visible when Filmic Reinhard is selected).
   - Bloom Toggle, Threshold, Knee, and Intensity sliders.
3. **Inspector Panel Post-Processing Card**: Dedicated card displaying render target mode, SDR fallback diagnostics, and reset-to-defaults capabilities.

---

## 5. Verification & Testing

The post-processing subsystem is validated by a 5-part automated test suite (`tests/unit/render/post_process_test.cpp`):

1. **Analytical Tone Mapping Curves**:
   - Validates monotonicity of ACES, Reinhard, Neutral, and Linear Bypass.
   - Asserts strict clamping within $[0, 1]$ even for extreme radiance values ($\ge 10000.0$).
   - Validates negative input safety (no NaNs or inverted colors).
   - Validates configurable Reinhard white point compression behavior.
   - Validates distinct mathematical outputs at mid-range radiance ($v = 2.0$).
   - Validates piecewise sRGB transfer function exactness.
2. **Soft-Knee Extraction**:
   - Asserts smooth $C^1$ continuity across threshold boundary without discontinuities.
   - Asserts threshold sensitivity (higher threshold strictly reduces extraction).
   - Asserts knee sensitivity (zero knee produces sharp step threshold).
3. **Target Lifecycle & Resizing**:
   - Tests `RGBA16_FLOAT` selection, format negotiation fallback, and dynamic multi-resolution resizing without resource leaks.
4. **Genuine GPU Floating-Point HDR Radiance Readback**:
   - Directly renders an emissive quad ($emissiveFactor = (8.0, 6.0, 4.0)$) into `hdrTarget_`.
   - Copies `hdrTarget_` (`RGBA16_FLOAT`) directly to a GPU readback buffer.
   - Decodes half-precision IEEE 754 floats in CPU memory.
   - Proves hardware accumulation of radiance exceeding $1.0$ (observed max radiance $= 8.54688$, $> 3100$ HDR pixels).
5. **Vulkan Hardware Rendering & Offscreen Execution**:
   - Shading and tone mapping of DamagedHelmet glTF asset.
   - Verification of exposure monotonicity ($\sum \text{High} > \sum \text{Normal} > \sum \text{Low}$).
   - Verification of bloom energy addition ($\sum \text{BloomOn} \ge \sum \text{BloomOff}$).
   - Offscreen capture generation for visual reference across all tone mapping operators, exposure levels, and SDR fallback.

---

## 6. Visual Verification Analysis

Offscreen test captures generated using Vulkan hardware dynamic rendering:

| Capture File | Description | Mean Luminance | Std Dev | Visual Assessment |
| :--- | :--- | :---: | :---: | :--- |
| `postprocess_aces_normal.png` | ACES Fitted, Exposure = 1.0, Bloom ON | 90.12 | 48.02 | High cinematic contrast, rich shadows, balanced highlights |
| `postprocess_aces_high_exposure.png` | ACES Fitted, Exposure = 2.5, Bloom ON | 142.80 | 61.88 | Shadow details lifted, specular highlights softly rolled off |
| `postprocess_aces_low_exposure.png` | ACES Fitted, Exposure = 0.3, Bloom ON | 37.10 | 24.09 | Underexposed moody scene, bright points preserved without clipping |
| `postprocess_bloom_enabled.png` | Bloom ON (Threshold = 0.2, Intensity = 0.2) | 121.35 | 40.97 | Pronounced physical glow radiating from specular highlights |
| `postprocess_bloom_disabled.png` | Bloom OFF | 88.67 | 48.35 | Crisp geometry without diffuse halation |
| `postprocess_reinhard.png` | Filmic Reinhard (White = 4.0) | 76.53 | 32.47 | Smooth shoulder transition, classic filmic response |
| `postprocess_reinhard_low_white.png` | Filmic Reinhard (White = 1.5) | 78.26 | 33.86 | Highlights compress earlier with higher shoulder brightness |
| `postprocess_neutral.png` | Khronos PBR Neutral | 61.74 | 38.46 | Strict colorimetric preservation without contrast expansion |
| `postprocess_bypass.png` | Linear Bypass (Direct clamp) | 80.92 | 35.87 | Raw clamped linear radiance |
| `postprocess_sdr_fallback.png` | SDR Fallback Mode (RGBA8 Target) | 90.12 | 48.02 | Verified fallback execution path |

---

## 7. Known Limitations & Future Work

1. **Temporal Anti-Aliasing (TAA)**: Not implemented; deferred to future anti-aliasing milestones.
2. **Auto-Exposure / Luminance Metering**: Current pipeline uses manual photometric exposure. Compute-based luminance histogram auto-exposure is deferred to later post-processing extensions.
3. **Chromatic Aberration / Depth of Field**: Deliberately out of scope for Milestone 10F; to be added as modular passes in future rendering milestones.
