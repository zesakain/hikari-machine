/// @file editor_engine.h
/// @brief Dear ImGui developer tooling and inspection workspace for Hikari.
///
/// Purpose:
/// Provides in-engine developer UI for inspecting the Shared World ECS, monitoring
/// Two-State Conductor execution, profiling frame performance, and viewing structured logs.
///
/// Invariants & Isolation:
/// - Built ONLY when HIKARI_BUILD_EDITOR=ON (EDITOR=1).
/// - Non-editor builds contain ZERO editor code or ImGui symbols.
/// - Encapsulates Dear ImGui headers entirely within private implementation.
///
/// Ownership and Lifetime:
/// Owned by the editor application. Bound non-owning references to World and Conductor.
///
/// Thread-safety:
/// Must be updated and rendered on the main application thread.

#pragma once

#include "core/time/duration.h"

#include <memory>
#include <string_view>

namespace eng::platform {
struct InputSnapshot;
class Window;
} // namespace eng::platform

namespace eng::rhi {
class RhiDevice;
class RhiCommandList;
} // namespace eng::rhi

namespace eng::world {
class World;
} // namespace eng::world

namespace eng::framework {
class Conductor;
} // namespace eng::framework

namespace eng::editor {

/// @struct EditorConfig
/// @brief Initial panel visibility and layout configuration for EditorEngine.
struct EditorConfig {
    /// Whether the Conductor runtime monitor panel is open.
    bool showConductorPanel{true};

    /// Whether the Shared World entity inspector panel is open.
    bool showEntityInspector{true};

    /// Whether the performance metrics and telemetry panel is open.
    bool showMetricsPanel{true};

    /// Whether the structured console log viewer panel is open.
    bool showConsolePanel{true};

    /// Whether the entity hierarchy panel is open.
    bool showHierarchyPanel{true};

    /// Whether the 3D scene viewport panel is open.
    bool showViewportPanel{true};

    /// Whether the project content browser panel is open.
    bool showContentBrowser{true};

    /// Whether the diagnostic "HIKARI EDITOR" banner window is open.
    bool showDiagnosticPanel{true};

    /// Whether the main toolbar is visible.
    bool showToolbar{true};

    /// Whether the Dear ImGui demo window is visible.
    bool showDemoWindow{false};
};

/// @class EditorEngine
/// @brief Primary manager for Dear ImGui context, panel rendering, and input processing.
class EditorEngine {
public:
    EditorEngine();
    ~EditorEngine();

    EditorEngine(const EditorEngine&) = delete;
    EditorEngine& operator=(const EditorEngine&) = delete;

    EditorEngine(EditorEngine&& other) noexcept;
    EditorEngine& operator=(EditorEngine&& other) noexcept;

    /// Initializes Dear ImGui context, styling, and custom RHI render pipeline.
    ///
    /// Preconditions : rhiDevice must be valid.
    /// Postconditions: ImGui context created and default font atlas built.
    bool initialize(rhi::RhiDevice& rhiDevice, const EditorConfig& config = {});

    /// Destroys ImGui context and releases GPU resources.
    void shutdown();

    /// Checks whether the editor engine is currently initialized.
    [[nodiscard]] bool isInitialized() const noexcept;

    /// Starts a new ImGui frame with the given delta time.
    void beginFrame(Duration dt);

    /// Draws all active editor panels (Conductor, World Inspector, Metrics, Console).
    void drawPanels();

    /// Finalizes the ImGui frame and records draw commands to the command list.
    void endFrame(rhi::RhiCommandList& cmdList);

    /// Translates platform input snapshot states into ImGui mouse and keyboard events.
    void processInput(const platform::InputSnapshot& input);

    /// Binds the authoritative Shared World for entity inspection.
    void bindWorld(world::World* world) noexcept;

    /// Binds the active Conductor for time state monitoring and transition triggering.
    void bindConductor(framework::Conductor* conductor) noexcept;

    /// Returns the total number of ImGui draw calls recorded in the last frame.
    [[nodiscard]] uint32_t lastDrawCallCount() const noexcept;

    /// Returns the total number of ImGui vertices rendered in the last frame.
    [[nodiscard]] uint32_t lastVertexCount() const noexcept;

    /// Returns the total number of ImGui indices uploaded in the last frame.
    [[nodiscard]] uint32_t lastIndexCount() const noexcept;

    /// Returns the cumulative number of executed editor passes.
    [[nodiscard]] uint64_t executedEditorPasses() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace eng::editor
