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
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float radius = 3.5F;
    const float lineHeight = ImGui::GetTextLineHeight();
    const float centerY = p.y + (lineHeight * 0.5F);

    drawList->AddCircleFilled(ImVec2(p.x + radius + 1.0F, centerY), radius,
                              ImGui::GetColorU32(toImVec4(color)));

    ImGui::Dummy(ImVec2((radius * 2.0F) + 5.0F, lineHeight));
    ImGui::SameLine(0.0F, 0.0F);
    ImGui::TextColored(toImVec4(color), "%s", label);
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

bool drawVec3Control(const char* label, float values[3], float resetValue, float speed,
                     float minValue, float maxValue, const bool mixed[3], bool* outModX,
                     bool* outModY, bool* outModZ) {
    bool modX = false;
    bool modY = false;
    bool modZ = false;

    ImGui::PushID(label);
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, ThemeMetrics::kPropertyLabelWidth);

    const auto& theme = getActiveThemeColors();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(toImVec4(theme.textSecondary), "%s", label);
    ImGui::NextColumn();

    float availW = ImGui::GetContentRegionAvail().x;
    float itemW = (availW - 64.0F) / 3.0F;
    if (itemW < 38.0F) {
        itemW = 38.0F;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0F, 0.0F));

    // X Axis - Red
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85F, 0.22F, 0.22F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95F, 0.30F, 0.30F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70F, 0.15F, 0.15F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 1.0F, 1.0F, 1.0F));
    if (ImGui::Button("X", ImVec2(18.0F, 0.0F))) {
        values[0] = resetValue;
        modX = true;
    }
    ImGui::PopStyleColor(4);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(itemW);
    const char* fmtX = (mixed != nullptr && mixed[0]) ? "---" : "%.2f";
    if (ImGui::DragFloat("##X", &values[0], speed, minValue, maxValue, fmtX)) {
        modX = true;
    }
    ImGui::SameLine();

    // Y Axis - Green
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.72F, 0.35F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25F, 0.82F, 0.42F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12F, 0.58F, 0.28F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 1.0F, 1.0F, 1.0F));
    if (ImGui::Button("Y", ImVec2(18.0F, 0.0F))) {
        values[1] = resetValue;
        modY = true;
    }
    ImGui::PopStyleColor(4);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(itemW);
    const char* fmtY = (mixed != nullptr && mixed[1]) ? "---" : "%.2f";
    if (ImGui::DragFloat("##Y", &values[1], speed, minValue, maxValue, fmtY)) {
        modY = true;
    }
    ImGui::SameLine();

    // Z Axis - Blue
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20F, 0.52F, 0.95F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30F, 0.62F, 0.98F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12F, 0.40F, 0.80F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 1.0F, 1.0F, 1.0F));
    if (ImGui::Button("Z", ImVec2(18.0F, 0.0F))) {
        values[2] = resetValue;
        modZ = true;
    }
    ImGui::PopStyleColor(4);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(itemW);
    const char* fmtZ = (mixed != nullptr && mixed[2]) ? "---" : "%.2f";
    if (ImGui::DragFloat("##Z", &values[2], speed, minValue, maxValue, fmtZ)) {
        modZ = true;
    }

    ImGui::PopStyleVar(); // ItemSpacing
    ImGui::Columns(1);
    ImGui::PopID();

    if (outModX != nullptr) {
        *outModX = modX;
    }
    if (outModY != nullptr) {
        *outModY = modY;
    }
    if (outModZ != nullptr) {
        *outModZ = modZ;
    }
    return modX || modY || modZ;
}

bool drawSearchField(const char* id, char* buffer, size_t bufferSize, const char* placeholder) {
    if (buffer == nullptr || bufferSize == 0) {
        return false;
    }

    ImGui::PushID(id);
    const auto& theme = getActiveThemeColors();

    float availW = ImGui::GetContentRegionAvail().x;
    bool hasText = buffer[0] != '\0';
    float inputW = hasText ? (availW - 24.0F) : availW;

    ImGui::SetNextItemWidth(inputW);
    bool changed = ImGui::InputTextWithHint("##SearchInput", placeholder, buffer, bufferSize);

    if (hasText) {
        ImGui::SameLine(0.0F, 2.0F);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toImVec4(theme.buttonHovered));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, toImVec4(theme.buttonActive));
        ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(theme.textMuted));
        if (ImGui::Button(EditorIcons::kClose, ImVec2(22.0F, 0.0F))) {
            buffer[0] = '\0';
            changed = true;
        }
        ImGui::PopStyleColor(4);
    }

    ImGui::PopID();
    return changed;
}

bool drawIconButton(const char* id, const char* icon, const char* tooltip, bool active,
                    const Color4f* customColor, float width, float height) {
    ImGui::PushID(id);
    const auto& theme = getActiveThemeColors();

    Color4f bgCol =
        active ? (customColor != nullptr ? customColor->withAlpha(0.35F) : theme.accentSubtle)
               : theme.buttonNormal;
    Color4f hoverCol =
        active ? (customColor != nullptr ? customColor->withAlpha(0.50F) : theme.accentHovered)
               : theme.buttonHovered;
    Color4f activeCol =
        active ? (customColor != nullptr ? customColor->withAlpha(0.65F) : theme.accentActive)
               : theme.buttonActive;
    Color4f textCol =
        active ? (customColor != nullptr ? *customColor : theme.accentPrimary) : theme.textPrimary;

    ImGui::PushStyleColor(ImGuiCol_Button, toImVec4(bgCol));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toImVec4(hoverCol));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, toImVec4(activeCol));
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(textCol));

    ImVec2 btnSize(width, height > 0.0F ? height : ThemeMetrics::kButtonHeightStandard);
    bool clicked = ImGui::Button(icon, btnSize);

    ImGui::PopStyleColor(4);

    if (tooltip != nullptr && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }

    ImGui::PopID();
    return clicked;
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
    Color4f txtColor =
        activeHighlight ? accentColor : (disabled ? theme.textDisabled : theme.textPrimary);

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
