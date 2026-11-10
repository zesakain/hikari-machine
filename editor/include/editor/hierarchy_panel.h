/// @file hierarchy_panel.h
/// @brief Scene hierarchy panel for inspecting and organizing entities in the Shared World.
///
/// Purpose:
/// Provides an interactive recursive tree view of entities in the active Shared World, supporting
/// selection, parent-child expansion/collapsing, inline renaming, drag-and-drop reparenting,
/// cascading deletion/duplication, and component badges.
///
/// Thread Safety:
/// Main editor thread only.
///
/// Invariants:
/// - Single source of truth is the ECS World; panel does not duplicate hierarchy state.
/// - Reparenting defaults to preserving world placement (stationary visual placement).

#pragma once

#include "editor/editor_panel.h"
#include "world/entity.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace flecs {
struct world;
} // namespace flecs

namespace eng::world {
class World;
} // namespace eng::world

namespace eng::editor {

/// ImGui drag-and-drop payload identifier for entity handles.
inline constexpr const char* kHierarchyEntityDragPayload = "HIERARCHY_ENTITY_DRAG";

enum class ComponentFilter : uint8_t { All = 0, Transform = 1, Physics = 2, Script = 3 };

class HierarchyPanel final : public EditorPanel {
public:
    HierarchyPanel() = default;
    ~HierarchyPanel() override = default;

    [[nodiscard]] std::string_view getId() const noexcept override { return "hierarchy"; }

    [[nodiscard]] std::string_view getDisplayName() const noexcept override {
        return "Scene Hierarchy";
    }

    [[nodiscard]] std::string_view getCategory() const noexcept override { return "Scene"; }

    void draw(EditorContext& ctx) override;

    /// Generates a unique, collision-free entity name in the Flecs ECS world.
    /// Safely handles non-existent entities, existing names, and invalid/dead entities.
    [[nodiscard]] static std::string generateUniqueEntityName(flecs::world& ecs,
                                                              const char* baseName);

    /// Generates a unique, collision-free entity name using the engine World.
    [[nodiscard]] static std::string generateUniqueEntityName(world::World& world,
                                                              const char* baseName);

    /// Reparents an entity using editor semantics.
    /// Defaults to preserveWorldTransform = true per Milestone 8B.6 Section 5.1.
    static bool reparentEntity(world::World& world, world::EntityHandle child,
                               world::EntityHandle parent, bool preserveWorldTransform = true);

    /// Duplicates an entity and its child hierarchy in the World, preserving supported components.
    static world::EntityHandle duplicateEntity(world::World& world, world::EntityHandle source,
                                               world::EntityHandle parent = world::EntityHandle{});

    /// Initiates an inline rename for an entity.
    void startRenaming(world::EntityHandle entity, std::string_view currentName);

    /// Commits the active inline rename if valid.
    void commitRename(EditorContext& ctx);

    /// Cancels the active inline rename.
    void cancelRename() noexcept;

private:
    void drawEntityNode(EditorContext& ctx, world::EntityHandle handle,
                        std::vector<world::EntityHandle>& displayedHandles, int& entityCount,
                        std::string_view searchFilter, ComponentFilter compFilter);

    std::array<char, 64> searchFilter_{};
    ComponentFilter componentFilter_{ComponentFilter::All};
    world::EntityHandle renamingEntity_{};
    std::array<char, 64> renameBuffer_{};
    bool focusRenameInput_{false};
};

} // namespace eng::editor
