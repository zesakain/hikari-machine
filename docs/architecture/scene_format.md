# Native Scene Format (.hikari)

## 1. Overview

Hikari-Machine uses `.hikari` as its native scene file format. It is a human-readable, versioned JSON representation of scene entities, stable identifiers, and their attached ECS components.

Scene files are loaded transactionally by `eng::framework::SceneSerializer` into the engine's `eng::world::World` (Flecs ECS) and saved atomically using safe filesystem replacement semantics.

---

## 2. File Schema Specification (Version 1)

```json
{
  "format": "hikari_scene",
  "version": 1,
  "metadata": {
    "name": "Sample Scene",
    "generator": "Hikari Editor 0.1.0",
    "timestamp": 1731050000
  },
  "entities": [
    {
      "id": "cube_01",
      "name": "GroundBox",
      "components": {
        "Transform": {
          "position": [0.0, -0.5, 0.0],
          "rotation": [0.0, 0.0, 0.0, 1.0],
          "scale": [10.0, 1.0, 10.0]
        },
        "RigidBody": {
          "motion_type": "static",
          "collision_layer": 0
        },
        "Collider": {
          "type": "box",
          "half_extents": [5.0, 0.5, 5.0],
          "offset": [0.0, 0.0, 0.0]
        }
      }
    },
    {
      "id": "sphere_01",
      "name": "DynamicBall",
      "components": {
        "Transform": {
          "position": [0.0, 5.0, 0.0],
          "rotation": [0.0, 0.0, 0.0, 1.0],
          "scale": [1.0, 1.0, 1.0]
        },
        "RigidBody": {
          "motion_type": "dynamic",
          "collision_layer": 1
        },
        "Collider": {
          "type": "sphere",
          "radius": 0.5,
          "offset": [0.0, 0.0, 0.0]
        },
        "Script": {
          "script_name": "on_update",
          "enabled": true,
          "update_interval": 0.016
        }
      }
    }
  ]
}
```

---

## 3. Schema Fields

### Header & Metadata
- `format`: Must be `"hikari_scene"`.
- `version`: Integer indicating schema revision (`1`). Files with unsupported versions are rejected cleanly with `ErrorCode::InvalidVersion`.
- `metadata`:
  - `name`: Display name of the scene.
  - `generator`: Application and version that generated the file.
  - `timestamp`: Unix epoch seconds at serialization time.

### Entity Objects
- `id`: Stable string identifier. Unlike runtime Flecs entity IDs (`flecs::entity::id()`), stable IDs persist across serialization cycles, machines, and editor sessions. Tracked in ECS via `eng::framework::StableIdComponent`.
- `name`: Display name of the entity in the Scene Hierarchy and Inspector.
- `components`: Key-value map of component type names to component properties.

---

## 4. Supported Components

| Component Key | Engine Type | Serialized Properties |
| :--- | :--- | :--- |
| `Transform` | `physics::PhysicsTransformComponent` | `position` ([x, y, z]), `rotation` ([x, y, z, w] quaternion), `scale` ([x, y, z]) |
| `RigidBody` | `physics::RigidBodyComponent` | `motion_type` (`"static"`, `"kinematic"`, `"dynamic"`), `collision_layer` (integer) |
| `Collider` | `physics::ColliderComponent` | `type` (`"box"`, `"sphere"`, `"capsule"`), dimensions (`half_extents`, `radius`, `half_height`), `offset` |
| `Script` | `script::ScriptComponent` | `script_name` (string), `enabled` (boolean), `update_interval` (float) |

### Extensibility & Forward Compatibility
- **Unknown Components:** If a scene file contains components not recognized by the current engine build, `SceneSerializer` logs a non-fatal warning and safely ignores them, allowing backward and forward compatibility across engine versions.
- **Missing Optional Components:** Entities are not required to hold all components. Components are added to the Flecs entity only when present in the JSON.

---

## 5. Reliability and Transactional Safety

### Atomic File Save
Writing directly to a target scene file risks truncation or corruption if an I/O error or crash occurs during writing. Hikari uses atomic save semantics:
1. The scene JSON is serialized and written to `<filepath>.tmp`.
2. The temporary file is flushed to disk.
3. The temporary file is renamed to `<filepath>`, replacing the old file atomically via filesystem semantics (`std::filesystem::rename`).
4. If serialization or writing fails at any point, the existing scene file remains completely untouched.

### Transactional Deserialization
Loading a scene file must never leave the simulation world in a partially populated or corrupted state:
1. The JSON string is parsed into an AST DOM (`core::JsonValue`).
2. Syntactic and structural checks validate header format, version, and entity array integrity.
3. All entities, stable IDs, and component fields are staged in memory (`StagedEntity` structures).
4. Validation asserts that stable IDs are unique and geometric values are valid (e.g., scale dimensions > 0).
5. Only after all validation passes is the active world mutated: existing user entities are cleared via `world.clearUserEntities()` and staged entities are spawned into Flecs.
6. If any step fails, an error is returned and the active simulation world is left completely unmodified.

---

## 6. Editor Integration

`WorkspaceManager` encapsulates native scene workflows:
- **File Menu Actions:**
  - `New Scene` (`Ctrl+N`): Prompts confirmation if unsaved changes exist, then clears entities and resets scene path.
  - `Open Scene...` (`Ctrl+O`): Prompts confirmation if unsaved changes exist, displays file path modal, loads scene, and updates path.
  - `Save Scene` (`Ctrl+S`): Saves to active scene path, or prompts Save As modal if untitled.
  - `Save Scene As...` (`Ctrl+Shift+S`): Prompts path dialog modal and writes scene.
- **Dirty State Tracking (`*`):**
  - Dirty flag set automatically on entity creation, duplication, deletion, renaming, component addition/removal, and property mutation in Inspector or Hierarchy.
  - Displayed prominently in toolbar and status bar (`Scene: Name*`).
  - Cleared automatically on successful save or open.
- **Selection Safety:**
  - `SelectionModel` is cleared upon loading or creating a scene to ensure no stale entity handles persist across scene transitions.
