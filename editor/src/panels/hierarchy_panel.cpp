/// @file hierarchy_panel.cpp
/// @brief Scene hierarchy panel implementation with modern desktop tree presentation.

#include "panels/hierarchy_panel.h"

#include "core/log.h"
#include "editor/editor_theme.h"
#include "editor/selection_model.h"
#include "physics/physics_components.h"
#include "script/script_components.h"
#include "world/world.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <string>
#include <vector>

namespace eng::editor {

namespace {

std::string generateUniqueEntityName(flecs::world& ecs, const char* baseName) {
    std::string candidate = baseName;
    int suffix = 1;
    while (ecs.lookup(candidate.c_str()).is_alive()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s_%d", baseName, suffix++);
        candidate = buf;
    }
    return candidate;
}

} // namespace

void HierarchyPanel::startRenaming(world::EntityHandle entity, std::string_view currentName) {
    renamingEntity_ = entity;
    renameBuffer_.fill('\0');
    size_t copyLen = std::min(currentName.size(), renameBuffer_.size() - 1);
    for (size_t i = 0; i < copyLen; ++i) {
        renameBuffer_[i] = currentName[i];
    }
    renameBuffer_[copyLen] = '\0';
    focusRenameInput_ = true;
}

void HierarchyPanel::commitRename(EditorContext& ctx) {
    if (ctx.world != nullptr && renamingEntity_.isValid()) {
        size_t start = 0;
        while (start < renameBuffer_.size() && renameBuffer_[start] != '\0' &&
               std::isspace(static_cast<unsigned char>(renameBuffer_[start]))) {
            ++start;
        }
        if (renameBuffer_[start] != '\0') {
            size_t end = start;
            while (end < renameBuffer_.size() && renameBuffer_[end] != '\0') {
                ++end;
            }
            while (end > start &&
                   std::isspace(static_cast<unsigned char>(renameBuffer_[end - 1]))) {
                --end;
            }
            if (end > start) {
                std::string_view newName(&renameBuffer_[start], end - start);
                ctx.world->setEntityName(renamingEntity_, newName);
            }
        }
    }
    cancelRename();
}

void HierarchyPanel::cancelRename() noexcept {
    renamingEntity_ = world::EntityHandle{};
    renameBuffer_.fill('\0');
    focusRenameInput_ = false;
}

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
                flecs::world& ecs = ctx.world->getRawWorld();
                if (ImGui::MenuItem("Create Empty Entity")) {
                    auto name = generateUniqueEntityName(ecs, "EmptyEntity");
                    auto h = ctx.world->createEntity(name);
                    if (ctx.selection != nullptr) {
                        ctx.selection->select(h, false);
                    }
                    ctx.selectedEntityId = h.id;
                }
                if (ImGui::MenuItem("Create Entity with Transform")) {
                    auto name = generateUniqueEntityName(ecs, "Entity");
                    auto h = ctx.world->createEntity(name);
                    ctx.world->setComponent(h, physics::PhysicsTransformComponent{
                                                   .transform = Transform{},
                                               });
                    if (ctx.selection != nullptr) {
                        ctx.selection->select(h, false);
                    }
                    ctx.selectedEntityId = h.id;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("3D Physics Cube")) {
                    auto name = generateUniqueEntityName(ecs, "PhysicsCube");
                    auto h = ctx.world->createEntity(name);
                    ctx.world->setComponent(h, physics::PhysicsTransformComponent{
                                                   .transform = Transform{Vec3{0.0F, 2.0F, 0.0F}},
                                               });
                    ctx.world->setComponent(h,
                                            physics::RigidBodyComponent{
                                                .bodyHandle = {},
                                                .motionType = physics::MotionType::Dynamic,
                                                .collisionLayer = physics::CollisionLayer::kMoving,
                                            });
                    if (ctx.selection != nullptr) {
                        ctx.selection->select(h, false);
                    }
                    ctx.selectedEntityId = h.id;
                }
                if (ImGui::MenuItem("3D Physics Sphere")) {
                    auto name = generateUniqueEntityName(ecs, "PhysicsSphere");
                    auto h = ctx.world->createEntity(name);
                    ctx.world->setComponent(h, physics::PhysicsTransformComponent{
                                                   .transform = Transform{Vec3{1.0F, 4.0F, 0.0F}},
                                               });
                    ctx.world->setComponent(h,
                                            physics::RigidBodyComponent{
                                                .bodyHandle = {},
                                                .motionType = physics::MotionType::Dynamic,
                                                .collisionLayer = physics::CollisionLayer::kMoving,
                                            });
                    if (ctx.selection != nullptr) {
                        ctx.selection->select(h, false);
                    }
                    ctx.selectedEntityId = h.id;
                }
                ImGui::EndPopup();
            }

            ImGui::Separator();

            ImGui::BeginChild("HierarchyTreeRegion", ImVec2(0.0F, 0.0F), false,
                              ImGuiWindowFlags_AlwaysVerticalScrollbar);

            flecs::world& ecs = ctx.world->getRawWorld();
            std::vector<flecs::entity> entities;
            ecs.each([&](flecs::entity e) {
                if (!e.is_alive()) {
                    return;
                }
                if (e.id() < EcsFirstUserEntityId || e.has(flecs::Module) ||
                    e.has<flecs::Component>()) {
                    return;
                }
                entities.push_back(e);
            });

            std::sort(
                entities.begin(), entities.end(),
                [](const flecs::entity& a, const flecs::entity& b) { return a.id() < b.id(); });

            int entityCount = 0;

            for (const auto& e : entities) {
                const char* rawName = e.name().c_str();
                char fallbackName[64];
                const char* name = rawName;
                if (name == nullptr || name[0] == '\0') {
                    std::snprintf(fallbackName, sizeof(fallbackName), "Entity #%llu",
                                  static_cast<unsigned long long>(e.id()));
                    name = fallbackName;
                }
                if (searchFilter_[0] != '\0' && strstr(name, searchFilter_.data()) == nullptr) {
                    continue;
                }
                entityCount++;

                // Entity icon based on components
                const char* icon = "[E]";
                if (e.has<physics::RigidBodyComponent>()) {
                    icon = "[3D]";
                } else if (e.has<script::ScriptComponent>()) {
                    icon = "[JS]";
                }

                world::EntityHandle handle(e.id());
                bool isRenamingThis = (renamingEntity_ == handle);

                if (isRenamingThis) {
                    ImGui::Text("%s", icon);
                    ImGui::SameLine();
                    if (focusRenameInput_) {
                        ImGui::SetKeyboardFocusHere();
                        focusRenameInput_ = false;
                    }
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0F);
                    ImGuiInputTextFlags renameFlags =
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
                    if (ImGui::InputText("##InlineRename", renameBuffer_.data(),
                                         renameBuffer_.size(), renameFlags)) {
                        commitRename(ctx);
                    }
                    if (ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit()) {
                        commitRename(ctx);
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        cancelRename();
                    }
                } else {
                    char label[128];
                    std::snprintf(label, sizeof(label), "%s %s##%llu", icon, name,
                                  static_cast<unsigned long long>(e.id()));

                    bool isSelected = false;
                    if (ctx.selection != nullptr) {
                        isSelected = ctx.selection->isSelected(handle);
                    } else {
                        isSelected = (ctx.selectedEntityId == e.id());
                    }

                    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_Leaf |
                                                   ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                                   ImGuiTreeNodeFlags_SpanAvailWidth;
                    if (isSelected) {
                        nodeFlags |= ImGuiTreeNodeFlags_Selected;
                    }

                    ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(e.id())),
                                      nodeFlags, "%s", label);

                    if (ImGui::IsItemClicked()) {
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            startRenaming(handle, name);
                        } else {
                            if (ctx.selection != nullptr) {
                                ctx.selection->select(handle, false);
                            }
                            ctx.selectedEntityId = e.id();
                        }
                    }

                    // Component badge pills on the right
                    float availX = ImGui::GetContentRegionAvail().x;

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
                        if (ctx.selection != nullptr) {
                            if (!ctx.selection->isSelected(handle)) {
                                ctx.selection->select(handle, false);
                            }
                        }
                        ctx.selectedEntityId = e.id();

                        if (ImGui::MenuItem("Rename", "F2")) {
                            startRenaming(handle, name);
                        }
                        if (ImGui::MenuItem("Duplicate Entity")) {
                            auto copy = ctx.world->createEntity(std::string(name) + "_Copy");
                            if (e.has<physics::PhysicsTransformComponent>()) {
                                ctx.world->setComponent(
                                    copy, *e.get<physics::PhysicsTransformComponent>());
                            }
                            if (e.has<physics::RigidBodyComponent>()) {
                                ctx.world->setComponent(copy,
                                                        *e.get<physics::RigidBodyComponent>());
                            }
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Delete Entity")) {
                            e.destruct();
                            if (ctx.selection != nullptr) {
                                ctx.selection->deselect(handle);
                            }
                            ctx.selectedEntityId = 0;
                        }
                        ImGui::EndPopup();
                    }
                }
            }

            if (entityCount == 0) {
                ImGui::Spacing();
                ImGui::TextDisabled("No entities match search query.");
            }

            // Context menu on empty background
            if (ImGui::BeginPopupContextWindow("HierarchyBgCtx",
                                               ImGuiPopupFlags_MouseButtonRight |
                                                   ImGuiPopupFlags_NoOpenOverItems)) {
                if (ImGui::MenuItem("Create Empty Entity")) {
                    auto name = generateUniqueEntityName(ecs, "EmptyEntity");
                    auto h = ctx.world->createEntity(name);
                    if (ctx.selection != nullptr) {
                        ctx.selection->select(h, false);
                    }
                    ctx.selectedEntityId = h.id;
                }
                if (ImGui::MenuItem("Create Entity with Transform")) {
                    auto name = generateUniqueEntityName(ecs, "Entity");
                    auto h = ctx.world->createEntity(name);
                    ctx.world->setComponent(h, physics::PhysicsTransformComponent{
                                                   .transform = Transform{},
                                               });
                    if (ctx.selection != nullptr) {
                        ctx.selection->select(h, false);
                    }
                    ctx.selectedEntityId = h.id;
                }
                if (ImGui::MenuItem("Create Physics Cube")) {
                    auto name = generateUniqueEntityName(ecs, "PhysicsCube");
                    auto h = ctx.world->createEntity(name);
                    ctx.world->setComponent(h, physics::PhysicsTransformComponent{
                                                   .transform = Transform{Vec3{0.0F, 2.0F, 0.0F}},
                                               });
                    ctx.world->setComponent(h,
                                            physics::RigidBodyComponent{
                                                .bodyHandle = {},
                                                .motionType = physics::MotionType::Dynamic,
                                                .collisionLayer = physics::CollisionLayer::kMoving,
                                            });
                    if (ctx.selection != nullptr) {
                        ctx.selection->select(h, false);
                    }
                    ctx.selectedEntityId = h.id;
                }
                ImGui::EndPopup();
            }

            ImGui::EndChild();

            // Keyboard shortcut F2 to rename selected entity
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
                ImGui::IsKeyPressed(ImGuiKey_F2) && !renamingEntity_.isValid()) {
                world::EntityHandle target{};
                if (ctx.selection != nullptr) {
                    target = ctx.selection->getPrimarySelected();
                } else if (ctx.selectedEntityId != 0) {
                    target = world::EntityHandle(ctx.selectedEntityId);
                }
                if (target.isValid() && ctx.world->isAlive(target)) {
                    startRenaming(target, ctx.world->getEntityName(target));
                }
            }
        } else {
            ImGui::TextDisabled("No Shared World bound");
        }
    }
    ImGui::End();
}

} // namespace eng::editor
