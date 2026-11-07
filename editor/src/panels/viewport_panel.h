/// @file viewport_panel.h
/// @brief 3D scene viewport panel with coordinate canvas and camera diagnostics.

#pragma once

#include "editor/editor_panel.h"

namespace eng::editor {

class ViewportPanel final : public EditorPanel {
public:
    ViewportPanel() = default;
    ~ViewportPanel() override = default;

    [[nodiscard]] std::string_view getId() const noexcept override { return "viewport"; }

    [[nodiscard]] std::string_view getDisplayName() const noexcept override {
        return "Scene Viewport";
    }

    [[nodiscard]] std::string_view getCategory() const noexcept override { return "Scene"; }

    void draw(EditorContext& ctx) override;
};

} // namespace eng::editor
