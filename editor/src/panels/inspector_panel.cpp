/// @file inspector_panel.cpp
/// @brief Entity inspector and component editor implementation.

#include "panels/inspector_panel.h"

#include "core/log.h"
#include "physics/physics_components.h"
#include "script/script_components.h"
#include "world/world.h"

#include <imgui.h>

namespace eng::editor {

void InspectorPanel::draw(EditorContext& ctx) {
    if (!isOpen_) {
        return;
    }

    if (ImGui::Begin(getDisplayName().data(), &isOpen_)) {
        if (ctx.world != nullptr && ctx.selectedEntityId != 0) {
            flecs::world& ecs = ctx.world->getRawWorld();
            flecs::entity e = ecs.entity(ctx.selectedEntityId);

            if (e.is_alive()) {
                // Header block
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.38F, 0.74F, 0.98F, 1.0F));
                ImGui::Text("Entity ID: %llu", static_cast<unsigned long long>(e.id()));
                ImGui::PopStyleColor();

                const char* entityName = e.name().c_str();
                if (entityName == nullptr || entityName[0] == '\0') {
                    entityName = "Unnamed";
                }
                ImGui::Text("Name: %s", entityName);
                ImGui::Separator();

                // 1. Physics Transform Component
                if (e.has<physics::PhysicsTransformComponent>()) {
                    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                        auto* tc = e.get_mut<physics::PhysicsTransformComponent>();
                        float pos[3] = {tc->transform.position.x, tc->transform.position.y,
                                        tc->transform.position.z};
                        if (ImGui::DragFloat3("Position", pos, 0.1F)) {
                            tc->transform.position = Vec3(pos[0], pos[1], pos[2]);
                        }
                        float rot[4] = {tc->transform.rotation.x, tc->transform.rotation.y,
                                        tc->transform.rotation.z, tc->transform.rotation.w};
                        if (ImGui::DragFloat4("Rotation (xyzw)", rot, 0.05F)) {
                            tc->transform.rotation =
                                Quat(rot[0], rot[1], rot[2], rot[3]).normalized();
                        }
                        float scl[3] = {tc->transform.scale.x, tc->transform.scale.y,
                                        tc->transform.scale.z};
                        if (ImGui::DragFloat3("Scale", scl, 0.1F)) {
                            tc->transform.scale = Vec3(scl[0], scl[1], scl[2]);
                        }
                    }
                }

                // 2. Rigid Body Component
                if (e.has<physics::RigidBodyComponent>()) {
                    if (ImGui::CollapsingHeader("Rigid Body", ImGuiTreeNodeFlags_DefaultOpen)) {
                        auto* rb = e.get_mut<physics::RigidBodyComponent>();
                        int motionIdx = (rb->motionType == physics::MotionType::Static)      ? 0
                                        : (rb->motionType == physics::MotionType::Kinematic) ? 1
                                                                                             : 2;
                        const char* motions[] = {"Static", "Kinematic", "Dynamic"};
                        if (ImGui::Combo("Motion Type", &motionIdx, motions, 3)) {
                            rb->motionType = (motionIdx == 0)   ? physics::MotionType::Static
                                             : (motionIdx == 1) ? physics::MotionType::Kinematic
                                                                : physics::MotionType::Dynamic;
                        }
                        int layer = rb->collisionLayer;
                        if (ImGui::InputInt("Collision Layer", &layer)) {
                            rb->collisionLayer = static_cast<uint16_t>(layer);
                        }
                    }
                }

                // 3. Script Component
                if (e.has<script::ScriptComponent>()) {
                    if (ImGui::CollapsingHeader("Script Component",
                                                ImGuiTreeNodeFlags_DefaultOpen)) {
                        auto* sc = e.get_mut<script::ScriptComponent>();
                        ImGui::Text("Script: %s", sc->scriptName);
                        ImGui::Checkbox("Enabled", &sc->enabled);
                    }
                }

                ImGui::Separator();
                ImGui::Spacing();

                // Add Component Dropdown
                if (ImGui::Button("+ Add Component", ImVec2(-1.0F, 0.0F))) {
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
                if (ImGui::Button("Delete Entity", ImVec2(-1.0F, 0.0F))) {
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
            ImGui::TextDisabled("No entity selected.");
            ImGui::TextWrapped("Select an entity from the Scene Hierarchy to inspect its "
                               "components, transform parameters, and physics properties.");
        }
    }
    ImGui::End();
}

} // namespace eng::editor
