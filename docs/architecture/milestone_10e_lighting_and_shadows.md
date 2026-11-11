# Milestone 10E: Lighting System, Shadow Mapping & Editor Integrations

## 1. Overview

Milestone 10E introduces real-time multi-light dynamic illumination, directional shadow mapping, secure Content Browser folder management, and external Lua script editor workflow integration to Hikari Engine.

Building upon the glTF 2.0 Cook-Torrance metallic-roughness PBR implementation from Milestone 10D, this milestone establishes:
1. **ECS Light Components:** First-class ECS representations for Directional, Point, and Spot light sources.
2. **GPU Multi-Light Uniform Buffer:** std140-compliant lighting buffer (`GpuLightingData`, 1680 bytes) bound to Descriptor Set 1.
3. **PBR Multi-Light Integration:** Distance-attenuated point lights, angular-attenuated spotlights, and directional sunlight integrated into the Cook-Torrance specular and diffuse BRDF.
4. **Directional Shadow Mapping:** 2048x2048 depth-only shadow pass, light-space view-projection transformation, 3x3 Percentage-Closer Filtering (PCF), and slope-scaled depth bias.
5. **Content Browser Folder Deletion:** Context menu and delete workflows with modal confirmation, recursive deletion validation, and root traversal security.
6. **External Lua Script Editor Integration:** Safe, non-blocking process launching (`posix_spawnp`) for opening scripts in the OS default editor.

---

## 2. ECS Lighting Architecture

Three light components are declared in `engine/render/include/render/light_components.h` and registered in Flecs worlds via `registerRenderComponents(world)`:

### 2.1 DirectionalLightComponent
Models an infinitely distant light source emitting parallel rays (e.g., sunlight):
- `direction`: Normalized world-space emission vector (`Vec3`).
- `color`: Linear RGB color tint (`Vec3`, default: `(1, 1, 1)`).
- `intensity`: Illuminance in lux/arbitrary units (default: `2.5`).
- `castShadows`: Flag indicating whether this light generates shadow maps (default: `true`).
- `shadowBias`: Constant depth bias offset for shadow receiver testing (default: `0.005`).
- `enabled`: Active/inactive toggle (default: `true`).

### 2.2 PointLightComponent
Models an omnidirectional point source radiating equally in all directions:
- `position`: World-space position (`Vec3`).
- `color`: Linear RGB color tint (`Vec3`).
- `intensity`: Luminous power in lumens/arbitrary units (default: `10.0`).
- `range`: Maximum influence radius in meters (default: `10.0`).
- `enabled`: Active/inactive toggle (default: `true`).

### 2.3 SpotLightComponent
Models a conical light emitter radiating from a point along a directional vector:
- `position`: World-space position (`Vec3`).
- `direction`: Normalized emission direction (`Vec3`).
- `color`: Linear RGB color tint (`Vec3`).
- `intensity`: Peak luminous intensity in candela/arbitrary units (default: `15.0`).
- `range`: Maximum influence radius in meters (default: `10.0`).
- `innerConeAngle`: Inner cone angle in radians where intensity is 100% (default: $15^\circ$).
- `outerConeAngle`: Outer cone angle in radians where intensity drops to 0% (default: $25^\circ$).
- `enabled`: Active/inactive toggle (default: `true`).

---

## 3. GPU Lighting Buffer & std140 Memory Layout

Per-frame lighting state is passed to shader pipelines via Descriptor Set 1 (`set=1, binding=0` uniform buffer, `binding=1` combined image sampler).

### 3.1 std140 Struct Layout (`GpuLightingData`)

To prevent struct padding discrepancies between C++ and SPIR-V/GLSL, `GpuLightingData` is defined with explicit 16-byte alignment and padding:

| Field | Type | Offset (Bytes) | Size (Bytes) |
|---|---|---|---|
| `lightSpaceViewProj` | `Mat4` (16-byte aligned columns) | 0 | 64 |
| `sun` | `GpuDirectionalLight` (dir + intensity, color + castShadows) | 64 | 32 |
| `pointLights[16]` | `GpuPointLight[16]` (pos + range, color + intensity) | 96 | 512 |
| `spotLights[16]` | `GpuSpotLight[16]` (pos + range, dir + inner, color + outer, intensity + pad) | 608 | 1024 |
| `cameraPosAndAmbientIntensity` | `Vec4` (`xyz` = camera pos, `w` = ambient intensity) | 1632 | 16 |
| `ambientColorAndShadowBias` | `Vec4` (`xyz` = ambient color, `w` = shadow bias) | 1648 | 16 |
| `lightCounts` | `uint32_t[4]` (`x` = point count, `y` = spot count, `z,w` = pad) | 1664 | 16 |
| **Total** | | | **1680** |

Compile-time verification is enforced via `static_assert(sizeof(GpuLightingData) == 1680)`.

---

## 4. Multi-Light PBR Formulation

The Cook-Torrance BRDF evaluates direct lighting contributions across all active lights.

### 4.1 Directional Light
Directional sunlight radiance is uniform across the entire scene, attenuated solely by the directional shadow factor:

$$L_{\text{dir}} = \text{sunColor} \cdot \text{sunIntensity} \cdot (1.0 - \text{shadow})$$

### 4.2 Point Light Distance Attenuation
Point lights use physical inverse-square falloff combined with a smooth windowing function to ensure light intensity smoothly terminates at `range`:

$$d = \|p_{\text{light}} - p_{\text{surface}}\|$$

$$\text{att}_{\text{dist}} = \frac{1}{\max(d^2, 0.0001)}$$

$$\text{att}_{\text{window}} = \text{saturate}\left(1.0 - \left(\frac{d}{\text{range}}\right)^4\right)^2$$

$$L_{\text{point}} = \text{color} \cdot \text{intensity} \cdot \text{att}_{\text{dist}} \cdot \text{att}_{\text{window}}$$

### 4.3 Spotlight Angular Falloff
Spotlights multiply distance attenuation with angular cone attenuation:

$$\theta = \max\left(\mathbf{l} \cdot (-\mathbf{d}_{\text{spot}}), 0.0\right)$$

$$\text{falloff}_{\text{spot}} = \text{smoothstep}\left(\cos(\text{outerAngle}), \cos(\text{innerAngle}), \theta\right)$$

$$L_{\text{spot}} = \text{color} \cdot \text{intensity} \cdot \text{att}_{\text{dist}} \cdot \text{att}_{\text{window}} \cdot \text{falloff}_{\text{spot}}$$

### 4.4 Shading Loop
For each light, the half-vector $\mathbf{h} = \text{normalize}(\mathbf{v} + \mathbf{l})$ is computed, and Cook-Torrance specular $(D \cdot V \cdot F)$ and energy-conserving diffuse $((1 - F)(1 - \text{metallic}) \frac{\text{baseColor}}{\pi})$ are accumulated:

$$\text{Color} = \mathbf{L}_{\text{ambient}} \cdot \text{AO} + \sum_{i=1}^{N} \left(f_{\text{diffuse}, i} + f_{\text{specular}, i}\right) L_i (\mathbf{n} \cdot \mathbf{l}_i) + \text{Emissive}$$

---

## 5. Directional Shadow Mapping Pipeline

### 5.1 Light-Space Orthographic Projection
The light-space view-projection matrix bounds the visible scene in light coordinates:
- Light direction: $\mathbf{l}_{\text{sun}}$
- Light view matrix: $\text{lookAt}(\mathbf{p}_{\text{center}} + \mathbf{l}_{\text{sun}} \cdot 50.0, \mathbf{p}_{\text{center}}, \mathbf{up})$
- Orthographic projection: `Mat4::orthographic(-extent, extent, -extent, extent, nearZ, farZ)`

### 5.2 Depth Pre-Pass Pipeline
`SceneRenderer` executes a dedicated depth-only pass into a $2048 \times 2048$ `D32_FLOAT` render target before the main color pass:
- Pipeline: `shadow_depth.vert` vertex shader outputting `gl_Position = lightSpaceViewProj * model * vec4(inPos, 1.0)`. No fragment shader attached.
- Attachment: `depthAttachment` with `AttachmentLoadOp::Clear` (depth cleared to `1.0`), color attachment count 0.
- State: Depth test and write enabled, front-face culling enabled to eliminate self-shadowing acne on backfaces.

### 5.3 Vulkan & Null Dynamic Rendering Support
`VulkanCommandList` and `NullCommandList` were enhanced to support depth-only render passes:
- When `colorTarget` is invalid (`TextureHandle::null()`), dynamic rendering sets `colorAttachmentCount = 0` and `pColorAttachments = nullptr`.
- Upon `endRenderPass()`, the depth target transitions from `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` to `DEPTH_STENCIL_READ_ONLY_OPTIMAL` for shader texture sampling.

### 5.4 Receiver Shadow Sampling & PCF Filtering
In the PBR fragment shader (`pbr_mesh.frag`):
1. **Light Space Transformation:** $P_{\text{clip}} = \mathbf{M}_{\text{lightSpace}} \cdot P_{\text{world}}$.
2. **Perspective Division & UV Remapping:**
   $$uv_{\text{shadow}} = P_{\text{clip}}.xy \cdot 0.5 + 0.5$$
   $$z_{\text{current}} = P_{\text{clip}}.z$$
3. **Slope-Scaled Depth Bias:**
   $$\text{bias} = \max(\text{shadowBias} \cdot (1.0 - \mathbf{n} \cdot \mathbf{l}), \text{shadowBias} \cdot 0.2)$$
4. **3x3 Percentage-Closer Filtering (PCF):** Samples 9 texels around $uv_{\text{shadow}}$ to produce soft shadow penumbras and antialiased shadow edges:
   $$\text{shadow} = \frac{1}{9} \sum_{x=-1}^{1} \sum_{y=-1}^{1} \left(z_{\text{current}} - \text{bias} > z_{\text{sample}}(uv + (x, y) \cdot \Delta_{\text{texel}}) ? 1.0 : 0.0\right)$$

---

## 6. Content Browser Folder Deletion Safety

`ContentBrowserPanel` and `ContentBrowserController` implement safe directory removal workflows:

### 6.1 Modal Confirmation Dialog
- Deletion requests from the folder tree context menu or action buttons trigger a modal confirmation popup (`Confirm Folder Deletion`).
- Displays the folder name and warns of permanent deletion of all nested assets.
- Requires explicit user confirmation via the `Delete` button.

### 6.2 Security & Recursive Safety
- **Empty vs Non-Empty:** Non-empty folders require recursive deletion.
- **Root Protection:** Deleting the root asset directory is forbidden and rejected.
- **Path Traversal / Symlink Protection:** Canonical path resolution ensures deleted paths are strict descendants of `rootDirectory_`. Attempts to escape the asset root via `..` or symlinks are rejected.
- **Active Directory Retreat:** If the currently open directory (or an ancestor) is deleted, `ContentBrowserController` automatically retreats navigation to the parent or root folder to prevent dangling directory state.

---

## 7. External Lua Script Editor Integration

`ExternalEditorLauncher` (`editor/include/editor/external_editor.h`, `editor/src/external_editor.cpp`) enables opening `.lua` scripts directly from the editor:

### 7.1 Cross-Platform Editor Spawning
- **Linux:** `xdg-open`
- **macOS:** `open`
- **Windows:** `ShellExecuteW`

### 7.2 Security & Process Execution
- Executed via `posix_spawnp` (POSIX) or direct API calls without invoking a command shell (`/bin/sh` or `cmd.exe`).
- Arguments are passed as discrete string arrays, completely mitigating shell injection vulnerabilities.
- Non-blocking execution prevents UI stalls or freezes while the external application launches.

### 7.3 Content Browser UI Integration
- **Context Menu:** Right-clicking any `.lua` file displays `Open in External Editor`.
- **Double-Click:** Double-clicking a `.lua` file in either Grid or List view automatically launches the external editor.

---

## 8. Verification & Test Suite

### 8.1 Automated Test Execution

| Test Executable | Target Area | Verified Behaviors |
|---|---|---|
| `lighting_render_test` | Lighting & Shadows | Directional intensity scaling, point light inverse-square falloff, spotlight cone falloff, directional shadow occlusion, offscreen PPM capture. |
| `content_browser_test` | Folder Management | Empty vs non-empty deletion, root protection, path traversal rejection, active folder retreat. |
| `script_editor_test` | External Editor | Executable detection, file existence validation, non-blocking process launching. |
| `pbr_render_test` | PBR BRDF & Materials | Full glTF 2.0 Cook-Torrance model regression validation across roughness, metallic, normal mapping, emissive, and alpha mask. |

### 8.2 Preset Test Results

- **Debug Preset (`cmake --preset debug`):** 41/41 tests passed (100%).
- **Release Preset (`cmake --preset release`):** 41/41 tests passed (100%).
- **Editor Preset (`cmake --preset editor`):** 44/44 tests passed (100%).
- **AddressSanitizer Preset (`cmake --preset asan`):** 41/41 tests passed (100%, 0 leaks, 0 errors).
- **Visual Capture Output:** `test-assets/lighting_shadow_capture.ppm` generated and visually confirmed.
