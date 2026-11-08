/// @file editor_theme.cpp
/// @brief Theme application, design tokens, and desktop typography implementation for Dear ImGui.

#include "editor/editor_theme.h"

#include "core/log.h"

#include <fstream>
#include <imgui.h>

namespace eng::editor {

namespace {

ThemeType s_activeThemeType = ThemeType::Dark;
ThemeColors s_activeColors = ThemeColors::hikariDark();

inline ImVec4 toImVec4(const Color4f& c) noexcept {
    return ImVec4(c.r, c.g, c.b, c.a);
}

} // namespace

void applyEditorTheme(ThemeType type) {
    s_activeThemeType = type;
    switch (type) {
    case ThemeType::Midnight:
        applyEditorTheme(ThemeColors::hikariMidnight());
        break;
    case ThemeType::Light:
        applyEditorTheme(ThemeColors::hikariLight());
        break;
    case ThemeType::Dark:
    default:
        applyEditorTheme(ThemeColors::hikariDark());
        break;
    }
}

void applyEditorTheme(const ThemeColors& colors) {
    s_activeColors = colors;

    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }

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
    imColors[ImGuiCol_TitleBgActive] = toImVec4(colors.surfaceHeader);
    imColors[ImGuiCol_TitleBgCollapsed] = toImVec4(colors.backgroundApp);
    imColors[ImGuiCol_MenuBarBg] = toImVec4(colors.backgroundApp);

    // Scrollbars & grabs
    imColors[ImGuiCol_ScrollbarBg] = toImVec4(colors.backgroundDarkest);
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

    imColors[ImGuiCol_Header] = toImVec4(colors.surfaceHeader);
    imColors[ImGuiCol_HeaderHovered] = toImVec4(colors.buttonHovered);
    imColors[ImGuiCol_HeaderActive] = toImVec4(colors.accentPrimary);

    imColors[ImGuiCol_Separator] = toImVec4(colors.borderSubtle);
    imColors[ImGuiCol_SeparatorHovered] = toImVec4(colors.accentPrimary);
    imColors[ImGuiCol_SeparatorActive] = toImVec4(colors.accentHovered);

    imColors[ImGuiCol_ResizeGrip] = ImVec4(0.0F, 0.0F, 0.0F, 0.0F);
    imColors[ImGuiCol_ResizeGripHovered] = toImVec4(colors.accentPrimary);
    imColors[ImGuiCol_ResizeGripActive] = toImVec4(colors.accentHovered);

    // Docking & tabs
    imColors[ImGuiCol_Tab] = toImVec4(colors.surfaceHeader);
    imColors[ImGuiCol_TabHovered] = toImVec4(colors.buttonHovered);
    imColors[ImGuiCol_TabActive] = toImVec4(colors.backgroundPanel);
    imColors[ImGuiCol_TabUnfocused] = toImVec4(colors.backgroundApp);
    imColors[ImGuiCol_TabUnfocusedActive] = toImVec4(colors.surfaceHeader);

#if defined(ImGuiConfigFlags_DockingEnable)
    imColors[ImGuiCol_DockingPreview] =
        ImVec4(colors.accentPrimary.r, colors.accentPrimary.g, colors.accentPrimary.b, 0.40F);
    imColors[ImGuiCol_DockingEmptyBg] = toImVec4(colors.backgroundWorkspace);
#endif

    // Tables
    imColors[ImGuiCol_TableHeaderBg] = toImVec4(colors.surfaceHeader);
    imColors[ImGuiCol_TableBorderStrong] = toImVec4(colors.borderMedium);
    imColors[ImGuiCol_TableBorderLight] = toImVec4(colors.borderSubtle);
    imColors[ImGuiCol_TableRowBg] = ImVec4(0.0F, 0.0F, 0.0F, 0.0F);
    imColors[ImGuiCol_TableRowBgAlt] = toImVec4(colors.surfaceHeader);

    // Plots & highlights
    imColors[ImGuiCol_PlotLines] = toImVec4(colors.statusInfo);
    imColors[ImGuiCol_PlotLinesHovered] = toImVec4(colors.accentHovered);
    imColors[ImGuiCol_PlotHistogram] = toImVec4(colors.accentPrimary);
    imColors[ImGuiCol_PlotHistogramHovered] = toImVec4(colors.accentHovered);
    imColors[ImGuiCol_TextSelectedBg] = toImVec4(colors.selectionActive);
    imColors[ImGuiCol_NavHighlight] = toImVec4(colors.borderFocus);

    // Spacing and sizing tokens (professional studio desktop aesthetic)
    style.WindowRounding = ThemeMetrics::kWindowRounding;
    style.ChildRounding = ThemeMetrics::kChildRounding;
    style.FrameRounding = ThemeMetrics::kFrameRounding;
    style.PopupRounding = ThemeMetrics::kPopupRounding;
    style.ScrollbarRounding = ThemeMetrics::kScrollbarRounding;
    style.GrabRounding = ThemeMetrics::kGrabRounding;
    style.TabRounding = ThemeMetrics::kTabRounding;

    style.WindowBorderSize = ThemeMetrics::kWindowBorderSize;
    style.ChildBorderSize = ThemeMetrics::kChildBorderSize;
    style.PopupBorderSize = ThemeMetrics::kPopupBorderSize;
    style.FrameBorderSize = ThemeMetrics::kFrameBorderSize;
    style.TabBorderSize = ThemeMetrics::kTabBorderSize;

    style.WindowPadding = ImVec2(10.0F, 10.0F);
    style.FramePadding = ImVec2(8.0F, 5.0F);
    style.ItemSpacing = ImVec2(8.0F, 6.0F);
    style.ItemInnerSpacing = ImVec2(6.0F, 4.0F);
    style.CellPadding = ImVec2(6.0F, 4.0F);
    style.IndentSpacing = 16.0F;
    style.ScrollbarSize = 12.0F;
    style.GrabMinSize = 10.0F;
}

ThemeType getActiveThemeType() noexcept {
    return s_activeThemeType;
}

const ThemeColors& getActiveThemeColors() noexcept {
    return s_activeColors;
}

const char* getThemeName(ThemeType type) noexcept {
    switch (type) {
    case ThemeType::Dark:
        return "Hikari Dark";
    case ThemeType::Midnight:
        return "Hikari Midnight";
    case ThemeType::Light:
        return "Hikari Light";
    default:
        return "Unknown";
    }
}

bool loadEditorFonts(float fontScale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    const float baseFontSize = 15.0F * (fontScale > 0.0F ? fontScale : 1.0F);

    const char* candidatePaths[] = {
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };

    for (const char* path : candidatePaths) {
        std::ifstream f(path);
        if (f.good()) {
            ImFont* font = io.Fonts->AddFontFromFileTTF(path, baseFontSize);
            if (font != nullptr) {
                ENG_LOG_INFO("Editor", "Loaded desktop UI font from system");
                return true;
            }
        }
    }

    io.Fonts->AddFontDefault();
    ENG_LOG_WARN("Editor", "No system TTF fonts found; using default Dear ImGui bitmap font");
    return false;
}

} // namespace eng::editor
