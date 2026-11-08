/// @file hierarchy_panel.h
/// @brief Scene hierarchy panel for inspecting and creating entities in the Shared World.

#pragma once

#include "editor/editor_panel.h"
#include "world/entity.h"

#include <array>
#include <string_view>

namespace eng::editor {

enum class ComponentFilter : uint8_t { All = 0, Transform = 1, Physics = 2, Script = 3 };

class HierarchyPanel final : public EditorPanel {
public:
    HierarchyPanel() = default;
    ~HierarchyPanel() override = default;

    [[nodiscard]] std::string_view getId() const noexcept override { return "hierarchy"; }

    [[nodiscard]] std::string_view getDisplayName() const noexcept override {
        return "Scene Hierarchy";
    }

    [[nodiscard]] std::string_view getCategory() const noexcept override { return "Scene"; }

    void draw(EditorContext& ctx) override;

    /// Initiates an inline rename for an entity.
    void startRenaming(world::EntityHandle entity, std::string_view currentName);

    /// Commits the active inline rename if valid.
    void commitRename(EditorContext& ctx);

    /// Cancels the active inline rename.
    void cancelRename() noexcept;

private:
    std::array<char, 64> searchFilter_{};
    ComponentFilter componentFilter_{ComponentFilter::All};
    world::EntityHandle renamingEntity_{};
    std::array<char, 64> renameBuffer_{};
    bool focusRenameInput_{false};
};

} // namespace eng::editor
