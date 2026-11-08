/// @file ui_helpers.cpp
/// @brief Implementation of reusable UI drawing helpers and semantic design tokens.

#include "editor/ui_helpers.h"
#include "imgui_utils.h"

#include <cstdarg>
#include <cstdio>
#include <imgui_internal.h>

namespace eng::editor {

void drawBadge(const char* label, const Color4f& badgeColor, const Color4f& textColor) {
    if (label == nullptr || label[0] == '\0') {
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ThemeMetrics::kFrameRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0F, 2.0F));

    // Dark outline / background wash
    ImVec4 bgCol = toImVec4(badgeColor.withAlpha(0.25F));
    ImVec4 borderCol = toImVec4(badgeColor);
    ImVec4 txtCol = toImVec4(textColor);

    ImGui::PushStyleColor(ImGuiCol_Button, bgCol);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bgCol);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, bgCol);
    ImGui::PushStyleColor(ImGuiCol_Border, borderCol);
    ImGui::PushStyleColor(ImGuiCol_Text, txtCol);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);

    char btnId[64];
    std::snprintf(btnId, sizeof(btnId), "%s##Badge", label);
    ImGui::Button(btnId);

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(2);
}

void drawStatusPill(const char* label, const Color4f& color, bool outlineOnly) {
    if (label == nullptr) {
        return;
    }

    if (outlineOnly) {
        ImGui::TextColored(toImVec4(color), "[%s]", label);
    } else {
        ImGui::TextColored(toImVec4(color), "%s %s", EditorIcons::kDot, label);
    }
}

bool drawSectionHeader(const char* label, bool defaultOpen) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
    if (defaultOpen) {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }
    return ImGui::CollapsingHeader(label, flags);
}

void drawPropertyLabel(const char* label, const char* tooltip, float width) {
    const auto& theme = getActiveThemeColors();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(toImVec4(theme.textSecondary), "%s", label);

    if (tooltip != nullptr && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }

    ImGui::SameLine(width);
}

void drawMutedText(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    const auto& theme = getActiveThemeColors();
    ImGui::TextColored(toImVec4(theme.textMuted), "%s", buf);
}

void drawSecondaryText(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    const auto& theme = getActiveThemeColors();
    ImGui::TextColored(toImVec4(theme.textSecondary), "%s", buf);
}

void beginCard(const char* id, float width, float height) {
    const auto& theme = getActiveThemeColors();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toImVec4(theme.surfaceCard));
    ImGui::PushStyleColor(ImGuiCol_Border, toImVec4(theme.borderSubtle));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ThemeMetrics::kChildRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, ThemeMetrics::kChildBorderSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0F, 8.0F));

    ImGui::BeginChild(id, ImVec2(width, height), true, ImGuiWindowFlags_None);
}

void endCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

bool drawTransportButton(const char* icon, const char* label, const Color4f& accentColor,
                         bool disabled, bool activeHighlight) {
    const auto& theme = getActiveThemeColors();
    ImGui::BeginDisabled(disabled);

    Color4f btnBg = activeHighlight ? accentColor.withAlpha(0.35F) : theme.buttonNormal;
    Color4f btnHover = activeHighlight ? accentColor.withAlpha(0.50F) : theme.buttonHovered;
    Color4f btnActive = activeHighlight ? accentColor.withAlpha(0.65F) : theme.buttonActive;
    Color4f txtColor = activeHighlight ? accentColor : (disabled ? theme.textDisabled : theme.textPrimary);

    ImGui::PushStyleColor(ImGuiCol_Button, toImVec4(btnBg));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toImVec4(btnHover));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, toImVec4(btnActive));
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(txtColor));

    char btnText[64];
    std::snprintf(btnText, sizeof(btnText), " %s %s ", icon, label);
    bool clicked = ImGui::Button(btnText, ImVec2(0.0F, ThemeMetrics::kButtonHeightStandard));

    ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
    return clicked;
}

} // namespace eng::editor
