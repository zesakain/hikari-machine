/// @file editor_panel.h
/// @brief Panel interface and shared editor execution context.
///
/// Purpose:
/// Establishes the reusable panel contract and non-owning shared context for all
/// dockable panels in the Hikari Editor.
///
/// Invariants & Rules:
/// - Built ONLY when HIKARI_BUILD_EDITOR=ON.
/// - Exception-free and RTTI-free.
/// - Does not own World or Conductor.
///
/// Thread Safety:
/// Main thread only.

#pragma once

#include <cstdint>
#include <string_view>

namespace eng::world {
class World;
} // namespace eng::world

namespace eng::framework {
class Conductor;
} // namespace eng::framework

namespace eng::rhi {
class RhiDevice;
} // namespace eng::rhi

namespace eng::editor {

class SelectionModel;

/// @struct EditorContext
/// @brief Non-owning execution context passed to editor panels each frame.
struct EditorContext {
    world::World* world{nullptr};
    framework::Conductor* conductor{nullptr};
    rhi::RhiDevice* rhiDevice{nullptr};
    SelectionModel* selection{nullptr};

    uint64_t selectedEntityId{0};
    bool isSimulating{true};
    bool isPaused{false};

    uint32_t windowWidth{1280};
    uint32_t windowHeight{720};
    float fontScale{1.0F};

    float lastFrameTimeMs{16.6F};
    float currentFps{60.0F};

    uint32_t lastDrawCalls{0};
    uint32_t lastVertices{0};
    uint32_t lastIndices{0};
    uint64_t executedPasses{0};

    bool isSceneDirty{false};
    bool requestMarkDirty{false};

    void markDirty() noexcept { requestMarkDirty = true; }
};

/// @class EditorPanel
/// @brief Base contract for an extensible, dockable editor panel.
class EditorPanel {
public:
    virtual ~EditorPanel() = default;

    /// Unique programmatic identifier (used in dockspace and layout queries).
    [[nodiscard]] virtual std::string_view getId() const noexcept = 0;

    /// Human-readable title displayed in the panel tab and window menu.
    [[nodiscard]] virtual std::string_view getDisplayName() const noexcept = 0;

    /// Logical category for menu grouping (e.g., "General", "Scene", "Profiling").
    [[nodiscard]] virtual std::string_view getCategory() const noexcept { return "General"; }

    /// Whether this panel is currently open/visible.
    [[nodiscard]] virtual bool isOpen() const noexcept { return isOpen_; }

    /// Toggles or sets panel visibility.
    virtual void setOpen(bool open) noexcept { isOpen_ = open; }

    /// Renders panel content using Dear ImGui.
    virtual void draw(EditorContext& ctx) = 0;

protected:
    bool isOpen_{true};
};

} // namespace eng::editor
