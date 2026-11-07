/// @file viewport_panel.cpp
/// @brief 3D scene viewport panel implementation.

#include "panels/viewport_panel.h"

#include "framework/conductor.h"

#include <imgui.h>

namespace eng::editor {

void ViewportPanel::draw(EditorContext& ctx) {
    if (!isOpen_) {
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    if (ImGui::Begin(getDisplayName().data(), &isOpen_, ImGuiWindowFlags_NoScrollbar)) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 screenPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // 1. Dark viewport canvas background (#141926)
        drawList->AddRectFilled(screenPos, ImVec2(screenPos.x + avail.x, screenPos.y + avail.y),
                                IM_COL32(20, 25, 36, 255));

        // 2. Coordinate grid lines
        float step = 32.0F;
        for (float x = 0.0F; x < avail.x; x += step) {
            drawList->AddLine(ImVec2(screenPos.x + x, screenPos.y),
                              ImVec2(screenPos.x + x, screenPos.y + avail.y),
                              IM_COL32(32, 40, 56, 180));
        }
        for (float y = 0.0F; y < avail.y; y += step) {
            drawList->AddLine(ImVec2(screenPos.x, screenPos.y + y),
                              ImVec2(screenPos.x + avail.x, screenPos.y + y),
                              IM_COL32(32, 40, 56, 180));
        }

        // 3. Central coordinate origin crosshair
        ImVec2 center(screenPos.x + (avail.x * 0.5F), screenPos.y + (avail.y * 0.5F));
        drawList->AddLine(ImVec2(center.x - 24.0F, center.y), ImVec2(center.x + 24.0F, center.y),
                          IM_COL32(59, 130, 246, 220), 2.0F);
        drawList->AddLine(ImVec2(center.x, center.y - 24.0F), ImVec2(center.x, center.y + 24.0F),
                          IM_COL32(59, 130, 246, 220), 2.0F);

        // 4. Viewport Toolbar HUD
        ImGui::SetCursorPos(ImVec2(12.0F, 10.0F));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.38F, 0.74F, 0.98F, 1.0F));
        ImGui::Text("HIKARI 3D SCENE VIEWPORT");
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("Resolution: %.0fx%.0f (%.2f:1)", static_cast<double>(avail.x),
                    static_cast<double>(avail.y),
                    avail.y > 0.0F ? static_cast<double>(avail.x / avail.y) : 1.0);

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        const char* activeMode =
            (ctx.conductor != nullptr) ? ctx.conductor->getActiveMode().getName().data() : "None";
        ImGui::Text("Active Mode: %s", activeMode);

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ctx.isPaused) {
            ImGui::TextColored(ImVec4(0.96F, 0.62F, 0.04F, 1.0F), "[PAUSED]");
        } else {
            ImGui::TextColored(ImVec4(0.06F, 0.73F, 0.51F, 1.0F), "[RUNNING]");
        }

        // Bottom-left camera diagnostics overlay
        ImGui::SetCursorPos(ImVec2(12.0F, avail.y - 26.0F));
        ImGui::TextDisabled("Camera: Perspective | FOV: 60 deg | Clipping: 0.1m - 1000m");
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace eng::editor
