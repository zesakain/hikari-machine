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

class WorkspaceManager;

/// @struct EditorConfig
/// @brief Initial panel visibility and layout configuration for EditorEngine.
struct EditorConfig {
    /// Path to the workspace layout file (default: "hikari_editor_layout.ini").
    std::string_view layoutFilePath{"hikari_editor_layout.ini"};

    /// Global UI font scale factor (default: 1.0).
    float fontScale{1.0F};

    /// Whether to automatically save workspace layout on shutdown.
    bool autoSaveLayout{true};

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

    /// Starts a new ImGui frame with the given delta time and physical window dimensions.
    void beginFrame(Duration dt, uint32_t width = 0, uint32_t height = 0);

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

    /// Returns the workspace manager governing panels and docking layout.
    [[nodiscard]] WorkspaceManager& getWorkspaceManager() noexcept;

    /// Returns the workspace manager governing panels and docking layout (const).
    [[nodiscard]] const WorkspaceManager& getWorkspaceManager() const noexcept;

    /// Saves the current workspace docking layout to disk.
    bool saveWorkspaceLayout(const char* filepath = nullptr);

    /// Restores the workspace docking layout from disk.
    bool loadWorkspaceLayout(const char* filepath = nullptr);

    /// Resets the workspace docking layout to the default professional configuration.
    void resetWorkspaceLayout() noexcept;

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
