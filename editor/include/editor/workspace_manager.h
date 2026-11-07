/// @file workspace_manager.h
/// @brief Professional docking workspace management and layout persistence for Hikari Editor.
///
/// Purpose:
/// Manages the root dockspace canvas, panel registration, layout persistence across restarts,
/// and automated default layout synthesis using Dear ImGui DockBuilder.
///
/// Invariants & Rules:
/// - Built ONLY when HIKARI_BUILD_EDITOR=ON.
/// - Exception-free and RTTI-free.
/// - Does not write layout files every frame.
///
/// Thread Safety:
/// Main thread only.

#pragma once

#include "editor/editor_panel.h"

#include <memory>
#include <string_view>
#include <vector>

namespace eng::editor {

/// @class WorkspaceManager
/// @brief Central manager for panel registry, root dockspace, and layout serialization.
class WorkspaceManager {
public:
    WorkspaceManager();
    ~WorkspaceManager();

    WorkspaceManager(const WorkspaceManager&) = delete;
    WorkspaceManager& operator=(const WorkspaceManager&) = delete;

    WorkspaceManager(WorkspaceManager&&) noexcept;
    WorkspaceManager& operator=(WorkspaceManager&&) noexcept;

    /// Registers a new editor panel into the workspace.
    void registerPanel(std::unique_ptr<EditorPanel> panel);

    /// Finds a registered panel by its unique ID.
    [[nodiscard]] EditorPanel* findPanel(std::string_view id) const noexcept;

    /// Returns all registered panels.
    [[nodiscard]] const std::vector<std::unique_ptr<EditorPanel>>& getPanels() const noexcept;

    /// Sets the layout file path (default: "hikari_editor_layout.ini").
    void setLayoutFilePath(std::string_view path);

    /// Returns the active layout file path.
    [[nodiscard]] std::string_view getLayoutFilePath() const noexcept;

    /// Requests resetting the workspace layout to the default docked configuration.
    void requestLayoutReset() noexcept;

    /// Checks if a layout reset is currently pending.
    [[nodiscard]] bool isLayoutResetPending() const noexcept;

    /// Saves the current workspace docking layout to disk.
    bool saveLayout(const char* customPath = nullptr);

    /// Loads and restores the workspace docking layout from disk.
    /// Falls back to default layout synthesis if the file is missing or corrupted.
    bool loadLayout(const char* customPath = nullptr);

    /// Renders the root application dockspace, menu bar, toolbar, panels, and status bar.
    void drawWorkspace(EditorContext& ctx);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace eng::editor
