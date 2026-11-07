/// @file editor_theme.cpp
/// @brief Theme application and design tokens implementation for Dear ImGui.

#include "editor/editor_theme.h"

#include <imgui.h>

namespace eng::editor {

namespace {
ThemeColors s_activeColors = ThemeColors::defaultDark();

inline ImVec4 toImVec4(const Color4f& c) noexcept {
    return ImVec4(c.r, c.g, c.b, c.a);
}
} // namespace

void applyEditorTheme(const ThemeColors& colors) {
    s_activeColors = colors;

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* imColors = style.Colors;

    // Base surface & text
    imColors[ImGuiCol_Text] = toImVec4(colors.textPrimary);
    imColors[ImGuiCol_TextDisabled] = toImVec4(colors.textDisabled);
    imColors[ImGuiCol_WindowBg] = toImVec4(colors.backgroundPanel);
    imColors[ImGuiCol_ChildBg] = toImVec4(colors.backgroundWorkspace);
    imColors[ImGuiCol_PopupBg] = toImVec4(colors.surfacePopup);

    // Borders & dividers
    imColors[ImGuiCol_Border] = toImVec4(colors.borderSubtle);
    imColors[ImGuiCol_BorderShadow] = ImVec4(0.0F, 0.0F, 0.0F, 0.0F);

    // Frame inputs (InputText, Checkbox, Slider, etc.)
    imColors[ImGuiCol_FrameBg] = toImVec4(colors.buttonNormal);
    imColors[ImGuiCol_FrameBgHovered] = toImVec4(colors.buttonHovered);
    imColors[ImGuiCol_FrameBgActive] = toImVec4(colors.buttonActive);

    // Title bar & headers
    imColors[ImGuiCol_TitleBg] = toImVec4(colors.backgroundApp);
    imColors[ImGuiCol_TitleBgActive] = toImVec4(colors.surfaceElevated);
    imColors[ImGuiCol_TitleBgCollapsed] = toImVec4(colors.backgroundApp);
    imColors[ImGuiCol_MenuBarBg] = toImVec4(colors.backgroundApp);

    // Scrollbars & grabs
    imColors[ImGuiCol_ScrollbarBg] = toImVec4(colors.backgroundApp);
    imColors[ImGuiCol_ScrollbarGrab] = toImVec4(colors.surfaceElevated);
    imColors[ImGuiCol_ScrollbarGrabHovered] = toImVec4(colors.buttonHovered);
    imColors[ImGuiCol_ScrollbarGrabActive] = toImVec4(colors.buttonActive);

    // Interactive controls & selections
    imColors[ImGuiCol_CheckMark] = toImVec4(colors.accentPrimary);
    imColors[ImGuiCol_SliderGrab] = toImVec4(colors.accentPrimary);
    imColors[ImGuiCol_SliderGrabActive] = toImVec4(colors.accentHovered);

    imColors[ImGuiCol_Button] = toImVec4(colors.buttonNormal);
    imColors[ImGuiCol_ButtonHovered] = toImVec4(colors.buttonHovered);
    imColors[ImGuiCol_ButtonActive] = toImVec4(colors.buttonActive);

    imColors[ImGuiCol_Header] = toImVec4(colors.surfaceElevated);
    imColors[ImGuiCol_HeaderHovered] = toImVec4(colors.buttonHovered);
    imColors[ImGuiCol_HeaderActive] = toImVec4(colors.accentPrimary);

    imColors[ImGuiCol_Separator] = toImVec4(colors.borderSubtle);
    imColors[ImGuiCol_SeparatorHovered] = toImVec4(colors.accentPrimary);
    imColors[ImGuiCol_SeparatorActive] = toImVec4(colors.accentHovered);

    imColors[ImGuiCol_ResizeGrip] = ImVec4(0.0F, 0.0F, 0.0F, 0.0F);
    imColors[ImGuiCol_ResizeGripHovered] = toImVec4(colors.accentPrimary);
    imColors[ImGuiCol_ResizeGripActive] = toImVec4(colors.accentHovered);

    // Docking & tabs
    imColors[ImGuiCol_Tab] = toImVec4(colors.surfaceElevated);
    imColors[ImGuiCol_TabHovered] = toImVec4(colors.buttonHovered);
    imColors[ImGuiCol_TabActive] = toImVec4(colors.backgroundPanel);
    imColors[ImGuiCol_TabUnfocused] = toImVec4(colors.backgroundApp);
    imColors[ImGuiCol_TabUnfocusedActive] = toImVec4(colors.surfaceElevated);

#if defined(ImGuiConfigFlags_DockingEnable)
    imColors[ImGuiCol_DockingPreview] =
        ImVec4(colors.accentPrimary.r, colors.accentPrimary.g, colors.accentPrimary.b, 0.35F);
    imColors[ImGuiCol_DockingEmptyBg] = toImVec4(colors.backgroundWorkspace);
#endif

    // Plots & highlights
    imColors[ImGuiCol_PlotLines] = toImVec4(colors.statusInfo);
    imColors[ImGuiCol_PlotLinesHovered] = toImVec4(colors.accentHovered);
    imColors[ImGuiCol_PlotHistogram] = toImVec4(colors.accentPrimary);
    imColors[ImGuiCol_PlotHistogramHovered] = toImVec4(colors.accentHovered);
    imColors[ImGuiCol_TextSelectedBg] = toImVec4(colors.selectionActive);
    imColors[ImGuiCol_NavHighlight] = toImVec4(colors.borderFocus);

    // Spacing and sizing tokens (restrained modern desktop aesthetic)
    style.WindowRounding = 0.0F; // Flat dockable panels
    style.ChildRounding = 2.0F;
    style.FrameRounding = 3.0F;
    style.PopupRounding = 4.0F;
    style.ScrollbarRounding = 4.0F;
    style.GrabRounding = 2.0F;
    style.TabRounding = 3.0F;

    style.WindowBorderSize = 1.0F;
    style.ChildBorderSize = 1.0F;
    style.PopupBorderSize = 1.0F;
    style.FrameBorderSize = 1.0F;
    style.TabBorderSize = 0.0F;

    style.WindowPadding = ImVec2(8.0F, 8.0F);
    style.FramePadding = ImVec2(6.0F, 4.0F);
    style.ItemSpacing = ImVec2(8.0F, 6.0F);
    style.ItemInnerSpacing = ImVec2(6.0F, 4.0F);
    style.CellPadding = ImVec2(6.0F, 4.0F);
    style.IndentSpacing = 16.0F;
    style.ScrollbarSize = 12.0F;
    style.GrabMinSize = 10.0F;
}

const ThemeColors& getActiveThemeColors() noexcept {
    return s_activeColors;
}

} // namespace eng::editor
