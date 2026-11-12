/// @file metrics_panel.h
/// @brief Performance profiler and engine telemetry panel.

#pragma once

#include "editor/editor_panel.h"

#include <array>

namespace eng::editor {

class MetricsPanel final : public EditorPanel {
public:
    MetricsPanel() = default;
    ~MetricsPanel() override = default;

    [[nodiscard]] std::string_view getId() const noexcept override { return "metrics"; }

    [[nodiscard]] std::string_view getDisplayName() const noexcept override {
        return "Profiler & Telemetry";
    }

    [[nodiscard]] std::string_view getCategory() const noexcept override { return "Diagnostics"; }

    void draw(EditorContext& ctx) override;

private:
    std::array<float, 120> fallbackHistory_{};
    size_t fallbackIndex_{0};
};

} // namespace eng::editor
