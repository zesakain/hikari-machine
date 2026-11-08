/// @file viewport_panel.h
/// @brief 3D scene viewport panel with coordinate canvas and camera diagnostics.

#pragma once

#include "editor/editor_camera.h"
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

    [[nodiscard]] EditorCamera& getCamera() noexcept { return camera_; }
    [[nodiscard]] const EditorCamera& getCamera() const noexcept { return camera_; }

private:
    EditorCamera camera_{};
};

} // namespace eng::editor
