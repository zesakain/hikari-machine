# Viewport & Camera System Architecture (Milestone 6)

## 1. Overview & Purpose

The Hikari Scene Viewport and Camera System provides an interactive, perspective 3D editing workspace integrated directly into the Hikari Editor.

Key architectural boundaries:
- **Decoupled from Scene ECS**: The `EditorCamera` is an editor authoring utility and is **never** added as an ECS entity to the scene World or saved into `.hikari` scene files.
- **Hardware & Driver Neutrality**: The perspective 3D projection pipeline renders cleanly into Dear ImGui draw lists with pixel-perfect viewport panel clipping and aspect ratio updates, running identically in Vulkan and headless / Null RHI modes.
- **World & Simulation Lifecycle Awareness**: The viewport renders the active simulation world (`EditWorld` in Edit mode, `RuntimeWorld` during Playing/Paused simulation), while selection handles are automatically cleared across simulation transitions to prevent dangling references.

---

## 2. Editor Camera Mathematics

The `eng::editor::EditorCamera` ([`editor/include/editor/editor_camera.h`](file:///home/lux/Hikari/editor/include/editor/editor_camera.h)) manages:
- **Position & Pivot**: `position_`, `pivot_`, `distance_`.
- **Orientation**: `yaw_` and `pitch_` (clamped to $[-89^\circ, +89^\circ]$ to prevent gimbal lock singularities).
- **Vectors**:
  $$\text{Forward} = \begin{pmatrix} \cos(\text{pitch}) \sin(\text{yaw}) \\ \sin(\text{pitch}) \\ -\cos(\text{pitch}) \cos(\text{yaw}) \end{pmatrix}, \quad \text{Right} = \frac{\text{Forward} \times (0, 1, 0)}{\|\text{Forward} \times (0, 1, 0)\|}, \quad \text{Up} = \text{Right} \times \text{Forward}$$
- **Projections**:
  - **Perspective (Vulkan $[0, 1]$ Depth)**:
    $$f = \frac{1}{\tan(\text{fovY} / 2)}, \quad P = \begin{pmatrix} f / \text{aspect} & 0 & 0 & 0 \\ 0 & f & 0 & 0 \\ 0 & 0 & \frac{zFar}{zNear - zFar} & \frac{zNear \cdot zFar}{zNear - zFar} \\ 0 & 0 & -1 & 0 \end{pmatrix}$$
  - **Orthographic**:
    $$P_{ortho} = \begin{pmatrix} \frac{2}{R - L} & 0 & 0 & -\frac{R + L}{R - L} \\ 0 & \frac{2}{T - B} & 0 & -\frac{T + B}{T - B} \\ 0 & 0 & \frac{1}{zNear - zFar} & \frac{zNear}{zNear - zFar} \\ 0 & 0 & 0 & 1 \end{pmatrix}$$
- **View Matrix**:
  $$V = \text{lookAt}(\text{position}, \text{position} + \text{Forward}, (0, 1, 0))$$

---

## 3. Viewport Navigation Controls

| Action | Control | Description |
| :--- | :--- | :--- |
| **Orbit** | `Alt` + Left Click Drag (or Middle Click Drag) | Rotates camera around the pivot point. Preserves stable up direction. |
| **Pan** | `Shift` + Middle Click Drag (or `Shift` + Right Click Drag) | Translates camera and pivot along the camera's local Right and Up plane. |
| **Zoom** | Mouse Wheel Scroll (or `Alt` + Right Click Drag) | Exponential distance zoom, clamped to $[0.2, 1000]\text{m}$. |
| **Fly Mode (FPS)** | Hold Right Click inside Viewport | Free-look camera: Mouse controls look; `W`/`S` forward/back, `A`/`D` left/right, `Q`/`E` down/up; `Shift` 2.5x speed boost. |
| **Focus / Frame** | `F` | Immediately frames and centers on the selected entity or world origin. |
| **Gizmo Translate** | `W` | Switches active gizmo to Translate mode. |
| **Gizmo Rotate** | `E` | Switches active gizmo to Rotate mode. |
| **Gizmo Scale** | `R` | Switches active gizmo to Scale mode. |
| **Toggle Grid** | `G` | Toggles 3D ground grid visibility. |

---

## 4. Entity Ray-Picking & Transform Gizmos

### 4.1 CPU Ray-Picking
Mouse screen position $(x, y)$ is converted to Normalized Device Coordinates (NDC) in $[-1, 1]$:
$$x_{ndc} = \frac{2(x - x_0)}{W} - 1, \quad y_{ndc} = 1 - \frac{2(y - y_0)}{H}$$
The near plane point $(x_{ndc}, y_{ndc}, 0)$ and far plane point $(x_{ndc}, y_{ndc}, 1)$ are multiplied by $VP^{-1}$ and perspective-divided by $w$ to form a world-space `Ray`:
$$\vec{O} = P_{near}, \quad \vec{D} = \text{normalize}(P_{far} - P_{near})$$
Ray intersections are tested against entity bounding volumes:
- Box colliders / OBB: Ray is transformed into entity local coordinate space via $\text{rotation}^{-1}$ and tested against $[-\text{halfExtents}, +\text{halfExtents}]$.
- Spheres: Analytic ray-sphere intersection.
- Closest entity ($t_{\min} > 0$) is selected via `SelectionModel`. Clicking empty space clears selection.

### 4.2 Interactive Transform Gizmos
- Displayed when an entity with `PhysicsTransformComponent` is selected.
- Axis handles (Red $+X$, Green $+Y$, Blue $+Z$) highlight in bright gold when hovered.
- Dragging projects mouse motion along the constrained 3D axis vector, applies optional snapping (0.1m, 0.5m, 1.0m; 15°; 0.25), and updates `PhysicsTransformComponent::transform` on all selected entities while marking the scene dirty.
- Supports both **World** and **Local** coordinate spaces.
