/// @file conductor_panel.cpp
/// @brief Two-State Conductor visual state machine panel implementation.

#include "panels/conductor_panel.h"

#include "framework/conductor.h"

#include <imgui.h>

namespace eng::editor {

void ConductorPanel::draw(EditorContext& ctx) {
    if (!isOpen_) {
        return;
    }

    if (ImGui::Begin(getDisplayName().data(), &isOpen_)) {
        if (ctx.conductor != nullptr) {
            auto activeSlot = ctx.conductor->getActiveSlot();
            auto state = ctx.conductor->getState();

            // 1. Time Slot Status
            ImGui::TextColored(ImVec4(0.38F, 0.74F, 0.98F, 1.0F), "Two-State Architecture:");
            ImGui::Text("Active Slot:  %s (%s)",
                        activeSlot == framework::ActiveSlot::SlotA ? "Slot A" : "Slot B",
                        ctx.conductor->getActiveMode().getName().data());
            ImGui::Text("Standby Mode: %s", ctx.conductor->getStandbyMode().getName().data());

            // 2. Lifecycle State
            const char* stateStr = "Active";
            ImVec4 stateColor = ImVec4(0.06F, 0.73F, 0.51F, 1.0F); // green

            switch (state) {
            case framework::ConductorState::Uninitialized:
                stateStr = "Uninitialized";
                stateColor = ImVec4(0.5F, 0.5F, 0.5F, 1.0F);
                break;
            case framework::ConductorState::Active:
                stateStr = "Active (Authoritative Running)";
                stateColor = ImVec4(0.06F, 0.73F, 0.51F, 1.0F);
                break;
            case framework::ConductorState::Preparing:
                stateStr = "Preparing (Standby Incremental Budget)";
                stateColor = ImVec4(0.96F, 0.62F, 0.04F, 1.0F);
                break;
            case framework::ConductorState::Prepared:
                stateStr = "Prepared (Ready for Commit)";
                stateColor = ImVec4(0.38F, 0.74F, 0.98F, 1.0F);
                break;
            case framework::ConductorState::Committing:
                stateStr = "Committing (Fast Validation)";
                stateColor = ImVec4(0.7F, 0.4F, 0.9F, 1.0F);
                break;
            case framework::ConductorState::Transitioning:
                stateStr = "Transitioning (Slow-Motion Dilation)";
                stateColor = ImVec4(0.85F, 0.35F, 0.85F, 1.0F);
                break;
            case framework::ConductorState::Terminated:
                stateStr = "Terminated";
                stateColor = ImVec4(0.94F, 0.27F, 0.27F, 1.0F);
                break;
            }

            ImGui::TextColored(stateColor, "Lifecycle State: %s", stateStr);
            ImGui::Text("Committed Transitions: %u",
                        ctx.conductor->getSuccessfulTransitionsCount());

            ImGui::Separator();
            ImGui::Spacing();

            // 3. Transition Control Actions
            if (state == framework::ConductorState::Active) {
                if (ImGui::Button("Request Transition", ImVec2(-1.0F, 28.0F))) {
                    (void)ctx.conductor->requestTransition();
                }
            } else if (state == framework::ConductorState::Prepared) {
                if (ImGui::Button("Execute Transition", ImVec2(180.0F, 28.0F))) {
                    (void)ctx.conductor->executeTransition();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(100.0F, 28.0F))) {
                    ctx.conductor->cancelTransition();
                }
            } else if (state == framework::ConductorState::Preparing) {
                ImGui::Text("Standby mode is preparing in background budget...");
                if (ImGui::Button("Cancel", ImVec2(100.0F, 28.0F))) {
                    ctx.conductor->cancelTransition();
                }
            }
        } else {
            ImGui::TextDisabled("No active Conductor bound");
        }
    }
    ImGui::End();
}

} // namespace eng::editor
