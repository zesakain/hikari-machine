# Hikari Editor Design System & UI/UX Architecture

## 1. Executive Summary & Visual Direction

The **Hikari Editor** is a high-performance, studio-grade 3D game engine editor designed for deep technical workflows and long development sessions. 

Rather than treating Dear ImGui as a disparate collection of raw widgets, the Hikari design system establishes a deliberate visual identity:

**"Hikari: Dark Technical Elegance"**

### Visual Principles:
- **Surface Layering**: Strict 8-level surface hierarchy (Darkest `#0F1013` $\to$ Panel `#202227` $\to$ Elevated `#2A2D36`) producing depth without garish bevels or skeuomorphism.
- **Restrained Accents**: Electric cyan (`#00D2FF` / `#38BDF8`) and cobalt blue (`#3B82F6` / `#2563EB`) used exclusively for active focus, selection, and primary highlights.
- **Information Density without Clutter**: Standard 8pt grid metrics (`ThemeMetrics`) providing consistent padding, aligned property labels, and crisp typography.
- **Semantic Color Tokens**: All colors represent explicit UI roles (surfaces, text, borders, transport, diagnostics, axes) instead of hardcoded RGBA values.
- **Dynamic Theming**: Instant runtime theme switching across **Hikari Dark**, **Hikari Midnight**, and **Hikari Light** without application restarts.

---

## 2. Pre-9A.1 UI/UX Audit Findings

Before Milestone 9A.1, an exhaustive audit of all editor panels and shell components was conducted. Key findings included:

1. **Scattered Hardcoded Colors**: Over 85 instances of inline `ImVec4(...)` calls were found across panels (`inspector_panel.cpp`, `content_browser_panel.cpp`, `console_panel.cpp`, `hierarchy_panel.cpp`, `viewport_panel.cpp`, and `workspace_manager.cpp`), leading to drift in accent colors, badge hues, and text contrast.
2. **Missing Theme Architecture**: Only a single hardcoded theme existed (`ThemeColors::defaultDark()`). There was no mechanism to switch themes dynamically or support alternate high-contrast or light environments.
3. **Inconsistent Iconography**: Transport controls and panel indicators used arbitrary, mismatched string glyphs (`[>]`, `[||]`, `[X]`, `[Tr]`, `[RB]`, `[NAV]`) rather than a unified semantic iconography system.
4. **Spacing & Padding Drift**: Menus, toolbars, and child panels used ad-hoc padding values (`(12, 7)`, `(10, 4)`, `(8, 6)`), creating subtle visual misalignments between headers, toolbars, and dockspaces.
5. **Lack of Standard Reusable Components**: Common UI patterns (format badges, status pills, section headers, property rows) were re-implemented differently in every panel.

---

## 3. Centralized UI Design Tokens

### 3.1 Color Token Structure (`Color4f`)

All color tokens are represented as normalized, float-precision POD structures (`Color4f`) encapsulating:
- `toRgba32()`: Converts normalized floats to 32-bit packed RGBA integers.
- `withAlpha(float newA)`: Returns a copy of the color with modified opacity.
- `luminance()`: Evaluates perceived ITU-R BT.709 relative luminance ($L = 0.2126R + 0.7152G + 0.0722B$).
- `isLight()`: Boolean query determining whether text should be dark or bright for optimal contrast.

### 3.2 Semantic Palette Hierarchy

```
+-----------------------------------------------------------------------+
|  Surfaces & Elevation                                                 |
|  - backgroundDarkest   : Gutter, Status Bar                           |
|  - backgroundApp       : Window Frame, Main Menu Bar, Toolbar Host    |
|  - backgroundWorkspace : DockSpace Canvas                             |
|  - backgroundPanel     : Standard Panel Bodies (Hierarchy, Inspector) |
|  - surfaceHeader       : Tabs, Collapsing Section Headers             |
|  - surfaceElevated     : Input Fields, Buttons, Slider Controls       |
|  - surfacePopup        : Modals, Popups, Context Menus, Tooltips      |
|  - surfaceCard         : Nested Component Cards                       |
|  - surfaceHighlight    : Subtle Selection & Drag Highlights           |
+-----------------------------------------------------------------------+
|  Typography & Readability                                             |
|  - textPrimary         : Primary High-Contrast Content Text           |
|  - textSecondary       : Property Labels, Metadata, Section Headers   |
|  - textMuted           : Secondary Hints, Placeholders, Empty States  |
|  - textDisabled        : Disabled Items, Inactive Shortcuts           |
|  - textInverse         : Contrast Text on Saturated Accents           |
+-----------------------------------------------------------------------+
|  Interactive & Accents                                                |
|  - accentPrimary       : Active Focus, Selected Tabs, Primary Action  |
|  - accentHovered       : Subtle Accent Glow on Hover                  |
|  - accentActive        : Deep Pressed State                           |
|  - accentSubtle        : 15% Opacity Wash for Selection Highlights    |
|  - selectionActive     : Multi-Select Entity Highlighting             |
+-----------------------------------------------------------------------+
|  Simulation Transport                                                 |
|  - transportPlay       : Emerald Green (#10B981)                      |
|  - transportPause      : Warm Amber (#F59E0B)                         |
|  - transportResume     : Golden Yellow (#EAB308)                      |
|  - transportStep       : Sky Blue (#38BDF8)                           |
|  - transportStop       : Crimson Red (#EF4444)                        |
+-----------------------------------------------------------------------+
```

---

## 4. Layout Metrics & Spacing Scale (`ThemeMetrics`)

The editor adheres to an **8-point grid** with consistent scaling tokens:

| Token | Value | Semantic Usage |
| :--- | :--- | :--- |
| `kToolbarHeight` | `44.0px` | Main editor simulation & layout toolbar |
| `kStatusBarHeight` | `26.0px` | Bottom telemetry & runtime status bar |
| `kMenuBarHeight` | `28.0px` | Top desktop application menu bar |
| `kButtonHeightStandard` | `26.0px` | Standard buttons and transport controls |
| `kButtonHeightCompact` | `22.0px` | Small inline buttons (add component, toggles) |
| `kPropertyLabelWidth` | `120.0px` | Aligned inspector property name column |
| `kSpacingXXS` | `2.0px` | Micro separation |
| `kSpacingXS` | `4.0px` | Inner button & cell padding |
| `kSpacingS` | `6.0px` | Standard item spacing |
| `kSpacingM` | `8.0px` | Base grid unit (card padding, frame padding) |
| `kSpacingL` | `12.0px` | Toolbar padding, section gap |
| `kSpacingXL` | `16.0px` | Panel gutters, indent spacing |
| `kSpacingXXL` | `24.0px` | Major workspace separators |

Corner radii are standardized to maintain a modern, technical aesthetic:
- Windows: `0.0px` (seamless docking).
- Cards & Frames: `3.0px`.
- Popups & Dialogs: `4.0px`.
- Tabs: `3.0px`.

---

## 5. Theme Presets & Dynamic Switching

Hikari provides three built-in theme presets:

### 5.1 Hikari Dark (Default)
- **Base Surfaces**: Graphite (`#151619`) and charcoal (`#202227`).
- **Accents**: Cobalt electric blue (`#3B82F6`) and bright cyan highlights.
- **Text**: Off-white primary text (`#F3F4F6`, $L = 0.95$) with muted secondary metadata (`#9CA3AF`).
- **Target Workflow**: Balanced standard studio development for low-to-medium ambient light.

### 5.2 Hikari Midnight
- **Base Surfaces**: Deep obsidian (`#090B0E`) and OLED charcoal (`#12151B`).
- **Accents**: Luminous electric cyan (`#38BDF8`).
- **Text**: High-contrast cool white (`#F0F6FC`).
- **Target Workflow**: Dark-room programming, shader debugging, and high-contrast focus.

### 5.3 Hikari Light
- **Base Surfaces**: Clean studio slate (`#EAEBEF`) and warm paper (`#F4F5F8`).
- **Accents**: Saturated royal cobalt (`#2563EB`).
- **Text**: Deep charcoal typography (`#111827`, $L = 0.08$) ensuring high readability.
- **Target Workflow**: Bright ambient environments, presentations, and documentation reviews.

### 5.4 Dynamic Switching Architecture
Themes are managed via `editor/include/editor/editor_theme.h`:
```cpp
void applyEditorTheme(ThemeType type);
void applyEditorTheme(const ThemeColors& colors);
ThemeType getActiveThemeType() noexcept;
const ThemeColors& getActiveThemeColors() noexcept;
```
Theme changes execute immediately by reconfiguring `ImGuiStyle` colors and spacing in-place without invalidating Vulkan textures, swapchains, or docking trees.

---

## 6. Standardized Iconography (`EditorIcons`)

Rather than arbitrary ASCII text strings, the design system defines standardized UTF-8 / ASCII icon glyphs in `editor/include/editor/ui_helpers.h`:

- **Simulation Transport**: `kPlay` (`▶`), `kPause` (`⏸`), `kResume` (`⏩`), `kStep` (`⏭`), `kStop` (`⏹`).
- **Viewport & Camera**: `kCamera` (`🎥`), `kOrbit` (`↺`), `kFly` (`✈`), `kGpu` (`⚡`), `kCpu` (`💻`), `kGrid` (`⊞`).
- **Content & Assets**: `kFolder` (`📁`), `kFile` (`📄`), `kMesh` (`🧊`), `kScene` (`🎬`), `kTexture` (`🖼`), `kScript` (`📜`), `kAudio` (`🔊`), `kPhysics` (`⚛`).
- **Actions**: `kAdd` (`+`), `kTrash` (`🗑`), `kClose` (`✕`), `kSearch` (`🔍`), `kSettings` (`⚙`), `kSave` (`💾`), `kReset` (`⟲`), `kEyeOpen` (`👁`), `kEyeClosed` (`⊘`).
- **Status Indicators**: `kDot` (`●`), `kCheck` (`✓`), `kInfo` (`ℹ`), `kWarning` (`⚠`), `kError` (`✖`).

---

## 7. Semantic UI Helpers (`editor/include/editor/ui_helpers.h`)

To eliminate boilerplate and prevent color drift, reusable helper functions provide standard rendering patterns:

- `drawBadge(label, badgeColor, textColor)`: Renders rounded format badges with subtle 25% background wash and 1px border.
- `drawStatusPill(label, color, outlineOnly)`: Displays a colored status indicator dot (`●`) paired with state text.
- `drawSectionHeader(label, defaultOpen)`: Formats collapsing section headers with full-width clickable banners.
- `drawPropertyLabel(label, tooltip, width)`: Formats property names aligned to `ThemeMetrics::kPropertyLabelWidth` with hover tooltips.
- `drawTransportButton(icon, label, accentColor, disabled, activeHighlight)`: Renders simulation transport buttons with active color highlighting.
- `beginCard(id, width, height)` / `endCard()`: Wraps component properties inside distinct rounded cards.

---

## 8. Main Editor Shell Integration

The centralized design system was applied to the core editor shell in `editor/src/workspace_manager.cpp`:

1. **Main Menu Bar**:
   - Styled with `ThemeColors::backgroundApp` and `ThemeMetrics::kFramePaddingY`.
   - Added `Window -> Theme` submenu with live toggles for **Hikari Dark**, **Hikari Midnight**, and **Hikari Light**.
2. **Toolbar**:
   - Uses `toImVec4(theme.backgroundApp)` and `ThemeMetrics::kSpacingL` padding.
   - Simulation controls use `drawTransportButton` with color-coded Emerald (`Play`), Amber (`Pause`), Yellow (`Resume`), Sky Blue (`Step`), and Crimson (`Stop`).
   - Transport state indicated via `drawStatusPill` (`[● PLAYING]`, `[● PAUSED]`, `[● EDIT MODE]`).
   - Active Two-State Conductor slot displayed using format badge with quick-switch button.
   - Active scene name displayed in header badge with dirty asterisk (`*`) indicator.
3. **Status Bar**:
   - Positioned at window bottom using `ThemeColors::backgroundDarkest`.
   - Runtime state displayed using status pill.
   - Selected entity count and name displayed using theme typography tokens.
   - Engine telemetry (Vulkan 1.3 GPU device name, viewport dimensions, FPS, and frame time) displayed cleanly.
