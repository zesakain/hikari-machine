/// @file conductor_panel.cpp
/// @brief Two-State Conductor visual state machine and transition control panel implementation.

#include "panels/conductor_panel.h"

#include "editor/editor_theme.h"
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

            // 1. Two-State Mirrored Cards (Slot A vs Slot B)
            ImGui::Columns(2, nullptr, true);

            // Card 1: Slot A (Exploration)
            bool isSlotAActive = (activeSlot == framework::ActiveSlot::SlotA);
            if (isSlotAActive) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12F, 0.18F, 0.28F, 1.0F));
            }
            ImGui::BeginChild("SlotACard", ImVec2(0.0F, 110.0F), true);
            ImGui::TextColored(isSlotAActive ? ImVec4(0.38F, 0.74F, 0.98F, 1.0F)
                                             : ImVec4(0.6F, 0.6F, 0.6F, 1.0F),
                               "STATE A: ExplorationMode");
            ImGui::Separator();
            if (isSlotAActive) {
                ImGui::TextColored(ImVec4(0.06F, 0.73F, 0.51F, 1.0F), "[AUTHORITATIVE ACTIVE]");
                ImGui::TextDisabled("Simulation: Continuous + State A");
                ImGui::TextDisabled("Clock: Normal Speed (1.0x)");
            } else {
                ImGui::TextColored(ImVec4(0.96F, 0.62F, 0.04F, 1.0F), "[STANDBY]");
                ImGui::TextDisabled("Status: Preparing Entry Cache");
                ImGui::TextDisabled("Budget: 0.50 ms/frame");
            }
            ImGui::EndChild();
            if (isSlotAActive) {
                ImGui::PopStyleColor();
            }
            ImGui::NextColumn();

            // Card 2: Slot B (Skill)
            bool isSlotBActive = (activeSlot == framework::ActiveSlot::SlotB);
            if (isSlotBActive) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.28F, 0.20F, 0.12F, 1.0F));
            }
            ImGui::BeginChild("SlotBCard", ImVec2(0.0F, 110.0F), true);
            ImGui::TextColored(isSlotBActive ? ImVec4(0.96F, 0.62F, 0.04F, 1.0F)
                                             : ImVec4(0.6F, 0.6F, 0.6F, 1.0F),
                               "STATE B: SkillMode");
            ImGui::Separator();
            if (isSlotBActive) {
                ImGui::TextColored(ImVec4(0.06F, 0.73F, 0.51F, 1.0F), "[AUTHORITATIVE ACTIVE]");
                ImGui::TextDisabled("Simulation: Continuous + State B");
                ImGui::TextDisabled("Clock: Precision Mode");
            } else {
                ImGui::TextColored(ImVec4(0.96F, 0.62F, 0.04F, 1.0F), "[STANDBY]");
                ImGui::TextDisabled("Status: Preparing Option Matrix");
                ImGui::TextDisabled("Budget: 0.50 ms/frame");
            }
            ImGui::EndChild();
            if (isSlotBActive) {
                ImGui::PopStyleColor();
            }
            ImGui::NextColumn();

            ImGui::Columns(1);
            ImGui::Separator();
            ImGui::Spacing();

            // 2. Lifecycle State Machine
            const char* stateStr = "Active";
            ImVec4 stateColor = ImVec4(0.06F, 0.73F, 0.51F, 1.0F);

            switch (state) {
            case framework::ConductorState::Uninitialized:
                stateStr = "Uninitialized";
                stateColor = ImVec4(0.5F, 0.5F, 0.5F, 1.0F);
                break;
            case framework::ConductorState::Active:
                stateStr = "Active (Authoritative Steady State)";
                stateColor = ImVec4(0.06F, 0.73F, 0.51F, 1.0F);
                break;
            case framework::ConductorState::Preparing:
                stateStr = "Preparing (Standby Incremental Budget)";
                stateColor = ImVec4(0.96F, 0.62F, 0.04F, 1.0F);
                break;
            case framework::ConductorState::Prepared:
                stateStr = "Prepared (Ready to Commit Transition)";
                stateColor = ImVec4(0.38F, 0.74F, 0.98F, 1.0F);
                break;
            case framework::ConductorState::Committing:
                stateStr = "Committing (Cache Validation <= 0.2ms)";
                stateColor = ImVec4(0.7F, 0.4F, 0.9F, 1.0F);
                break;
            case framework::ConductorState::Transitioning:
                stateStr = "Transitioning (Slow-Motion Transition Curve)";
                stateColor = ImVec4(0.85F, 0.35F, 0.85F, 1.0F);
                break;
            case framework::ConductorState::Terminated:
                stateStr = "Terminated";
                stateColor = ImVec4(0.94F, 0.27F, 0.27F, 1.0F);
                break;
            }

            ImGui::Text("Conductor Lifecycle: ");
            ImGui::SameLine();
            ImGui::TextColored(stateColor, "%s", stateStr);
            ImGui::TextDisabled("Committed Transitions: %u",
                                ctx.conductor->getSuccessfulTransitionsCount());

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // 3. Interactive Transition Controls
            if (state == framework::ConductorState::Active) {
                const char* targetName =
                    isSlotAActive ? "State B (Skill)" : "State A (Exploration)";
                char btnLabel[64];
                std::snprintf(btnLabel, sizeof(btnLabel), "Request Transition to %s", targetName);
                if (ImGui::Button(btnLabel, ImVec2(-1.0F, 30.0F))) {
                    (void)ctx.conductor->requestTransition();
                }
            } else if (state == framework::ConductorState::Prepared) {
                if (ImGui::Button("Commit Transition Switch", ImVec2(200.0F, 30.0F))) {
                    (void)ctx.conductor->executeTransition();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(100.0F, 30.0F))) {
                    ctx.conductor->cancelTransition();
                }
            } else if (state == framework::ConductorState::Preparing) {
                ImGui::TextDisabled("Standby mode is preparing within frame budget...");
                if (ImGui::Button("Cancel Preparation", ImVec2(160.0F, 30.0F))) {
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
