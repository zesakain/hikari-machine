# Hikari Engine — Persistent Benchmark Scenes & Visual Showcase

## 1. Overview

The persistent benchmark scenes in Hikari Engine provide turn-key, pre-configured 3D environments and assets for performance benchmarking, rendering validation, and editor inspection.

Rather than requiring manual glTF import and configuration each time, these scenes are serialized in the native `.hikari` JSON format under `assets/benchmark-scenes/`. When opened in the Hikari Editor or loaded via runtime CLI, they automatically instantiate complete entity hierarchies, assign PBR materials and mesh assets via relative paths, configure directional/point lighting and shadow casters, frame the viewport camera, and apply tuned post-processing settings (exposure, ACES tone mapping, bloom).

---

## 2. Benchmark Scene Catalog

### 2.1 Sponza Benchmark (`assets/benchmark-scenes/Sponza_Benchmark.hikari`)
- **Asset Source:** Khronos glTF 2.0 Sponza (`test-assets/benchmark/sponza/Sponza.gltf`)
- **ECS Entities:** 106 total entities (1 root transform, 103 mesh primitives, 1 sunlight entity, 1 benchmark camera entity)
- **Geometry:** 262,267 vertices, 532,152 triangles across 103 submeshes with full tangent space
- **Materials:** 25 glTF PBR metallic-roughness materials with base color, normal maps, metallic-roughness, emissive, and occlusion
- **Lighting:**
  - Directional Sunlight: Direction `(0.45, 0.85, 0.25)`, intensity `3.5`, color `#FFF8F0` (warm afternoon sunlight), shadow caster enabled with 2048x2048 depth map.
  - Fill Point Light: Position `(0, 1.5, 0)`, intensity `8.0`, radius `15.0m`, color `#D8E8F5` (subtle courtyard ambient bounce).
- **Post-Processing & Framing:**
  - Tone Mapping: `AcesFitted`
  - Exposure: `1.0`
  - Bloom: Enabled (`threshold: 1.0`, `intensity: 0.05`)
  - Camera Viewport: Position `(0.0, 2.0, 0.0)`, Target `(0.0, 2.0, -6.0)` (framed looking down the grand colonnade)

### 2.2 Flight Helmet Benchmark (`assets/benchmark-scenes/FlightHelmet_Benchmark.hikari`)
- **Asset Source:** Khronos FlightHelmet (`test-assets/benchmark/flight_helmet/FlightHelmet.gltf`)
- **ECS Entities:** 8 total entities (1 root node, 5 mesh primitive components, 1 directional key light, 1 benchmark camera)
- **Geometry:** High-fidelity multi-part helmet, hose, lenses, and leather straps
- **Materials:** 5 distinct PBR materials testing metal, glass, fabric, and aged leather
- **Lighting:**
  - Key Directional Light: Direction `(0.5, 0.7, 0.5)`, intensity `2.8`, color `#FFFFFF`
- **Post-Processing & Framing:**
  - Tone Mapping: `AcesFitted`, Exposure: `1.0`, Bloom: Enabled
  - Camera Viewport: Position `(0.0, 0.35, 0.85)`, Target `(0.0, 0.2, 0.0)` (framed close-up on visor and leather textures)

### 2.3 Damaged Helmet Benchmark (`assets/benchmark-scenes/DamagedHelmet_Benchmark.hikari`)
- **Asset Source:** Battle Damaged Sci-Fi Helmet (`test-assets/benchmark/damaged_helmet/DamagedHelmet.gltf`)
- **ECS Entities:** 3 total entities (1 mesh entity, 1 key sunlight entity, 1 benchmark camera)
- **Geometry:** Detailed single-mesh battle helmet
- **Materials:** Single 2K/4K PBR material testing scratched metal, weathered plastic, emissive visor, and occlusion
- **Lighting:**
  - Directional Light: Direction `(0.6, 0.6, 0.5)`, intensity `3.0`, color `#FFF5EB`
- **Post-Processing & Framing:**
  - Tone Mapping: `AcesFitted`, Exposure: `1.0`
  - Camera Viewport: Position `(0.0, 0.0, 2.5)`, Target `(0.0, 0.0, 0.0)`

---

## 3. Editor & Runtime Workflows

### 3.1 Main Menu Bar — Dedicated "Benchmarks" Menu
The Editor menu bar provides a top-level `Benchmarks` menu with the following actions:
- **Load Sponza Benchmark:** Opens `assets/benchmark-scenes/Sponza_Benchmark.hikari`.
- **Load Flight Helmet Benchmark:** Opens `assets/benchmark-scenes/FlightHelmet_Benchmark.hikari`.
- **Load Damaged Helmet Benchmark:** Opens `assets/benchmark-scenes/DamagedHelmet_Benchmark.hikari`.
- **Reset Benchmark Camera (`Ctrl+R`):** Snaps the editor camera eye and target back to the author-specified benchmark camera orientation.

### 3.2 Main Menu Bar — Active Scene Indicator
The active scene name is displayed with high visibility in the main menu bar:
`[Scene: Sponza_Benchmark.hikari]` (or `[Scene: Sponza_Benchmark.hikari *]` when unsaved modifications exist).

### 3.3 File -> Open Scene Modal Quick-Launch
When opening scenes through `File -> Open Scene...`, quick-launch buttons are embedded in the modal dialog:
- `[Sponza]`
- `[Flight Helmet]`
- `[Damaged Helmet]`

Clicking any button immediately loads the corresponding scene and sets up camera and lighting.

### 3.4 Content Browser Integration
The Content Browser allows browsing to `assets/benchmark-scenes/`:
- **Double-click:** Double-clicking any `.hikari` file in either Grid view or List view immediately loads the scene into the active World.
- **Context Menu:** Right-clicking any `.hikari` item presents `Open Scene`, which executes the same workspace load workflow.

### 3.5 Command-Line Startup
Any benchmark scene can be passed directly to the editor executable via the `--scene` argument:
```bash
./build/editor/hikari_editor --scene assets/benchmark-scenes/Sponza_Benchmark.hikari
```
The workspace manager loads the scene, applies saved viewport camera framing, configures post-processing, and displays the scene in the viewport.

---

## 4. Technical Architecture

### 4.1 Extended Scene Serialization Schema
Hikari scene files (`.hikari`) serialize light components, camera components, and scene-level settings:
```json
{
  "hikari_scene_version": 1,
  "settings": {
    "camera": {
      "position": [0.0, 2.0, 0.0],
      "target": [0.0, 2.0, -6.0],
      "fovDegrees": 65.0
    },
    "postProcess": {
      "toneMapOp": 1,
      "exposure": 1.0,
      "bloomEnabled": true,
      "bloomThreshold": 1.0,
      "bloomIntensity": 0.05
    }
  },
  "entities": [
    {
      "name": "Sunlight",
      "directionalLight": {
        "direction": [0.45, 0.85, 0.25],
        "color": [1.0, 0.97, 0.94],
        "intensity": 3.5,
        "castsShadows": true
      }
    },
    {
      "name": "BenchmarkCamera",
      "camera": {
        "fovDegrees": 65.0,
        "nearPlane": 0.1,
        "farPlane": 100.0,
        "isPrimary": true
      }
    }
  ]
}
```

### 4.2 Portable Relative Asset Referencing
All mesh and material asset identifiers inside `.hikari` scenes use repository-relative URIs:
`"mesh": "test-assets/benchmark/sponza/Sponza.gltf#mesh_0"`
When `SceneSerializer::deserialize` runs, it resolves relative paths through `AssetManager`, ensuring machine independence across developer workstations and CI runners.

### 4.3 Inspector Panel Integration
The Editor Inspector panel supports editing:
- `DirectionalLightComponent`: Direction, Color, Intensity, Shadow Caster toggle.
- `PointLightComponent`: Position/Offset, Color, Intensity, Attenuation Radius, Shadow Caster toggle.
- `CameraComponent`: Field of View (FOV), Near Clip Plane, Far Clip Plane, Primary Camera toggle.
- `+ Add Component` menu includes entries to add `Directional Light`, `Point Light`, or `Camera` to any entity.

---

## 5. Visual Evidence & Artifacts

Pre-rendered visual verification captures generated from the persistent scenes:
1. `sponza_persistent_overview.png`: Wide colonnade framing with ACES tone mapping and directional sunlight.
2. `sponza_persistent_detail_arch.png`: Close-up of archways and textured marble columns.
3. `sponza_persistent_shadows.png`: Courtyard floor shadow map projection.
4. `flight_helmet_persistent_scene.png`: Multi-material visor, hose, and leather rendering.
5. `damaged_helmet_persistent_scene.png`: Weathered metal, scratches, and roughness reflection.
