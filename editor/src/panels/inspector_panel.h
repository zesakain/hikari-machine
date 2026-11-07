/// @file inspector_panel.h
/// @brief Entity inspector and component editor panel.

#pragma once

#include "editor/editor_panel.h"

namespace eng::editor {

class InspectorPanel final : public EditorPanel {
public:
    InspectorPanel() = default;
    ~InspectorPanel() override = default;

    [[nodiscard]] std::string_view getId() const noexcept override { return "inspector"; }

    [[nodiscard]] std::string_view getDisplayName() const noexcept override { return "Inspector"; }

    [[nodiscard]] std::string_view getCategory() const noexcept override { return "Scene"; }

    void draw(EditorContext& ctx) override;
};

} // namespace eng::editor
