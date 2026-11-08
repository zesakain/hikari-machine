# Content Browser Navigation & Asset Management

**Subsystem:** Editor Tooling  
**Milestone:** 8B.5.1  
**Primary Header:** `editor/include/editor/content_browser_controller.h`  
**Panel Implementation:** `editor/src/panels/content_browser_panel.cpp`  

---

## 1. Overview

The Content Browser is the primary workspace in Hikari-Machine for discovering, organizing, creating, and inspecting project assets. It is decoupled into two architectural layers:

1. **Headless Controller (`eng::editor::ContentBrowserController`)**:
   - Manages filesystem enumeration, path normalization, and root containment.
   - Maintains navigation history (Back / Forward / Up) and breadcrumb stacks.
   - Executes filesystem mutations (folder creation, file creation from templates, renaming, safe deletion, and duplication).
   - Manages sorting, category filtering, search queries, and view modes without GUI dependencies, allowing 100% headless automated testing.
2. **ImGui View Panel (`eng::editor::ContentBrowserPanel`)**:
   - Implements the interactive Dear ImGui user interface.
   - Renders the interactive toolbar, clickable breadcrumb bar, directory tree, and asset items.
   - Provides Grid View and List View presentation modes.
   - Supports 3D asset drag-and-drop into the Scene Viewport (`CONTENT_BROWSER_MESH`).
   - Displays modal dialogs for creation, rename, and recursive deletion confirmation.

---

## 2. Directory Navigation & History

### Navigation Features
- **Directory Tree (Left Pane)**: Hierarchical tree view displaying directories rooted at the project asset root (`assets`). Clicking any directory immediately navigates to it.
- **Clickable Breadcrumbs**: Visual breadcrumb bar (`assets > models > characters`) where each segment is a clickable button navigating directly to that ancestor path.
- **Toolbar Navigation**:
  - `Back` (`<`): Traverses back through the history stack.
  - `Forward` (`>`): Traverses forward through the history stack.
  - `Up` (`^`): Navigates to the parent directory, stopping at the project root.
  - `Refresh` (`R`): Rescans the active directory and updates displayed items.
- **Direct Interaction**: Double-clicking any folder in either Grid View or List View immediately opens that directory.

### Security and Path Containment
- All filesystem operations are verified against the canonical project asset root via `isPathWithinRoot()`.
- Directory traversal attacks (`..`) and symlinks pointing outside the project root are rejected.
- Navigating above the project asset root is disallowed.

---

## 3. Asset Creation & Management

### A. Folder Creation
- Creates new directories within the current browsing location.
- Enforces strict name validation: rejects empty names, path separators (`/`, `\`), and illegal characters (`:`, `*`, `?`, `"`, `<`, `>`, `|`).
- Detects existing items and prevents collisions.

### B. File Creation Templates
Supports instant generation of template files:
1. **Empty File**: Creates a blank file.
2. **Text Document (`.txt`)**: Creates a text file.
3. **Lua Script (`.lua`)**: Generates an engine script with standard boilerplate hooks:
   ```lua
   -- ScriptComponent template
   function on_init()
   end

   function on_update(dt)
   end
   ```
4. **Hikari Scene (`.hikari`)**: Creates a valid minimal native `.hikari` scene JSON conforming to `framework::SceneSerializer`:
   ```json
   {
     "formatVersion": 1,
     "metadata": {
       "name": "NewScene",
       "generator": "Hikari Engine",
       "timestamp": 0
     },
     "entities": []
   }
   ```
   Verified by regression tests to deserialize cleanly into `world::World`.

### C. Renaming
- Renames files or folders in place.
- Validates target name and prevents destination collisions.

### D. Safe Deletion
- Prompts with a confirmation modal before deleting any item.
- Non-empty directories require explicit confirmation for recursive deletion.
- Deletion of the project asset root is strictly forbidden.

### E. Duplication
- Automatically generates `<name>_Copy.<ext>` or `<name>_Copy_1.<ext>` without overwriting existing files.

---

## 4. Presentation & Organization

- **Grid View**: Large card buttons with type badges, truncated labels, and double-click opening.
- **List View**: Detailed table with sortable columns: `Type`, `Name`, `Ext`, `Size`, `Modified`.
- **Sorting**: Supports sorting by `Name`, `Type`, `Size`, and `Date Modified` in either `Ascending` or `Descending` order.
- **Category Filtering**: Quick filter chips for `All`, `Meshes`, `Textures`, `Audio`, `Scripts`, and `Scenes`.
- **Search**: Instant case-insensitive substring search filter.

---

## 5. Viewport Drag-and-Drop Spawning

- Dragging any 3D asset (`.obj`, `.gltf`, `.glb`) from the Content Browser into the Scene Viewport transmits an ImGui drag-and-drop payload with type `CONTENT_BROWSER_MESH`.
- Dropping into the viewport instantiates a new entity in the active World at the camera's focus pivot, automatically attaching:
  - `physics::PhysicsTransformComponent`
  - `render::MeshComponent`
  - `physics::ColliderComponent` (auto-sized to the mesh bounds via `AssetManager`)
