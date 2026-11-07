/// @file hierarchy_panel.h
/// @brief Scene hierarchy panel for inspecting and creating entities in the Shared World.

#pragma once

#include "editor/editor_panel.h"

#include <array>

namespace eng::editor {

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

private:
    std::array<char, 64> searchFilter_{};
};

} // namespace eng::editor
