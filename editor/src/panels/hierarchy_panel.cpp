/// @file hierarchy_panel.cpp
/// @brief Scene hierarchy panel implementation with modern desktop tree presentation.

#include "panels/hierarchy_panel.h"

#include "core/log.h"
#include "editor/editor_theme.h"
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
            // Search Bar & Filter
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0F);
            ImGui::InputTextWithHint("##HierarchySearch", "Search entities...",
                                     searchFilter_.data(), searchFilter_.size());
            ImGui::SameLine();
            if (ImGui::Button("+ Add", ImVec2(60.0F, 0.0F))) {
                ImGui::OpenPopup("AddEntityPopup");
            }

            if (ImGui::BeginPopup("AddEntityPopup")) {
                if (ImGui::MenuItem("Empty Entity")) {
                    auto h = ctx.world->createEntity("EmptyEntity");
                    ctx.selectedEntityId = h.id;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("3D Physics Cube")) {
                    auto h = ctx.world->createEntity("PhysicsCube");
                    ctx.world->setComponent(h, physics::PhysicsTransformComponent{
                                                   .transform = Transform{Vec3{0.0F, 2.0F, 0.0F}},
                                               });
                    ctx.world->setComponent(h,
                                            physics::RigidBodyComponent{
                                                .bodyHandle = {},
                                                .motionType = physics::MotionType::Dynamic,
                                                .collisionLayer = physics::CollisionLayer::kMoving,
                                            });
                    ctx.selectedEntityId = h.id;
                }
                if (ImGui::MenuItem("3D Physics Sphere")) {
                    auto h = ctx.world->createEntity("PhysicsSphere");
                    ctx.world->setComponent(h, physics::PhysicsTransformComponent{
                                                   .transform = Transform{Vec3{1.0F, 4.0F, 0.0F}},
                                               });
                    ctx.world->setComponent(h,
                                            physics::RigidBodyComponent{
                                                .bodyHandle = {},
                                                .motionType = physics::MotionType::Dynamic,
                                                .collisionLayer = physics::CollisionLayer::kMoving,
                                            });
                    ctx.selectedEntityId = h.id;
                }
                ImGui::EndPopup();
            }

            ImGui::Separator();

            ImGui::BeginChild("HierarchyTreeRegion", ImVec2(0.0F, 0.0F), false,
                              ImGuiWindowFlags_AlwaysVerticalScrollbar);

            flecs::world& ecs = ctx.world->getRawWorld();
            int entityCount = 0;

            ecs.each([&](flecs::entity e) {
                if (!e.is_alive()) {
                    return;
                }
                const char* name = e.name().c_str();
                if (name == nullptr || name[0] == '\0') {
                    name = "Unnamed Entity";
                }
                if (searchFilter_[0] != '\0' && strstr(name, searchFilter_.data()) == nullptr) {
                    return;
                }
                entityCount++;

                // Entity icon based on components
                const char* icon = "[E]";
                if (e.has<physics::RigidBodyComponent>()) {
                    icon = "[3D]";
                } else if (e.has<script::ScriptComponent>()) {
                    icon = "[JS]";
                }

                char label[128];
                std::snprintf(label, sizeof(label), "%s %s##%llu", icon, name,
                              static_cast<unsigned long long>(e.id()));

                bool isSelected = (ctx.selectedEntityId == e.id());
                ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_Leaf |
                                               ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                               ImGuiTreeNodeFlags_SpanAvailWidth;
                if (isSelected) {
                    nodeFlags |= ImGuiTreeNodeFlags_Selected;
                }

                ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(e.id())),
                                  nodeFlags, "%s", label);

                if (ImGui::IsItemClicked()) {
                    ctx.selectedEntityId = e.id();
                }

                // Component badge pills on the right
                float cursorX = ImGui::GetCursorPosX();
                float availX = ImGui::GetContentRegionAvail().x;
                (void)cursorX;

                if (e.has<physics::PhysicsTransformComponent>()) {
                    ImGui::SameLine(availX - 70.0F);
                    ImGui::TextColored(ImVec4(0.23F, 0.51F, 0.96F, 0.8F), "[Tr]");
                }
                if (e.has<physics::RigidBodyComponent>()) {
                    ImGui::SameLine(availX - 45.0F);
                    ImGui::TextColored(ImVec4(0.96F, 0.62F, 0.04F, 0.8F), "[RB]");
                }
                if (e.has<script::ScriptComponent>()) {
                    ImGui::SameLine(availX - 20.0F);
                    ImGui::TextColored(ImVec4(0.85F, 0.35F, 0.85F, 0.8F), "[L]");
                }

                // Context menu on entity
                char contextId[64];
                std::snprintf(contextId, sizeof(contextId), "EntityCtx_%llu",
                              static_cast<unsigned long long>(e.id()));
                if (ImGui::BeginPopupContextItem(contextId)) {
                    ctx.selectedEntityId = e.id();
                    if (ImGui::MenuItem("Duplicate Entity")) {
                        auto copy = ctx.world->createEntity(std::string(name) + "_Copy");
                        if (e.has<physics::PhysicsTransformComponent>()) {
                            ctx.world->setComponent(copy,
                                                    *e.get<physics::PhysicsTransformComponent>());
                        }
                        if (e.has<physics::RigidBodyComponent>()) {
                            ctx.world->setComponent(copy, *e.get<physics::RigidBodyComponent>());
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete Entity")) {
                        e.destruct();
                        ctx.selectedEntityId = 0;
                    }
                    ImGui::EndPopup();
                }
            });

            if (entityCount == 0) {
                ImGui::Spacing();
                ImGui::TextDisabled("No entities match search query.");
            }

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
