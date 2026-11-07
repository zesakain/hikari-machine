/// @file console_panel.h
/// @brief Structured log console panel for viewing and filtering engine messages.

#pragma once

#include "editor/editor_panel.h"

#include <array>

namespace eng::editor {

class ConsolePanel final : public EditorPanel {
public:
    ConsolePanel() = default;
    ~ConsolePanel() override = default;

    [[nodiscard]] std::string_view getId() const noexcept override { return "console"; }

    [[nodiscard]] std::string_view getDisplayName() const noexcept override { return "Console"; }

    [[nodiscard]] std::string_view getCategory() const noexcept override { return "Diagnostics"; }

    void draw(EditorContext& ctx) override;

private:
    std::array<char, 64> filterBuffer_{};
    int selectedLogLevel_{0}; // 0 = All, 1 = Info, 2 = Warning, 3 = Error+
    bool autoScroll_{true};
};

} // namespace eng::editor
