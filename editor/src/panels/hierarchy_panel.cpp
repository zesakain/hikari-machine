/// @file hierarchy_panel.cpp
/// @brief Scene hierarchy panel implementation with modern desktop tree presentation.

#include "editor/hierarchy_panel.h"

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

std::string HierarchyPanel::generateUniqueEntityName(flecs::world& ecs, const char* baseName) {
    if (baseName == nullptr || baseName[0] == '\0') {
        baseName = "Entity";
    }
    std::string candidate = baseName;
    int suffix = 1;
    while (true) {
        auto existing = ecs.lookup(candidate.c_str());
        // In Flecs, lookup returns entity with id 0 if not found.
        // Calling is_alive() directly on entity with id 0 triggers an assertion in ecs_is_alive().
        // An entity name is available if id == 0 or if the entity is not valid/alive.
        if (existing.id() == 0 || !existing.is_valid()) {
            break;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s_%d", baseName, suffix++);
        candidate = buf;
    }
    return candidate;
}

std::string HierarchyPanel::generateUniqueEntityName(world::World& world, const char* baseName) {
    return generateUniqueEntityName(world.getRawWorld(), baseName);
}

namespace {

world::EntityHandle duplicateEntity(world::World& world, world::EntityHandle source) {
    if (!world.isAlive(source)) {
        return world::EntityHandle{};
    }

    flecs::world& ecs = world.getRawWorld();
    flecs::entity srcEntity = ecs.entity(source.id);

    // 1. Generate unique copy name
    std::string baseName = std::string(world.getEntityName(source));
    if (baseName.empty()) {
        baseName = "Entity";
    }
    std::string copyName =
        HierarchyPanel::generateUniqueEntityName(ecs, (baseName + "_Copy").c_str());

    // 2. Spawn new valid entity in Shared World
    auto clone = world.createEntity(copyName);

    // 3. Clone supported components explicitly
    if (srcEntity.has<physics::PhysicsTransformComponent>()) {
        const auto* tc = srcEntity.get<physics::PhysicsTransformComponent>();
        if (tc != nullptr) {
            physics::PhysicsTransformComponent cloneTc = *tc;
            cloneTc.transform.position.x += 0.5F; // Visually offset duplicate
            world.setComponent(clone, cloneTc);
        }
    }

    if (srcEntity.has<physics::RigidBodyComponent>()) {
        const auto* rbc = srcEntity.get<physics::RigidBodyComponent>();
        if (rbc != nullptr) {
            physics::RigidBodyComponent cloneRbc{
                .bodyHandle = {}, // Fresh unassigned body handle
                .motionType = rbc->motionType,
                .collisionLayer = rbc->collisionLayer,
            };
            world.setComponent(clone, cloneRbc);
        }
    }

    if (srcEntity.has<script::ScriptComponent>()) {
        const auto* sc = srcEntity.get<script::ScriptComponent>();
        if (sc != nullptr) {
            world.setComponent(clone, *sc);
        }
    }

    return clone;
}

void deleteSelectedEntities(world::World& world, SelectionModel* selection, uint64_t& selectedId) {
    if (selection != nullptr && !selection->empty()) {
        auto entities = selection->getSelectedEntities();
        for (const auto& h : entities) {
            world.destroyEntity(h);
        }
        selection->clear();
        selectedId = 0;
    } else if (selectedId != 0) {
        world.destroyEntity(world::EntityHandle(selectedId));
        selectedId = 0;
    }
}

bool caseInsensitiveContains(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    if (haystack.size() < needle.size()) {
        return false;
    }
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                          [](char ch1, char ch2) {
                              return std::tolower(static_cast<unsigned char>(ch1)) ==
                                     std::tolower(static_cast<unsigned char>(ch2));
                          });
    return it != haystack.end();
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
            // Search Bar & Filter Controls
            float availW = ImGui::GetContentRegionAvail().x;
            float addBtnW = 60.0F;
            float clearBtnW = (searchFilter_[0] != '\0') ? 22.0F : 0.0F;
            float searchInputW = availW - addBtnW - clearBtnW - (clearBtnW > 0.0F ? 12.0F : 8.0F);

            ImGui::SetNextItemWidth(searchInputW);
            ImGui::InputTextWithHint("##HierarchySearch", "Search entities...",
                                     searchFilter_.data(), searchFilter_.size());
            if (clearBtnW > 0.0F) {
                ImGui::SameLine();
                if (ImGui::Button("X", ImVec2(clearBtnW, 0.0F))) {
                    searchFilter_.fill('\0');
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("+ Add", ImVec2(addBtnW, 0.0F))) {
                ImGui::OpenPopup("AddEntityPopup");
            }

            // Component filter chips
            auto drawFilterChip = [&](const char* label, ComponentFilter filter) {
                bool active = (componentFilter_ == filter);
                if (active) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.23F, 0.51F, 0.96F, 1.0F));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 1.0F, 1.0F, 1.0F));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15F, 0.17F, 0.22F, 1.0F));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65F, 0.65F, 0.65F, 1.0F));
                }
                if (ImGui::SmallButton(label)) {
                    componentFilter_ = filter;
                }
                ImGui::PopStyleColor(2);
            };

            drawFilterChip("All", ComponentFilter::All);
            ImGui::SameLine();
            drawFilterChip("[Tr]", ComponentFilter::Transform);
            ImGui::SameLine();
            drawFilterChip("[RB]", ComponentFilter::Physics);
            ImGui::SameLine();
            drawFilterChip("[Script]", ComponentFilter::Script);

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
            auto aliveHandles = ctx.world->getAllEntities();
            entities.reserve(aliveHandles.size());
            for (auto h : aliveHandles) {
                entities.push_back(ecs.entity(h.id));
            }

            std::sort(
                entities.begin(), entities.end(),
                [](const flecs::entity& a, const flecs::entity& b) { return a.id() < b.id(); });

            std::vector<world::EntityHandle> displayedHandles;
            displayedHandles.reserve(entities.size());
            int entityCount = 0;

            for (const auto& e : entities) {
                world::EntityHandle handle(e.id());
                if (!handle.isValid() || !ctx.world->isAlive(handle)) {
                    continue;
                }

                const char* rawName = e.name().c_str();
                char fallbackName[64];
                const char* name = rawName;
                if (name == nullptr || name[0] == '\0') {
                    std::snprintf(fallbackName, sizeof(fallbackName), "Entity #%llu",
                                  static_cast<unsigned long long>(e.id()));
                    name = fallbackName;
                }

                // Component filter
                if (componentFilter_ == ComponentFilter::Transform &&
                    !e.has<physics::PhysicsTransformComponent>()) {
                    continue;
                }
                if (componentFilter_ == ComponentFilter::Physics &&
                    !e.has<physics::RigidBodyComponent>()) {
                    continue;
                }
                if (componentFilter_ == ComponentFilter::Script &&
                    !e.has<script::ScriptComponent>()) {
                    continue;
                }

                // Case-insensitive name filter
                if (searchFilter_[0] != '\0' &&
                    !caseInsensitiveContains(name, searchFilter_.data())) {
                    continue;
                }

                displayedHandles.push_back(handle);
                entityCount++;

                // Entity icon based on components
                const char* icon = "[E]";
                if (e.has<physics::RigidBodyComponent>()) {
                    icon = "[3D]";
                } else if (e.has<script::ScriptComponent>()) {
                    icon = "[JS]";
                }

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
                        } else if (ctx.selection != nullptr) {
                            if (ImGui::GetIO().KeyCtrl) {
                                ctx.selection->toggle(handle);
                            } else if (ImGui::GetIO().KeyShift) {
                                ctx.selection->selectRange(displayedHandles, handle);
                            } else {
                                ctx.selection->select(handle, false);
                            }
                            ctx.selectedEntityId = ctx.selection->getPrimarySelected().id;
                        } else {
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
                        if (ImGui::MenuItem("Duplicate Entity", "Ctrl+D")) {
                            auto clone = duplicateEntity(*ctx.world, handle);
                            if (clone.isValid()) {
                                if (ctx.selection != nullptr) {
                                    ctx.selection->select(clone, false);
                                }
                                ctx.selectedEntityId = clone.id;
                            }
                        }
                        ImGui::Separator();
                        bool hasMultiSelection =
                            (ctx.selection != nullptr && ctx.selection->count() > 1 &&
                             ctx.selection->isSelected(handle));
                        if (hasMultiSelection) {
                            char delLabel[64];
                            std::snprintf(delLabel, sizeof(delLabel), "Delete %zu Entities",
                                          ctx.selection->count());
                            if (ImGui::MenuItem(delLabel, "Delete")) {
                                deleteSelectedEntities(*ctx.world, ctx.selection,
                                                       ctx.selectedEntityId);
                            }
                        } else {
                            if (ImGui::MenuItem("Delete Entity", "Delete")) {
                                ctx.world->destroyEntity(handle);
                                if (ctx.selection != nullptr) {
                                    ctx.selection->deselect(handle);
                                }
                                if (ctx.selectedEntityId == handle.id) {
                                    ctx.selectedEntityId = 0;
                                }
                            }
                        }
                        ImGui::EndPopup();
                    }
                }
            }

            if (entities.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled("No entities in scene.");
                ImGui::TextDisabled("Click '+ Add' above to create an entity.");
            } else if (entityCount == 0) {
                ImGui::Spacing();
                ImGui::TextDisabled("No entities match search or filter.");
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
                ImGui::Separator();
                if (ImGui::MenuItem("Select All", "Ctrl+A")) {
                    if (ctx.selection != nullptr) {
                        ctx.selection->selectAll(displayedHandles);
                        ctx.selectedEntityId = ctx.selection->getPrimarySelected().id;
                    }
                }
                if (ImGui::MenuItem("Clear Selection", "Esc")) {
                    if (ctx.selection != nullptr) {
                        ctx.selection->clear();
                        ctx.selectedEntityId = 0;
                    }
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

            // Keyboard shortcut Ctrl+D to duplicate selected entity
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::GetIO().KeyCtrl &&
                ImGui::IsKeyPressed(ImGuiKey_D) && !renamingEntity_.isValid()) {
                world::EntityHandle target{};
                if (ctx.selection != nullptr) {
                    target = ctx.selection->getPrimarySelected();
                } else if (ctx.selectedEntityId != 0) {
                    target = world::EntityHandle(ctx.selectedEntityId);
                }
                if (target.isValid() && ctx.world->isAlive(target)) {
                    auto clone = duplicateEntity(*ctx.world, target);
                    if (clone.isValid()) {
                        if (ctx.selection != nullptr) {
                            ctx.selection->select(clone, false);
                        }
                        ctx.selectedEntityId = clone.id;
                    }
                }
            }

            // Keyboard shortcut Delete to delete selected entities
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
                ImGui::IsKeyPressed(ImGuiKey_Delete) && !renamingEntity_.isValid()) {
                deleteSelectedEntities(*ctx.world, ctx.selection, ctx.selectedEntityId);
            }

            // Keyboard shortcut Ctrl+A to select all displayed entities
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::GetIO().KeyCtrl &&
                ImGui::IsKeyPressed(ImGuiKey_A) && !renamingEntity_.isValid() &&
                ctx.selection != nullptr) {
                ctx.selection->selectAll(displayedHandles);
                ctx.selectedEntityId = ctx.selection->getPrimarySelected().id;
            }

            // Keyboard shortcut Escape to clear selection
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
                ImGui::IsKeyPressed(ImGuiKey_Escape) && !renamingEntity_.isValid() &&
                ctx.selection != nullptr) {
                ctx.selection->clear();
                ctx.selectedEntityId = 0;
            }
        } else {
            ImGui::TextDisabled("No Shared World bound");
        }
    }
    ImGui::End();
}

} // namespace eng::editor
