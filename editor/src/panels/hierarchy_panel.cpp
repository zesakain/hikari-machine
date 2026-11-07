/// @file hierarchy_panel.cpp
/// @brief Scene hierarchy panel implementation.

#include "panels/hierarchy_panel.h"

#include "core/log.h"
#include "physics/physics_components.h"
#include "script/script_components.h"
#include "world/world.h"

#include <cstdio>
#include <cstring>
#include <imgui.h>

namespace eng::editor {

void HierarchyPanel::draw(EditorContext& ctx) {
    if (!isOpen_) {
        return;
    }

    if (ImGui::Begin(getDisplayName().data(), &isOpen_)) {
        if (ctx.world != nullptr) {
            // Action bar
            if (ImGui::Button("+ Empty")) {
                auto h = ctx.world->createEntity("EmptyEntity");
                ctx.selectedEntityId = h.id;
            }
            ImGui::SameLine();
            if (ImGui::Button("+ Cube")) {
                auto h = ctx.world->createEntity("PhysicsCube");
                ctx.world->setComponent(h, physics::PhysicsTransformComponent{
                                               .transform = Transform{Vec3{0.0F, 2.0F, 0.0F}},
                                           });
                ctx.world->setComponent(h, physics::RigidBodyComponent{
                                               .bodyHandle = {},
                                               .motionType = physics::MotionType::Dynamic,
                                               .collisionLayer = physics::CollisionLayer::kMoving,
                                           });
                ctx.selectedEntityId = h.id;
            }
            ImGui::SameLine();
            if (ImGui::Button("+ Sphere")) {
                auto h = ctx.world->createEntity("PhysicsSphere");
                ctx.world->setComponent(h, physics::PhysicsTransformComponent{
                                               .transform = Transform{Vec3{1.0F, 4.0F, 0.0F}},
                                           });
                ctx.world->setComponent(h, physics::RigidBodyComponent{
                                               .bodyHandle = {},
                                               .motionType = physics::MotionType::Dynamic,
                                               .collisionLayer = physics::CollisionLayer::kMoving,
                                           });
                ctx.selectedEntityId = h.id;
            }

            ImGui::Separator();
            ImGui::SetNextItemWidth(-1.0F);
            ImGui::InputTextWithHint("##HierarchySearch", "Search entities...",
                                     searchFilter_.data(), searchFilter_.size());
            ImGui::Separator();

            ImGui::BeginChild("HierarchyList", ImVec2(0.0F, 0.0F), false,
                              ImGuiWindowFlags_AlwaysVerticalScrollbar);

            flecs::world& ecs = ctx.world->getRawWorld();
            ecs.each([&](flecs::entity e) {
                if (!e.is_alive()) {
                    return;
                }
                const char* name = e.name().c_str();
                if (name == nullptr || name[0] == '\0') {
                    name = "Unnamed";
                }
                if (searchFilter_[0] != '\0' && strstr(name, searchFilter_.data()) == nullptr) {
                    return;
                }

                // Component badges
                char badges[32] = "";
                if (e.has<physics::PhysicsTransformComponent>()) {
                    strcat(badges, " [Tr]");
                }
                if (e.has<physics::RigidBodyComponent>()) {
                    strcat(badges, " [RB]");
                }
                if (e.has<script::ScriptComponent>()) {
                    strcat(badges, " [Lua]");
                }

                char label[128];
                std::snprintf(label, sizeof(label), "%s##%llu", name,
                              static_cast<unsigned long long>(e.id()));

                bool isSelected = (ctx.selectedEntityId == e.id());
                if (ImGui::Selectable(label, isSelected)) {
                    ctx.selectedEntityId = e.id();
                }

                if (badges[0] != '\0') {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", badges);
                }

                // Context menu on entity
                char contextId[64];
                std::snprintf(contextId, sizeof(contextId), "EntityCtx_%llu",
                              static_cast<unsigned long long>(e.id()));
                if (ImGui::BeginPopupContextItem(contextId)) {
                    ctx.selectedEntityId = e.id();
                    if (ImGui::MenuItem("Delete Entity")) {
                        e.destruct();
                        ctx.selectedEntityId = 0;
                    }
                    ImGui::EndPopup();
                }
            });

            // Context menu on empty background
            if (ImGui::BeginPopupContextWindow("HierarchyBgCtx",
                                               ImGuiPopupFlags_MouseButtonRight |
                                                   ImGuiPopupFlags_NoOpenOverItems)) {
                if (ImGui::MenuItem("Create Empty Entity")) {
                    auto h = ctx.world->createEntity("EmptyEntity");
                    ctx.selectedEntityId = h.id;
                }
                if (ImGui::MenuItem("Create Physics Cube")) {
                    auto h = ctx.world->createEntity("PhysicsCube");
                    ctx.world->setComponent(h, physics::PhysicsTransformComponent{
                                                   .transform = Transform{Vec3{0.0F, 2.0F, 0.0F}},
                                               });
                    ctx.selectedEntityId = h.id;
                }
                ImGui::EndPopup();
            }

            ImGui::EndChild();
        } else {
            ImGui::TextDisabled("No Shared World bound");
        }
    }
    ImGui::End();
}

} // namespace eng::editor
