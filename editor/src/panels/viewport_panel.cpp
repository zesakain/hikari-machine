/// @file viewport_panel.cpp
/// @brief 3D scene viewport panel implementation with studio toolbar and grid canvas.

#include "panels/viewport_panel.h"

#include "editor/editor_theme.h"
#include "framework/conductor.h"

#include <cmath>
#include <imgui.h>

namespace eng::editor {

namespace {
int s_viewMode = 0;      // 0 = Lit, 1 = Wireframe, 2 = Normals
int s_transformMode = 0; // 0 = Translate, 1 = Rotate, 2 = Scale
int s_snappingMode = 2;  // 0 = Off, 1 = 0.1m, 2 = 0.5m, 3 = 1.0m
bool s_isOrtho = false;
} // namespace

void ViewportPanel::draw(EditorContext& ctx) {
    if (!isOpen_) {
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    if (ImGui::Begin(getDisplayName().data(), &isOpen_, ImGuiWindowFlags_NoScrollbar)) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 screenPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        float topBarHeight = 32.0F;
        ImVec2 canvasPos(screenPos.x, screenPos.y + topBarHeight);
        ImVec2 canvasSize(avail.x, avail.y - topBarHeight);

        // 1. Dark graphite viewport canvas background (#131722)
        drawList->AddRectFilled(canvasPos,
                                ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                                IM_COL32(19, 23, 34, 255));

        // 2. Dual-tier coordinate grid
        // Minor grid lines (24px)
        float minorStep = 24.0F;
        for (float x = 0.0F; x < canvasSize.x; x += minorStep) {
            drawList->AddLine(ImVec2(canvasPos.x + x, canvasPos.y),
                              ImVec2(canvasPos.x + x, canvasPos.y + canvasSize.y),
                              IM_COL32(28, 34, 50, 120));
        }
        for (float y = 0.0F; y < canvasSize.y; y += minorStep) {
            drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + y),
                              ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + y),
                              IM_COL32(28, 34, 50, 120));
        }

        // Major grid lines (120px)
        float majorStep = 120.0F;
        for (float x = 0.0F; x < canvasSize.x; x += majorStep) {
            drawList->AddLine(ImVec2(canvasPos.x + x, canvasPos.y),
                              ImVec2(canvasPos.x + x, canvasPos.y + canvasSize.y),
                              IM_COL32(40, 48, 70, 200), 1.2F);
        }
        for (float y = 0.0F; y < canvasSize.y; y += majorStep) {
            drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + y),
                              ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + y),
                              IM_COL32(40, 48, 70, 200), 1.2F);
        }

        // 3. Central coordinate origin crosshair
        ImVec2 center(canvasPos.x + (canvasSize.x * 0.5F), canvasPos.y + (canvasSize.y * 0.5F));
        drawList->AddLine(ImVec2(center.x - 30.0F, center.y), ImVec2(center.x + 30.0F, center.y),
                          IM_COL32(59, 130, 246, 220), 2.0F);
        drawList->AddLine(ImVec2(center.x, center.y - 30.0F), ImVec2(center.x, center.y + 30.0F),
                          IM_COL32(59, 130, 246, 220), 2.0F);

        // 4. Orientation Axes Widget in Top-Right
        ImVec2 axisCenter(canvasPos.x + canvasSize.x - 45.0F, canvasPos.y + 45.0F);
        drawList->AddCircleFilled(axisCenter, 22.0F, IM_COL32(20, 25, 35, 180));
        // X Axis (Red)
        drawList->AddLine(axisCenter, ImVec2(axisCenter.x + 18.0F, axisCenter.y),
                          IM_COL32(239, 68, 68, 255), 2.0F);
        // Y Axis (Green)
        drawList->AddLine(axisCenter, ImVec2(axisCenter.x, axisCenter.y - 18.0F),
                          IM_COL32(34, 197, 94, 255), 2.0F);
        // Z Axis (Blue, diagonal projection)
        drawList->AddLine(axisCenter, ImVec2(axisCenter.x - 12.0F, axisCenter.y + 12.0F),
                          IM_COL32(59, 130, 246, 255), 2.0F);

        // 5. Embedded Viewport Header Bar
        ImGui::SetCursorPos(ImVec2(6.0F, 4.0F));
        const char* viewModes[] = {"Lit Mode", "Wireframe", "Normals"};
        ImGui::SetNextItemWidth(100.0F);
        ImGui::Combo("##ViewModeCombo", &s_viewMode, viewModes, 3);

        ImGui::SameLine();
        if (ImGui::Button(s_isOrtho ? "Orthographic" : "Perspective")) {
            s_isOrtho = !s_isOrtho;
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        const char* gizmoModes[] = {"Translate", "Rotate", "Scale"};
        ImGui::SetNextItemWidth(100.0F);
        ImGui::Combo("##GizmoCombo", &s_transformMode, gizmoModes, 3);

        ImGui::SameLine();
        const char* snapModes[] = {"Snap: Off", "Snap: 0.1m", "Snap: 0.5m", "Snap: 1.0m"};
        ImGui::SetNextItemWidth(105.0F);
        ImGui::Combo("##SnapCombo", &s_snappingMode, snapModes, 4);

        // 6. Floating Diagnostic Card in Bottom-Left
        float cardWidth = 340.0F;
        float cardHeight = 65.0F;
        ImVec2 cardPos(canvasPos.x + 12.0F, canvasPos.y + canvasSize.y - cardHeight - 12.0F);

        drawList->AddRectFilled(cardPos, ImVec2(cardPos.x + cardWidth, cardPos.y + cardHeight),
                                IM_COL32(18, 21, 28, 220), 4.0F);
        drawList->AddRect(cardPos, ImVec2(cardPos.x + cardWidth, cardPos.y + cardHeight),
                          IM_COL32(45, 52, 68, 180), 4.0F);

        ImGui::SetCursorScreenPos(ImVec2(cardPos.x + 10.0F, cardPos.y + 8.0F));
        ImGui::TextColored(ImVec4(0.38F, 0.74F, 0.98F, 1.0F), "Resolution: %.0fx%.0f (%.2f:1)",
                           static_cast<double>(canvasSize.x), static_cast<double>(canvasSize.y),
                           canvasSize.y > 0.0F ? static_cast<double>(canvasSize.x / canvasSize.y)
                                               : 1.0);

        const char* activeMode =
            (ctx.conductor != nullptr) ? ctx.conductor->getActiveMode().getName().data() : "None";
        ImGui::SetCursorScreenPos(ImVec2(cardPos.x + 10.0F, cardPos.y + 26.0F));
        ImGui::TextDisabled("Mode: %s | Camera: %s (FOV: 60 deg)", activeMode,
                            s_isOrtho ? "Orthographic" : "Perspective");

        ImGui::SetCursorScreenPos(ImVec2(cardPos.x + 10.0F, cardPos.y + 44.0F));
        if (ctx.isPaused) {
            ImGui::TextColored(ImVec4(0.96F, 0.62F, 0.04F, 1.0F), "[PAUSED]");
        } else {
            ImGui::TextColored(ImVec4(0.06F, 0.73F, 0.51F, 1.0F), "[RUNNING]");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("| Clip: 0.1m - 1000m | Baseline Canvas");
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace eng::editor
