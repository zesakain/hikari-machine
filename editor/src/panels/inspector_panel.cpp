/// @file inspector_panel.cpp
/// @brief Entity inspector and component editor implementation with professional property grid.

#include "panels/inspector_panel.h"

#include "core/log.h"
#include "editor/editor_theme.h"
#include "physics/physics_components.h"
#include "script/script_components.h"
#include "world/world.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <imgui.h>

namespace eng::editor {

namespace {

void drawVec3Property(const char* label, float values[3], float resetValue = 0.0F,
                      float speed = 0.1F) {
    ImGui::PushID(label);
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, ThemeMetrics::kPropertyLabelWidth);
    ImGui::TextDisabled("%s", label);
    ImGui::NextColumn();

    float availW = ImGui::GetContentRegionAvail().x;
    float itemW = (availW - 70.0F) / 3.0F;
    if (itemW < 35.0F) {
        itemW = 35.0F;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0F, 0.0F));

    // X Axis (Red)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85F, 0.20F, 0.20F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95F, 0.30F, 0.30F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70F, 0.15F, 0.15F, 1.0F));
    if (ImGui::Button("X", ImVec2(18.0F, 0.0F))) {
        values[0] = resetValue;
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(itemW);
    ImGui::DragFloat("##X", &values[0], speed, 0.0F, 0.0F, "%.2f");
    ImGui::SameLine();

    // Y Axis (Green)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15F, 0.70F, 0.25F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25F, 0.80F, 0.35F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10F, 0.60F, 0.20F, 1.0F));
    if (ImGui::Button("Y", ImVec2(18.0F, 0.0F))) {
        values[1] = resetValue;
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(itemW);
    ImGui::DragFloat("##Y", &values[1], speed, 0.0F, 0.0F, "%.2f");
    ImGui::SameLine();

    // Z Axis (Blue)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20F, 0.45F, 0.85F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30F, 0.55F, 0.95F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15F, 0.35F, 0.75F, 1.0F));
    if (ImGui::Button("Z", ImVec2(18.0F, 0.0F))) {
        values[2] = resetValue;
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(itemW);
    ImGui::DragFloat("##Z", &values[2], speed, 0.0F, 0.0F, "%.2f");

    ImGui::PopStyleVar();
    ImGui::Columns(1);
    ImGui::PopID();
}

} // namespace

void InspectorPanel::draw(EditorContext& ctx) {
    if (!isOpen_) {
        return;
    }

    if (ImGui::Begin(getDisplayName().data(), &isOpen_)) {
        if (ctx.world != nullptr && ctx.selectedEntityId != 0) {
            flecs::world& ecs = ctx.world->getRawWorld();
            flecs::entity e = ecs.entity(ctx.selectedEntityId);

            if (e.is_alive()) {
                // Entity Header
                const char* entityName = e.name().c_str();
                if (entityName == nullptr || entityName[0] == '\0') {
                    entityName = "Unnamed Entity";
                }

                char nameBuf[64];
                std::snprintf(nameBuf, sizeof(nameBuf), "%s", entityName);

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0F, 4.0F));
                ImGui::TextDisabled("Entity");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90.0F);
                if (ImGui::InputText("##EntityNameInput", nameBuf, sizeof(nameBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
                    e.set_name(nameBuf);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("#%llu", static_cast<unsigned long long>(e.id()));
                ImGui::PopStyleVar();

                ImGui::Separator();
                ImGui::Spacing();

                // 1. Transform Component Card
                if (e.has<physics::PhysicsTransformComponent>()) {
                    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                        auto* tc = e.get_mut<physics::PhysicsTransformComponent>();

                        float pos[3] = {tc->transform.position.x, tc->transform.position.y,
                                        tc->transform.position.z};
                        drawVec3Property("Position", pos, 0.0F, 0.05F);
                        tc->transform.position = Vec3(pos[0], pos[1], pos[2]);

                        // Display Euler angles in degrees
                        float rot[3] = {tc->transform.rotation.x * 57.2957795F,
                                        tc->transform.rotation.y * 57.2957795F,
                                        tc->transform.rotation.z * 57.2957795F};
                        drawVec3Property("Rotation", rot, 0.0F, 0.5F);
                        tc->transform.rotation =
                            Quat(rot[0] * 0.0174532925F, rot[1] * 0.0174532925F,
                                 rot[2] * 0.0174532925F, 1.0F)
                                .normalized();

                        float scl[3] = {tc->transform.scale.x, tc->transform.scale.y,
                                        tc->transform.scale.z};
                        drawVec3Property("Scale", scl, 1.0F, 0.05F);
                        tc->transform.scale = Vec3(scl[0], scl[1], scl[2]);
                    }
                }

                // 2. Rigid Body Component Card
                if (e.has<physics::RigidBodyComponent>()) {
                    if (ImGui::CollapsingHeader("Rigid Body", ImGuiTreeNodeFlags_DefaultOpen)) {
                        auto* rb = e.get_mut<physics::RigidBodyComponent>();

                        ImGui::Columns(2, nullptr, false);
                        ImGui::SetColumnWidth(0, ThemeMetrics::kPropertyLabelWidth);
                        ImGui::TextDisabled("Motion Type");
                        ImGui::NextColumn();

                        int motionIdx = (rb->motionType == physics::MotionType::Static)      ? 0
                                        : (rb->motionType == physics::MotionType::Kinematic) ? 1
                                                                                             : 2;
                        const char* motions[] = {"Static", "Kinematic", "Dynamic"};
                        ImGui::SetNextItemWidth(-1.0F);
                        if (ImGui::Combo("##MotionTypeCombo", &motionIdx, motions, 3)) {
                            rb->motionType = (motionIdx == 0)   ? physics::MotionType::Static
                                             : (motionIdx == 1) ? physics::MotionType::Kinematic
                                                                : physics::MotionType::Dynamic;
                        }
                        ImGui::Columns(1);

                        ImGui::Columns(2, nullptr, false);
                        ImGui::SetColumnWidth(0, ThemeMetrics::kPropertyLabelWidth);
                        ImGui::TextDisabled("Collision Layer");
                        ImGui::NextColumn();

                        const char* layers[] = {"0: Static Geometry", "1: Moving Dynamics",
                                                "2: Trigger Volumes"};
                        int layerIdx = std::min(static_cast<int>(rb->collisionLayer), 2);
                        ImGui::SetNextItemWidth(-1.0F);
                        if (ImGui::Combo("##CollisionLayerCombo", &layerIdx, layers, 3)) {
                            rb->collisionLayer = static_cast<uint16_t>(layerIdx);
                        }
                        ImGui::Columns(1);
                    }
                }

                // 3. Script Component Card
                if (e.has<script::ScriptComponent>()) {
                    if (ImGui::CollapsingHeader("Script Component",
                                                ImGuiTreeNodeFlags_DefaultOpen)) {
                        auto* sc = e.get_mut<script::ScriptComponent>();

                        ImGui::Columns(2, nullptr, false);
                        ImGui::SetColumnWidth(0, ThemeMetrics::kPropertyLabelWidth);
                        ImGui::TextDisabled("Script File");
                        ImGui::NextColumn();
                        ImGui::Text("%s", sc->scriptName);
                        ImGui::Columns(1);

                        ImGui::Columns(2, nullptr, false);
                        ImGui::SetColumnWidth(0, ThemeMetrics::kPropertyLabelWidth);
                        ImGui::TextDisabled("Active");
                        ImGui::NextColumn();
                        ImGui::Checkbox("Enabled##ScriptActive", &sc->enabled);
                        ImGui::Columns(1);
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Add Component Dropdown Button
                if (ImGui::Button("+ Add Component", ImVec2(-1.0F, 28.0F))) {
                    ImGui::OpenPopup("AddComponentPopup");
                }
                if (ImGui::BeginPopup("AddComponentPopup")) {
                    if (!e.has<physics::PhysicsTransformComponent>() &&
                        ImGui::MenuItem("Transform")) {
                        e.set<physics::PhysicsTransformComponent>({});
                    }
                    if (!e.has<physics::RigidBodyComponent>() && ImGui::MenuItem("Rigid Body")) {
                        e.set<physics::RigidBodyComponent>({
                            .motionType = physics::MotionType::Dynamic,
                            .collisionLayer = physics::CollisionLayer::kMoving,
                        });
                    }
                    if (!e.has<script::ScriptComponent>() && ImGui::MenuItem("Script Component")) {
                        e.set<script::ScriptComponent>({
                            .scriptName = "scripts/main.lua",
                            .enabled = true,
                        });
                    }
                    ImGui::EndPopup();
                }

                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55F, 0.15F, 0.15F, 1.0F));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75F, 0.20F, 0.20F, 1.0F));
                if (ImGui::Button("Delete Entity", ImVec2(-1.0F, 24.0F))) {
                    e.destruct();
                    ctx.selectedEntityId = 0;
                }
                ImGui::PopStyleColor(2);
            } else {
                ImGui::TextDisabled("Selected entity is no longer alive.");
                ctx.selectedEntityId = 0;
            }
        } else {
            ImGui::Spacing();
            ImGui::TextDisabled("No Entity Selected");
            ImGui::TextWrapped("Select an entity from the Scene Hierarchy to inspect and edit its "
                               "components, transform parameters, and physics properties.");
        }
    }
    ImGui::End();
}

} // namespace eng::editor
