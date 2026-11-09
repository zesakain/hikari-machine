# Milestone 8B.6 Specification: glTF Scene Import & Entity Hierarchy
## Deep Architecture Review & Implementation-Ready Specification

**Status:** Architecture Review Complete — Ready for Lead Review & Phased Implementation  
**Target Milestone:** 8B.6  
**Document Location:** `docs/architecture/milestone_8b6_scene_import.md`  
**Governing Standard:** `AGENTS.md` (Strict C++20, `-fno-exceptions`, `-fno-rtti`, Flecs ECS v4.0.4, Jolt Physics, Vulkan RHI, zero hot-path allocation)

---

## 1. Executive Summary

Milestone 8B.5 established Hikari-Machine's initial glTF 2.0 and binary GLB asset loading pipeline through `eng::render::GltfImporter`. While 8B.5 achieved robust ingestion of multi-primitive meshes, normal calculation, winding detection under negative scale, and PBR metallic-roughness material extraction, it relied on a **flatten-and-bake** approach: all glTF nodes were flattened into a single `eng::render::MeshData` asset with world transforms baked into vertex buffers.

Milestone 8B.6 expands Hikari-Machine from a single-mesh model importer into a full **hierarchical scene importer and scene graph system**. This milestone introduces:
1. **First-Class ECS Entity Hierarchy:** Native parent-child relationships in the shared Flecs ECS world using `(flecs::ChildOf, parent)`.
2. **Explicit Transform Separation with Physics Coexistence:** Introducing `TransformComponent` for local TRS and `WorldTransformComponent` for computed global transforms, while safely preserving `PhysicsTransformComponent` as the synchronization contract with Jolt Physics to prevent regression across 40+ call sites.
3. **glTF Scene & Node Graph Ingestion:** Ingesting glTF node hierarchies into corresponding Flecs entity trees, preserving empty parent nodes (locators/pivots), multi-mesh node configurations, instanced geometry deduplication, and local TRS properties.
4. **Interactive Editor Hierarchy Panel:** Upgrading `eng::editor::HierarchyPanel` from a flat ID-sorted leaf list into an interactive, recursive tree view with node expansion/collapsing, reparenting via drag-and-drop, and cascading subtree operations (delete, duplicate).
5. **Hierarchical Scene Serialization:** Extending the `.hikari` JSON scene file format with `parent` stable IDs, ensuring transactional two-pass round-trip serialization and 100% backward compatibility with legacy scenes.
6. **Hierarchy-Aware Viewport Rendering:** Updating `eng::render::SceneRenderer` to render mesh instances using computed world transforms rather than assuming flat, unparented root transforms.

---

## 2. Verified Current Architecture

The following architectural facts have been verified directly in the codebase:

### 2.1 Physics and Transform Ownership
- **[Verified]** **Edit Mode vs. Runtime Session:**
  - In `eng::framework::RuntimeSession` (`engine/framework/src/runtime_session.cpp`), the editor operates on an isolated `editWorld` during `SimulationState::Edit`. Physics simulation is inactive.
  - When the user presses "Play", `RuntimeSession::play()` serializes `editWorld` to a JSON string via `SceneSerializer::serializeToString()`, instantiates a completely separate `runtimeWorld` and `runtimePhysicsWorld`, deserializes the JSON string into `runtimeWorld`, and spawns Jolt rigid bodies from `PhysicsTransformComponent`.
  - When "Stop" is clicked, `runtimeWorld` is destroyed, and the editor seamlessly reverts to `editWorld`. `editWorld` is never mutated by the physics simulation.
- **[Verified]** **Physics Synchronization Flow:**
  - Jolt body lifecycle and synchronization are implemented in `engine/physics/src/physics_components.cpp` via Flecs systems:
    1. `PhysicsPreSync` (`.kind(flecs::OnUpdate)`): Iterates entities with `const PhysicsTransformComponent` and `const RigidBodyComponent`. If `motionType == MotionType::Kinematic`, calls `physicsWorld.setTransform(rb.bodyHandle, t.transform)`.
    2. `PhysicsStep` (`.kind(flecs::OnUpdate)`): Advances Jolt physics via `physicsWorld.step(it.delta_time())`.
    3. `PhysicsPostSync` (`.kind(flecs::PostUpdate)`): Iterates entities with `PhysicsTransformComponent` and `const RigidBodyComponent`. If `motionType == MotionType::Dynamic`, calls `physicsWorld.getTransform(rb.bodyHandle)` and writes the simulated world transform into `t.transform`.
- **[Verified]** **Transform Ownership Answers:**
  1. *Authoritative Transform Owner:* In Edit Mode, Flecs ECS owns the transform. In Runtime Simulation, Jolt Physics owns the transform for Dynamic bodies; Flecs ECS owns the transform for Kinematic and non-physics bodies.
  2. *Physics Read/Write:* Physics reads `PhysicsTransformComponent` at startup (spawning bodies) and in `PhysicsPreSync` (kinematics). Physics writes `PhysicsTransformComponent` in `PhysicsPostSync` (dynamics).
  3. *Render Synchronization:* `SceneRenderer::render()` runs after `PhysicsPostSync` in the frame loop, reading `tc->transform.toMatrix()` directly from `PhysicsTransformComponent`.
  4. *Editor Modification During Active Physics:* If modified on a Dynamic body during runtime simulation, the manual edit in `PhysicsTransformComponent` is overwritten on the very next frame by `PhysicsPostSync`.

### 2.2 Math Conventions & Invariants (`engine/core/include/core/math.h`)
- **[Verified]** **Matrix Layout:** `Mat4` uses column-major memory layout matching Vulkan and OpenGL standards. Translation is stored at column 3 (`m[12], m[13], m[14]`).
- **[Verified]** **Matrix Multiplication Order:** `operator*` implements standard column-vector transformation:
  $$\mathbf{M}_{\text{res}} = \mathbf{M}_{\text{left}} \times \mathbf{M}_{\text{right}}$$
  Transforming a column vector: $\mathbf{p}' = \mathbf{M} \cdot \mathbf{p}$.
  Concatenating parent and child matrices: $\mathbf{M}_{\text{world}} = \mathbf{M}_{\text{parent}} \times \mathbf{M}_{\text{child\_local}}$.
- **[Verified]** **Quaternion Composition:** `Quat::operator*` implements the standard Hamilton product. `(q_parent * q_child).rotate(v)` applies child orientation first, then parent orientation.
- **[Verified]** **Transform Decomposition & Inversion:** `Mat4` provides `inverse()` returning `std::optional<Mat4>` (returns `std::nullopt` if determinant is zero/singular). Note that `inverseAffine()` in `math.h` assumes orthonormal $3\times3$ (pure rotation) and must **not** be used when non-uniform scale is present.

### 2.3 Flecs Integration (`engine/world/src/flecs/flecs.h`)
- **[Verified]** **Flecs Version:** Vendored at Flecs v4.0.4.
- **[Verified]** **Hierarchical Querying:** Flecs v4.0.4 natively supports `.cascade(flecs::ChildOf)` queries, which traverse entities in breadth-first topological order (guaranteeing parents are visited before their children) with zero heap allocations on the hot path.

### 2.4 Existing glTF Importer (`engine/render/src/gltf_importer.cpp`)
- **[Verified]** `GltfImporter` uses `cgltf`. It iterates `data->nodes`, checks `if (node->mesh != nullptr)`, calculates world matrix via `cgltf_node_transform_world()`, and transforms all vertex positions and normals directly into a single baked `MeshData::vertices` array. Node hierarchy, empty locator nodes, and geometry sharing are completely discarded.

---

## 3. Transform Ownership and Migration Strategy

### 3.1 Analysis of `PhysicsTransformComponent`
`PhysicsTransformComponent` is referenced in over 40 distinct locations across the engine and editor:
- Editor panels: `inspector_panel.cpp`, `hierarchy_panel.cpp`, `content_browser_panel.cpp`, `viewport_panel.cpp`.
- Framework & Systems: `runtime_session.cpp`, `scene_serializer.cpp`, `physics_components.cpp`.
- Rendering: `scene_renderer.cpp`, `scene_renderer.h`.
- Automated test suites: `physics_test.cpp`, `scene_test.cpp`, `scene_renderer_test.cpp`, `editor_test.cpp`, `runtime_session_test.cpp`.

**[Verified Finding]:** Naive deprecation or removal of `PhysicsTransformComponent` in Milestone 8B.6 would create massive architectural churn, break serialization compatibility, and introduce subtle physics desynchronization bugs.

### 3.2 Recommended Component Model: The Three-Tier Architecture
**[Recommended]** Milestone 8B.6 adopts a strictly additive, non-breaking three-tier transform model:

1. **`TransformComponent` (Local Spatial Transform):**
   - *Location:* `engine/world/include/world/components.h`.
   - *Semantics:* Authoritative local position, rotation (quaternion), and scale relative to parent entity (or world origin if root).
   - *Fields:* `Transform transform`, `bool isDirty{true}`.
   - *Used by:* Scene graph, editor gizmos, glTF hierarchy importer, serialization.

2. **`WorldTransformComponent` (Computed Global Transform):**
   - *Location:* `engine/world/include/world/components.h`.
   - *Semantics:* Evaluated read-only world transform resulting from hierarchical traversal.
   - *Fields:* `Mat4 worldMatrix{Mat4::identity()}`, `Transform worldTransform{}`.
   - *Used by:* `SceneRenderer`, spatial queries, frustum culling, bounding volume calculations.

3. **`PhysicsTransformComponent` (Simulated Body State):**
   - *Location:* `engine/physics/include/physics/physics_components.h`.
   - *Semantics:* Retained as the explicit physics body synchronization interface with Jolt.
   - *Fields:* `Transform transform`.
   - *Used by:* `PhysicsPreSync`, `PhysicsPostSync`, Jolt body creation.

### 3.3 Physics Synchronization Rules in a Hierarchy
**[Recommended]** Jolt Physics operates exclusively in world coordinates. To prevent mathematical contradictions between hierarchical constraints and physical simulation:
1. **Dynamic Rigid Bodies:**
   - In physics simulation, dynamic rigid bodies move freely in world space under Newtonian forces.
   - *Rule:* If an entity has a `RigidBodyComponent` with `MotionType::Dynamic`, it **must be a root entity** in the ECS hierarchy.
   - *Enforcement:* If an editor user or script reparents an entity with a Dynamic rigid body under another entity, the editor/runtime warns and either:
     - Automatically removes the parent link (detaches to root), OR
     - Converts the body to `MotionType::Kinematic`.
   - *Sync Flow:* `PhysicsPostSync` updates `PhysicsTransformComponent`. The hierarchy system copies this world transform into `TransformComponent` (since it is a root, local == world) and `WorldTransformComponent`.
2. **Kinematic Rigid Bodies:**
   - Kinematic bodies are driven by gameplay or scene graph animation and push dynamic bodies out of their path.
   - *Rule:* Kinematic bodies may be child entities in a hierarchy.
   - *Sync Flow:* The hierarchy propagation computes the child's `WorldTransformComponent`. In `PhysicsPreSync`, the child's computed world transform is pushed to Jolt via `physicsWorld.setTransform(rb.bodyHandle, wt.worldTransform)`.
3. **Static Rigid Bodies / Colliders:**
   - Static bodies do not move during simulation. Their initial world transform is read from `WorldTransformComponent` during body creation.

---

## 4. Transform Hierarchy & Mathematical Invariants

### 4.1 System Execution Order & Scheduler Phases
**[Verified]** Hikari registers systems in `SystemGroup::Continuous` across standard Flecs pipeline phases:

```
[flecs::OnUpdate]
  │  ├── 1. ScriptSystem (Gameplay logic & animation modify TransformComponent)
  │  ├── 2. HierarchyTransformPreSync (Propagates local -> world for kinematic bodies)
  │  ├── 3. PhysicsPreSync (Pushes Kinematic WorldTransforms to Jolt)
  │  └── 4. PhysicsStep (Jolt advances simulation by delta_time)
  │
[flecs::PostUpdate]
  │  ├── 5. PhysicsPostSync (Pulls simulated Dynamic transforms from Jolt into ECS)
  │  └── 6. AnimationPostSync
  │
[flecs::PreStore]
  │  └── 7. HierarchyTransformSystem (Evaluates final WorldTransformComponent for all entities)
  │
[Render Pass (Outside World::progress)]
     └── 8. SceneRenderer::render() (Reads WorldTransformComponent::worldMatrix)
```

### 4.2 Exact Mathematical Invariants
**[Recommended]** The hierarchy implementation must enforce the following 7 strict invariants:

1. **Acyclic Hierarchy Invariant:**
   - No entity may be its own ancestor ($A \not\to \dots \to A$).
   - Any call to `World::setParent(child, parent)` must evaluate `!World::isDescendantOf(parent, child)`. If a cycle is detected, the operation aborts and returns `false`.
2. **Valid World Matrix Before Render Invariant:**
   - Before `SceneRenderer::render()` executes, every active entity possessing `MeshComponent` or child entities must have an up-to-date `WorldTransformComponent`.
3. **Root Invariance:**
   - For any entity with no parent (`!entity.target(flecs::ChildOf).is_valid()`):
     $$\mathbf{M}_{\text{world}} = \mathbf{M}_{\text{local}}$$
     $$\mathbf{T}_{\text{world}} = \mathbf{T}_{\text{local}}$$
4. **Downward Invalidation Invariant:**
   - If an entity's `TransformComponent` is modified, or if its parent changes, its `isDirty` flag is set to `true`. When evaluated, all of its transitive descendants are recomputed.
5. **Non-Uniform Scale Shearing Handling:**
   - Combining a parent's non-uniform scale $\mathbf{S}_{\text{parent}}$ with a child's rotation $\mathbf{R}_{\text{child}}$ produces shearing. A standard orthogonal TRS `Transform` cannot represent shear without distortion.
   - *Invariant:* `WorldTransformComponent::worldMatrix` is the **ground truth** transformation matrix passed to Vulkan shaders and used for AABB bounding transformations. `WorldTransformComponent::worldTransform` stores a best-effort orthogonal decomposition for inspector telemetry.
6. **Zero Hot-Path Allocation Invariant:**
   - Transform propagation must never invoke `new`, `malloc`, or dynamic vector reallocation during the simulation tick. Flecs queries with `.cascade(flecs::ChildOf)` operate on pre-allocated archetype chunks.
7. **Cascading Destruction Invariant:**
   - When an entity is destroyed via `World::destroyEntity(h)`, all of its descendant entities are recursively destructed, removed from `activeEntities_`, and deselected in `SelectionModel`.

### 4.3 Dirty Propagation Algorithm (Flecs-Native)
**[Recommended]** Using Flecs v4.0.4's `.cascade(flecs::ChildOf)` query:

```cpp
void registerHierarchySystems(world::World& world) {
    auto& ecs = world.getRawWorld();

    // Query automatically sorts entities breadth-first: parents precede children
    auto hierarchySys = ecs.system<WorldTransformComponent, TransformComponent>()
        .term(flecs::ChildOf).cascade()
        .kind(flecs::PreStore)
        .run([](flecs::iter& it) {
            while (it.next()) {
                auto wt = it.field<WorldTransformComponent>(0);
                auto tc = it.field<TransformComponent>(1);

                for (auto i : it) {
                    flecs::entity e = it.entity(i);
                    flecs::entity parent = e.target(flecs::ChildOf);

                    if (!parent.is_valid()) {
                        // Root entity
                        if (tc[i].isDirty) {
                            wt[i].worldMatrix = tc[i].transform.toMatrix();
                            wt[i].worldTransform = tc[i].transform;
                            tc[i].isDirty = false;
                        }
                    } else {
                        // Child entity: parent is guaranteed already evaluated this frame
                        const auto* parentWt = parent.get<WorldTransformComponent>();
                        const auto* parentTc = parent.get<TransformComponent>();
                        bool mustUpdate = tc[i].isDirty || (parentTc && parentTc->isDirty);

                        if (mustUpdate && parentWt != nullptr) {
                            Mat4 localMat = tc[i].transform.toMatrix();
                            wt[i].worldMatrix = parentWt->worldMatrix * localMat;
                            wt[i].worldTransform = parentWt->worldTransform.combine(tc[i].transform);
                            tc[i].isDirty = false;
                        }
                    }
                }
            }
        });

    world.registerSystem(world::SystemGroup::Continuous, hierarchySys);
}
```

---

## 5. Reparenting Semantics

### 5.1 Default Behavior: Preserve World Transform
**[Recommended]** When an entity is reparented under a new parent in the editor or via API, its visual world placement must remain stationary:

1. **Capture Current World Transform:**
   Retrieve the entity's current world matrix: $\mathbf{M}_{\text{child, world}} = \mathbf{W}_{\text{child}}.\text{worldMatrix}$.
2. **Evaluate Target Parent World Matrix:**
   - If target parent is valid (`newParent.isValid()`):
     $\mathbf{M}_{\text{parent, world}} = \mathbf{W}_{\text{parent}}.\text{worldMatrix}$.
   - If target parent is null (detaching to root):
     $\mathbf{M}_{\text{parent, world}} = \mathbf{I}$ (Identity).
3. **Inversion & Local Transform Calculation:**
   - Compute $\mathbf{M}_{\text{parent, world}}^{-1}$ using full general matrix inversion (`Mat4::inverse()`).
   - If `inverse()` returns `std::nullopt` (e.g. parent scale is zero or matrix is singular):
     - **[Verified Safeguard]:** The reparenting operation fails, logs `ENG_LOG_ERROR("World", "Cannot reparent: Target parent matrix is singular/non-invertible")`, and returns `false`. The entity's parent is unchanged.
   - Compute new local matrix:
     $$\mathbf{M}_{\text{new\_local}} = \mathbf{M}_{\text{parent, world}}^{-1} \times \mathbf{M}_{\text{child, world}}$$
   - Decompose $\mathbf{M}_{\text{new\_local}}$ into translation, rotation (quaternion), and scale, assigning the result to the entity's `TransformComponent`.
4. **Establish Flecs Relationship:**
   Call `childEntity.child_of(newParentEntity)` or remove `flecs::ChildOf` if detaching to root. Set `tc->isDirty = true`.

### 5.2 Explicit Alternative Mode: Preserve Local Transform
**[Recommended]**
- When snapping accessories to sockets or equipment slots (e.g. weapon to a character hand socket), preserving local transform is desirable ($T_{\text{local}}$ is preserved, causing the object to snap into the parent's frame).
- In the Editor UI (`HierarchyPanel`), dropping an entity onto another defaults to **Preserve World Transform**. Right-clicking the target node exposes an explicit menu option: **"Paste / Move Here (Preserve Local Transform)"**. No confusing hotkeys or magic modifiers are required.

---

## 6. glTF Importer Architecture & Node Mapping

### 6.1 `GltfSceneImporter` vs. Static `GltfImporter`
- **[Verified Decision]** The existing Milestone 8B.5 `GltfImporter::importFromFile()` flatten-and-bake pipeline is **retained without modification** under `AssetManager::loadMesh()` for static props and environment geometry.
- A new dedicated importer, `eng::render::GltfSceneImporter` (`engine/render/include/render/gltf_scene_importer.h`), is introduced for hierarchical scene ingestion.

### 6.2 Node Mapping Specification
When parsing a glTF 2.0 or GLB asset with `cgltf`:

1. **Scene Selection:**
   - `cgltf_data` contains `scene` (default scene pointer) and `scenes` array.
   - The importer selects `data->scene ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr)`.
2. **Every Node Becomes an Entity:**
   - Every node in the selected scene graph maps to an ECS entity in `world::World`.
3. **Nodes Without Meshes (Locators / Grouping Nodes):**
   - Preserved as pure transform entities containing `TransformComponent` and `WorldTransformComponent`. They act as pivot points, camera mount points, or hierarchical grouping folders.
4. **Local TRS Extraction:**
   - If `node->has_matrix`: Decompose $4\times4$ matrix into translation, rotation (quaternion), and scale using polar/Gram-Schmidt decomposition.
   - If TRS attributes are present: Directly populate `Transform{translation, rotation, scale}`.
5. **Geometry Sharing & Asset Deduplication:**
   - Multiple glTF nodes pointing to the same `cgltf_mesh` do **not** duplicate vertex data.
   - Each unique `cgltf_mesh` is registered once with `AssetManager` under the URI `filepath#mesh_<index>`.
   - All corresponding entities receive `MeshComponent` referencing the identical `AssetId`.
6. **Multi-Primitive Meshes:**
   - If a `cgltf_mesh` contains multiple primitives (each with its own material):
     - The node entity acts as a parent locator.
     - For each primitive $i \in [0, \text{primitives\_count}-1]$, a child entity is spawned named `"{NodeName}_Primitive_{i}"` with identity local transform, carrying `MeshComponent{submeshIndex = i}`.
7. **Material References:**
   - glTF materials are parsed using the existing Milestone 8B.5 PBR metallic-roughness extractor, loaded into `AssetManager`, and bound via `MeshComponent::materialAsset`.
8. **Invalid or Unsupported Nodes:**
   - Nodes with malformed matrices or unsupported extension cameras/lights are imported as empty transform entities, logging diagnostic warnings without aborting scene import.

---

## 7. Serialization and Backward Compatibility

### 7.1 Schema Extension (`docs/architecture/scene_format.md`)
**[Recommended]** The `.hikari` JSON format adds an optional `"parent"` property to entity objects:

```json
{
  "hikari_scene_version": 1,
  "metadata": {
    "name": "HierarchicalScene",
    "timestamp": 1731196800
  },
  "entities": [
    {
      "id": 1001,
      "name": "RootVehicle",
      "components": {
        "Transform": {
          "position": [0.0, 1.0, 0.0],
          "rotation": [0.0, 0.0, 0.0, 1.0],
          "scale": [1.0, 1.0, 1.0]
        }
      }
    },
    {
      "id": 1002,
      "name": "Turret",
      "parent": 1001,
      "components": {
        "Transform": {
          "position": [0.0, 0.8, 0.2],
          "rotation": [0.0, 0.3826834, 0.0, 0.9238795],
          "scale": [1.0, 1.0, 1.0]
        }
      }
    }
  ]
}
```

### 7.2 Two-Pass Staged Deserialization Algorithm
To support arbitrary entity order in JSON without dangling references:

- **Pass 1: Validation & Entity Staging:**
  1. Parse JSON objects. Check for duplicate stable IDs.
  2. Extract `parentStableId = entVal.getUInt64("parent", 0)`.
  3. Validate: If `parentStableId == staged.stableId`, reject self-parenting and set `parentStableId = 0`.
  4. Instantiate all entities in `world::World`. Assign `StableIdComponent{stableId}`.
  5. Populate mapping table: `std::unordered_map<uint64_t, world::EntityHandle> stableIdToHandle`.
  6. Populate components (`TransformComponent`, `PhysicsTransformComponent`, `MeshComponent`, etc.).
- **Pass 2: Hierarchy Assembly & Topological Verification:**
  1. For each staged entity where `parentStableId != 0`:
     - Lookup parent handle in `stableIdToHandle`.
     - If not found: Log warning `ENG_LOG_WARN("Scene", "Dangling parent ID referenced; defaulted to root")` and leave entity as root.
     - If found: Check `world.isDescendantOf(parentHandle, childHandle)` to detect cycles. If cycle detected, log error and leave entity as root.
     - Otherwise, call `world.setParent(childHandle, parentHandle)`.
- **Backward Compatibility Guarantee:**
  - Existing scenes lack the `"parent"` field. `getUInt64("parent", 0)` returns `0`. All legacy entities are instantiated as roots, preserving 100% backward compatibility.

---

## 8. Rendering and Viewport Integration

### 8.1 `SceneRenderer` World Matrix Query
**[Recommended]** `SceneRenderer::render()` (`engine/render/src/scene_renderer.cpp`) is updated to query world matrices in order of fidelity:

```cpp
Mat4 model = Mat4::identity();
if (e.has<world::WorldTransformComponent>()) {
    const auto* wt = e.get<world::WorldTransformComponent>();
    if (wt != nullptr) {
        model = wt->worldMatrix;
    }
} else if (e.has<world::TransformComponent>()) {
    const auto* tc = e.get<world::TransformComponent>();
    if (tc != nullptr) {
        model = tc->transform.toMatrix();
    }
} else if (e.has<physics::PhysicsTransformComponent>()) {
    const auto* pt = e.get<physics::PhysicsTransformComponent>();
    if (pt != nullptr) {
        model = pt->transform.toMatrix();
    }
}
```

### 8.2 Bounding Volume Transformation & Culling
Local mesh AABBs are transformed into world space using the computed $4\times4$ world matrix:
$$\mathbf{AABB}_{\text{world}} = \mathbf{AABB}_{\text{local}}.\text{transformed}(\mathbf{M}_{\text{world}})$$
This accounts for all parent translations, rotations, and non-uniform scaling without separate hierarchy traversals during culling.

---

## 9. Implementation Phases

```
Phase 1: ECS Hierarchy API & Transform Components (Local & World)
  │
  ▼
Phase 2: Hierarchy Transform Propagation System (.cascade(ChildOf))
  │
  ▼
Phase 3: GltfSceneImporter (Multi-node graph parsing & geometry instancing)
  │
  ▼
Phase 4: Editor Hierarchy Panel Redesign (Recursive tree & drag-drop reparenting)
  │
  ▼
Phase 5: Hierarchical Scene Serialization (.hikari two-pass round-trip)
  │
  ▼
Phase 6: End-to-End Viewport Integration & Test Suite Verification
```

---

### Phase 1: ECS Hierarchy API & Transform Components
- **Objective:** Introduce `TransformComponent`, `WorldTransformComponent`, and parent-child ECS operations in `eng::world::World`.
- **Files Modified:**
  - `engine/world/include/world/components.h` (new/updated)
  - `engine/world/include/world/world.h`
  - `engine/world/src/world.cpp`
- **Required APIs:**
  - `bool World::setParent(EntityHandle child, EntityHandle parent)`
  - `EntityHandle World::getParent(EntityHandle child) const`
  - `std::vector<EntityHandle> World::getChildren(EntityHandle parent) const`
  - `bool World::isDescendantOf(EntityHandle descendant, EntityHandle ancestor) const`
  - `std::vector<EntityHandle> World::getRootEntities() const`
- **Dependencies:** Flecs v4.0.4.
- **Unit Tests:** `tests/unit/world/hierarchy_test.cpp`.
- **Completion Criteria:** All hierarchy tests pass; cycles rejected; cascading destruction verified.

---

### Phase 2: Hierarchy Transform Propagation System
- **Objective:** Implement top-down transform propagation updating `WorldTransformComponent`.
- **Files Modified:**
  - `engine/world/include/world/hierarchy_systems.h` (new)
  - `engine/world/src/hierarchy_systems.cpp` (new)
  - `engine/world/src/world.cpp`
- **Required Mechanics:**
  - Flecs system running at `flecs::PreStore` using `.cascade(flecs::ChildOf)`.
  - Non-uniform scale matrix composition ($M_{\text{world}} = M_{\text{parent}} \times M_{\text{local}}$).
  - Dirty flag optimization.
- **Dependencies:** Phase 1.
- **Unit Tests:** `tests/unit/world/transform_propagation_test.cpp`.
- **Completion Criteria:** Numerical verification of nested translations, rotations, and scales against analytical ground truth. Zero heap allocations during simulation tick.

---

### Phase 3: glTF Scene Importer
- **Objective:** Parse glTF node graphs into structured `GltfSceneTemplate` without flattening vertices.
- **Files Modified:**
  - `engine/render/include/render/gltf_scene_importer.h` (new)
  - `engine/render/src/gltf_scene_importer.cpp` (new)
  - `engine/render/CMakeLists.txt`
- **Required Mechanics:**
  - Extract `cgltf_node` hierarchy, local TRS, empty locator nodes, and multi-primitive splits.
  - Deduplicate shared meshes through `AssetManager`.
- **Dependencies:** Phase 1, Phase 2, `cgltf`.
- **Unit Tests:** `tests/unit/render/gltf_scene_importer_test.cpp`.
- **Completion Criteria:** glTF assets parse into template structures preserving all node relationships and shared geometry.

---

### Phase 4: Editor Hierarchy Panel Redesign
- **Objective:** Recursive tree view rendering with drag-and-drop reparenting.
- **Files Modified:**
  - `editor/include/editor/hierarchy_panel.h`
  - `editor/src/panels/hierarchy_panel.cpp`
- **Required Mechanics:**
  - Recursive `drawEntityNode()` with `ImGuiTreeNodeFlags_OpenOnArrow`.
  - Drag-drop payload `HIERARCHY_ENTITY_DRAG`.
  - Preserve World Transform calculation upon reparenting.
- **Dependencies:** Phase 1, Phase 2.
- **Integration Tests:** `tests/unit/editor/editor_test.cpp`.
- **Completion Criteria:** Interactive tree expand/collapse; drag-drop reparenting stationary in world space.

---

### Phase 5: Hierarchical Scene Serialization
- **Objective:** Extend `.hikari` format to serialize and deserialize entity parentage.
- **Files Modified:**
  - `engine/framework/src/scene_serializer.cpp`
  - `engine/framework/include/framework/scene_serializer.h`
- **Required Mechanics:**
  - Add optional `"parent"` field. Two-pass staged deserializer.
  - Cycle detection and dangling parent recovery.
- **Dependencies:** Phase 1, Phase 2.
- **Unit Tests:** `tests/unit/framework/scene_test.cpp`.
- **Completion Criteria:** Round-trip fidelity for nested hierarchies; 100% backward compatibility for legacy scenes.

---

### Phase 6: End-to-End Viewport Integration & Test Suite Verification
- **Objective:** Connect glTF scene spawning to the Viewport and verify rendering.
- **Files Modified:**
  - `editor/src/panels/viewport_panel.cpp`
  - `engine/render/src/scene_renderer.cpp`
- **Required Mechanics:**
  - Dragging glTF into Viewport spawns full hierarchy.
  - `SceneRenderer` renders child meshes using `WorldTransformComponent::worldMatrix`.
- **Dependencies:** Phases 1–5.
- **Integration Tests:** `tests/integration/scene_hierarchy_integration_test.cpp`.
- **Completion Criteria:** Full end-to-end drag-and-drop hierarchy rendered accurately in Vulkan Viewport.

---

## 10. Acceptance Criteria

1. **Hierarchy Data Model:**
   - [ ] `World::setParent()`, `getParent()`, and `getChildren()` manage Flecs `(flecs::ChildOf, parent)` relationships.
   - [ ] Cyclic reparenting attempts abort and return `false` without modifying hierarchy.
   - [ ] Destroying a parent entity cascades to all descendants, removing handles from `activeEntities_`.
2. **Transform Propagation:**
   - [ ] `HierarchyTransformSystem` computes `WorldTransformComponent` across $\ge 5$ levels of nesting.
   - [ ] $M_{\text{world}} = M_{\text{parent}} \times M_{\text{local}}$ verified for non-uniform scale and rotation.
   - [ ] Zero heap allocations occur in `HierarchyTransformSystem` during the simulation tick.
3. **glTF Scene Ingestion:**
   - [ ] Ingesting a multi-node glTF file produces an entity tree matching the file's node graph.
   - [ ] Empty nodes are preserved as locator transform entities.
   - [ ] Shared meshes reuse `AssetManager` instances rather than duplicating vertex buffers.
4. **Editor Hierarchy Panel:**
   - [ ] Entities with children display expandable/collapsible tree disclosure arrows.
   - [ ] Drag-and-drop reparenting preserves world transform (object remains stationary in viewport).
   - [ ] Reparenting onto non-invertible parent is safely rejected.
5. **Serialization:**
   - [ ] Saving a hierarchical scene outputs valid JSON with `"parent"` stable IDs.
   - [ ] Deserialization restores exact hierarchy regardless of entity array order.
   - [ ] Legacy `.hikari` files load without errors as root entities.
6. **Rendering Accuracy:**
   - [ ] `SceneRenderer` renders child meshes using `WorldTransformComponent::worldMatrix`.
   - [ ] Transforming a parent entity moves all child geometry synchronously in the Viewport.
7. **Zero Regressions:**
   - [ ] All existing 31 glTF unit tests and physics tests continue to pass.
   - [ ] Clean build under `-Wall -Wextra -Werror` with zero `clang-tidy` warnings.

---

## 11. Test Scenarios

### 11.1 Unit Tests
- `HierarchyTest.ParentChildBasic`: Verify parenting, parent retrieval, and child enumeration.
- `HierarchyTest.CycleRejection`: Attempt direct ($A \to A$, $A \to B \to A$) and transitive cycles ($A \to B \to C \to A$).
- `HierarchyTest.CascadingDestruction`: Destroy root; assert all descendants are dead and removed from `activeEntities_`.
- `TransformPropagationTest.AnalyticalNesting`: Verify 4-level transform composition against hand-calculated matrices.
- `TransformPropagationTest.ShearingHandling`: Verify non-uniform scale + rotation produces mathematically exact world matrix.
- `GltfSceneImporterTest.LocatorNodesPreserved`: Verify nodes with no mesh exist as entities with valid local transforms.
- `GltfSceneImporterTest.GeometryInstancing`: Two nodes referencing the same mesh produce identical `meshAsset` IDs.
- `SceneSerializerTest.HierarchyRoundTrip`: Save 3-level tree; reload into fresh world; verify handles and transforms match.
- `SceneSerializerTest.LegacySceneBackwardsCompatibility`: Load flat scene without `"parent"` fields; verify no errors.

### 11.2 Integration Tests
- `SceneHierarchyIntegrationTest.SimulatedHierarchy`: Step simulation in `RuntimeSession`; verify kinematic parents move child colliders, and dynamic bodies remain in world coordinates.

---

## 12. Architectural Risks and Open Decisions

### 12.1 Identified Risks & Mitigations
1. **Singular Parent Matrix Inversion:**
   - *Risk:* If a parent has scale 0 in any axis, $\mathbf{M}_{\text{parent}}$ is singular. Recomputing local transform $\mathbf{M}_{\text{parent}}^{-1} \times \mathbf{M}_{\text{child}}$ would divide by zero.
   - *Mitigation:* `Mat4::inverse()` returns `std::nullopt`. The reparenting logic explicitly checks for `std::nullopt` and cancels the reparenting operation, logging an error.
2. **Dynamic Rigid Bodies in Hierarchies:**
   - *Risk:* In Jolt, dynamic bodies move according to physics equations in world space. Parenting a dynamic body under a moving entity creates competing authorities.
   - *Mitigation:* Strictly enforce that entities with `RigidBodyComponent` set to `Dynamic` must be root entities.

### 12.2 Open Decisions Requiring Human Lead Approval
1. **Multi-Primitive Split Entity vs. Single Entity:**
   - *Question:* Should meshes with multiple primitives always spawn child entities per primitive, or should `MeshComponent` be updated in a future milestone to render all submeshes?
   - *Status:* **[Open]**
   - *Recommendation:* For Milestone 8B.6, spawn child primitive entities (`{NodeName}_Primitive_{i}`). This requires zero changes to the Vulkan renderer pipeline and allows per-primitive material inspection in the Inspector panel.
2. **Future Deprecation Timeline for `PhysicsTransformComponent`:**
   - *Question:* When should `PhysicsTransformComponent` be unified with `WorldTransformComponent`?
   - *Status:* **[Open]**
   - *Recommendation:* Defer deprecation to Milestone 9. Keep `PhysicsTransformComponent` active throughout Milestone 8B to guarantee zero regressions across editor and physics tests.

---

*End of Milestone 8B.6 Deep Architecture Review.*
