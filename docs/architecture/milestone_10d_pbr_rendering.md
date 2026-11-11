# Milestone 10D: Full glTF Metallic-Roughness Physically Based Rendering (PBR)

## 1. Overview

Milestone 10D establishes technically correct, glTF 2.0-compliant Physically Based Rendering (PBR) in Hikari Engine. Building upon the resource descriptor binding of Milestone 10A, the texture streaming and mipmapping pipeline of Milestone 10B, and the glTF material representation of Milestone 10C, this milestone implements the standard Cook-Torrance microfacet specular BRDF and energy-conserving diffuse reflection model.

**Scope Boundary:** Milestone 10D addresses physical material evaluation, microfacet specular reflection, diffuse energy conservation, tangent-space normal mapping, and glTF texture conventions. Dynamic multi-light ECS infrastructure, shadow mapping, and image-based lighting (IBL) cubemaps are scheduled for Milestone 10E.

---

## 2. Microfacet BRDF Formulation

The engine implements the bidirectional reflectance distribution function (BRDF) specified by the glTF 2.0 specification:

$$f(l, v) = f_{\text{diffuse}}(l, v) + f_{\text{specular}}(l, v)$$

### 2.1 Specular BRDF (Cook-Torrance)

$$f_{\text{specular}}(l, v) = \frac{D(h) \cdot F(v, h) \cdot G(l, v)}{4 (n \cdot l)(n \cdot v)} = D(h) \cdot F(v, h) \cdot V(l, v)$$

where:
- $n$ is the surface normal (perturbed by normal mapping if present).
- $v$ is the view direction from surface to camera.
- $l$ is the incoming light direction.
- $h = \text{normalize}(v + l)$ is the microfacet half-vector.

#### 1. Normal Distribution Function $D(h)$ (GGX / Trowbridge-Reitz)
Models the statistical distribution of microfacet orientations:

$$D(h) = \frac{\alpha^2}{\pi \left((n \cdot h)^2 (\alpha^2 - 1) + 1\right)^2}$$

where $\alpha = \text{roughness}^2$. Roughness is clamped to $[0.045, 1.0]$ to eliminate numerical singularities and division-by-zero at specular peaks.

#### 2. Correlated Visibility Function $V(l, v)$ (Heitz / Smith Joint)
The combined shadowing-masking geometry term accounts for microfacet self-shadowing. The engine implements the Smith joint GGX correlated visibility function, which merges the geometric attenuation $G(l, v)$ and the microfacet denominator $4 (n \cdot l)(n \cdot v)$ into a single numerically stable term:

$$V(l, v) = \frac{0.5}{(n \cdot l) \sqrt{(n \cdot v)^2 (1 - \alpha^2) + \alpha^2} + (n \cdot v) \sqrt{(n \cdot l)^2 (1 - \alpha^2) + \alpha^2}}$$

#### 3. Fresnel Reflectance $F(v, h)$ (Schlick Approximation)
Determines the fraction of reflected light across varying angles of incidence:

$$F(v, h) = F_0 + (1 - F_0)(1 - (v \cdot h))^5$$

where $F_0$ is the specular reflectance at normal incidence ($\theta = 0^\circ$).

### 2.2 Energy-Conserving Diffuse Reflection

Light penetrating the surface undergoes subsurface scattering before emerging as diffuse reflection. To satisfy conservation of energy:

$$\text{Reflected} + \text{Refracted} \le 1$$

The specular Fresnel reflectance $F$ accounts for the reflected energy. The refracted energy available for diffuse scattering is:

$$k_d = (1 - F) \cdot (1 - \text{metallic})$$

- **Dielectrics ($\text{metallic} = 0$):** Use a physically accurate baseline reflectance $F_0 = 0.04$ (~4% reflectance at normal incidence, characteristic of non-conductive plastics, glass, wood, stone). The remaining $96\%$ refracts and emerges as Lambertian diffuse:
  $$f_{\text{diffuse}} = k_d \frac{\text{baseColor}}{\pi}$$
- **Metals ($\text{metallic} = 1$):** Conductors absorb all refracted light via free electron excitation; diffuse reflection is completely suppressed ($k_d = 0$). Metals exhibit colored specular reflection determined by $F_0 = \text{baseColor}$.
- **Transition:** Linearly blended via $\text{mix}(\text{vec3}(0.04), \text{baseColor}, \text{metallic})$.

---

## 3. glTF Texture Channels & Conventions

All five material texture slots bound to Descriptor Set 0 are sampled and evaluated according to glTF 2.0 specifications:

| Binding | Semantic | glTF Color Space | Format | Channel Mapping / Evaluation |
|---|---|---|---|---|
| `set=0, binding=0` | Base Color | sRGB | `RGBA8_SRGB` | RGB = diffuse/base color, A = alpha |
| `set=0, binding=1` | Metallic-Roughness | Linear | `RGBA8_UNORM` | **G = Roughness**, **B = Metallic**, R/A unused |
| `set=0, binding=2` | Normal Map | Linear | `RGBA8_UNORM` | RGB = Tangent-space normal vector $[0, 1] \to [-1, 1]$ |
| `set=0, binding=3` | Ambient Occlusion | Linear | `RGBA8_UNORM` | **R = Occlusion**, G/B/A unused |
| `set=0, binding=4` | Emissive Map | sRGB | `RGBA8_SRGB` | RGB = Emissive radiance |

### Numeric Clamping, Normalization & Occlusion
- `metallic`: clamped to $[0.0, 1.0]$.
- `roughness`: clamped to $[0.045, 1.0]$.
- `normalScale`: scales the $X$ and $Y$ tangent components before renormalization:
  $$n_{\text{perturbed}} = \text{normalize}\left(\mathbf{TBN} \cdot \left[s_x \cdot \text{scale}, s_y \cdot \text{scale}, s_z\right]^T\right)$$
- `occlusionStrength`: modulates ambient/indirect lighting attenuation only. In accordance with the glTF 2.0 specification, ambient occlusion never attenuates direct illumination:
  $$\text{ao} = 1.0 + \text{occlusionStrength} \cdot (\text{sample.r} - 1.0)$$
  $$\text{ambient} = (\text{ambientDiffuse} + \text{ambientSpecular}) \cdot \text{ao}$$
- **Texture Coordinates:** Multiple UV coordinate sets (`texCoordIndex > 0`) are currently not supported; all texture lookups evaluate the primary `TEXCOORD_0` attribute stream. Unsupported glTF extensions (e.g. KHR_texture_transform) are parsed but ignored.

---

## 4. Tangent-Space Normal Mapping & TBN Basis

### 4.1 Vertex Stream Tangent Attribute
The engine's `Vertex` structure (`engine/render/include/render/mesh.h`) includes a 4-component tangent vector:

```cpp
struct Vertex {
    Vec3 position;   // offset 0..11  (12B)
    Vec3 normal;     // offset 12..23 (12B)
    Vec2 uv;         // offset 24..31 (8B)
    Vec4 color;      // offset 32..47 (16B)
    Vec4 tangent;    // offset 48..63 (16B)
}; // sizeof(Vertex) == 64 bytes (cache-line aligned)
```

The 4th component $w \in \{-1.0, +1.0\}$ encodes the sign/handedness of the bitangent vector:

$$\mathbf{B} = (\mathbf{N} \times \mathbf{T}) \cdot w$$

### 4.2 glTF Tangent Import & Auto-Generation
1. **glTF Tangents:** `GltfImporter` and `GltfSceneImporter` ingest existing tangent vectors (`cgltf_attribute_type_tangent`), applying the 3x3 normal matrix to $(T_x, T_y, T_z)$ and preserving handedness $w$.
2. **Auto-Generation:** If a glTF primitive lacks tangent attributes, `MeshData::generateTangents()` calculates tangent vectors using UV coordinate derivatives across triangle faces:
   $$\mathbf{T} = \frac{\Delta v_2 \mathbf{E}_1 - \Delta v_1 \mathbf{E}_2}{\Delta u_1 \Delta v_2 - \Delta u_2 \Delta v_1}$$
   followed by Gram-Schmidt re-orthogonalization against vertex normals and handedness calculation:
   $$w = \text{sgn}\left((\mathbf{N} \times \mathbf{T}) \cdot \mathbf{B}_{\text{raw}}\right)$$

### 4.3 Shader TBN Reconstruction
In `assets/shaders/pbr_mesh.frag`:
```glsl
vec3 N = normalize(inNormal);
vec3 T = normalize(inTangent.xyz);
// Gram-Schmidt re-orthogonalization against interpolated normal
T = normalize(T - dot(T, N) * N);
vec3 B = cross(N, T) * inTangent.w;
mat3 TBN = mat3(T, B, N);

if (material.hasNormalTex != 0u) {
    vec3 normalSample = texture(uNormalMap, inUV).xyz * 2.0 - 1.0;
    normalSample.xy *= material.normalScale;
    N = normalize(TBN * normalSample);
}
```

---

## 5. Alpha Behavior & Emissive Radiance

- **Alpha Modes:**
  - `AlphaMode::Opaque` (default): Surface is rendered completely opaque; alpha channel is ignored during depth writing and rasterization.
  - `AlphaMode::Mask`: Fragments with `baseColor.a < material.alphaCutoff` are discarded immediately in the fragment shader before lighting calculations (`discard`).
  - `AlphaMode::Blend`: **UNSUPPORTED** in Milestone 10D. Rasterization pipelines currently use opaque depth writing and blending is disabled (`blendEnable = false`). Transparent object sorting, depth buffer handling, and alpha blending are scheduled for future rendering milestones.
- **Emissive Radiance:** Multiplied by `emissiveFactor` and added directly to the illuminated output without diffuse/specular attenuation:
  $$\text{Color}_{\text{final}} = \text{directLight} + \text{ambient} + \text{emissive}$$

---

## 6. Verification & Automated Test Suite

Milestone 10D includes a comprehensive verification suite (`tests/unit/render/pbr_render_test.cpp`) running against the Vulkan RHI device:

1. **`testDielectricRoughnessVariations`:** Compares smooth dielectric ($\text{roughness} = 0.05$) against rough dielectric ($\text{roughness} = 0.95$), verifying microfacet specular concentration.
2. **`testMetallicRoughnessVariations`:** Compares smooth metal ($\text{metallic} = 1.0, \text{roughness} = 0.05$) against rough metal ($\text{roughness} = 0.95$).
3. **`testMetallicVsDielectricEnergyConservation`:** Verifies that dielectric surfaces reflect significant diffuse color under broad illumination while metallic surfaces absorb diffuse light, satisfying energy conservation ($B_{\text{dielectric}} = 131$ vs $B_{\text{metallic}} = 50$).
4. **`testNormalMappingPerturbation`:** Verifies that tangent-space normal maps successfully tilt surface normals and redirect illumination ($R_{\text{flat}} = 106$ vs $R_{\text{tilted}} = 32$).
5. **`testEmissiveContribution`:** Verifies that materials with non-zero `emissiveFactor` contribute additive radiant flux regardless of incoming light ($G_{\text{unlit}} = 15$ vs $G_{\text{emissive}} = 255$).
6. **`testAlphaMaskCutoff`:** Verifies that fragments with alpha below `alphaCutoff` are discarded to background clear color, while fragments above cutoff are rendered ($R_{\text{discarded}} = 20$ vs $R_{\text{opaque}} = 131$).
7. **`testDamagedHelmetPbrImportAndRender`:** Loads the canonical Khronos `DamagedHelmet.glb` asset, validates parsing of all 5 PBR texture bindings and tangent attributes, renders the scene via `SceneRenderer`, and reads back 1,481 non-clear PBR pixels.
