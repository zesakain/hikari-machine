/// @file imgui_utils.h
/// @brief Internal ImGui conversion utilities and helper functions for editor implementation.

#pragma once

#include "editor/editor_theme.h"
#include <imgui.h>

namespace eng::editor {

/// Converts Color4f design token to ImVec4.
[[nodiscard]] inline ImVec4 toImVec4(const Color4f& c) noexcept {
    return ImVec4(c.r, c.g, c.b, c.a);
}

} // namespace eng::editor
