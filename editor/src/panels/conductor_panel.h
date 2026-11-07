/// @file conductor_panel.h
/// @brief Two-State Conductor state machine and transition visualizer panel.

#pragma once

#include "editor/editor_panel.h"

namespace eng::editor {

class ConductorPanel final : public EditorPanel {
public:
    ConductorPanel() = default;
    ~ConductorPanel() override = default;

    [[nodiscard]] std::string_view getId() const noexcept override { return "conductor"; }

    [[nodiscard]] std::string_view getDisplayName() const noexcept override {
        return "Conductor Monitor";
    }

    [[nodiscard]] std::string_view getCategory() const noexcept override { return "Framework"; }

    void draw(EditorContext& ctx) override;
};

} // namespace eng::editor
