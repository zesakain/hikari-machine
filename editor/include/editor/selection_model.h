/// @file selection_model.h
/// @brief Centralized entity selection service for the Hikari Editor.
///
/// Purpose:
/// Manages single, multi, and range selection of entities across all editor panels
/// (Scene Hierarchy, Inspector, Viewport). Owns editor-side selection state independently
/// of the simulation ECS World and validates cached handles against entity destruction.
///
/// Invariants & Rules:
/// - Built ONLY when HIKARI_BUILD_EDITOR=ON.
/// - Exception-free and RTTI-free.
/// - Does not mutate simulation components or advance clocks.
///
/// Thread Safety:
/// Main thread only. Not thread-safe.

#pragma once

#include "world/entity.h"

#include <span>
#include <vector>

namespace eng::world {
class World;
} // namespace eng::world

namespace eng::editor {

/// @class SelectionModel
/// @brief Editor service managing selected entity handles with generational validation.
class SelectionModel {
public:
    SelectionModel() = default;
    ~SelectionModel() = default;

    SelectionModel(const SelectionModel&) = default;
    SelectionModel& operator=(const SelectionModel&) = default;
    SelectionModel(SelectionModel&&) noexcept = default;
    SelectionModel& operator=(SelectionModel&&) noexcept = default;

    /// Selects an entity. If multiSelect is false, replaces the entire selection.
    /// If multiSelect is true, appends the entity if not already present.
    void select(world::EntityHandle entity, bool multiSelect = false);

    /// Toggles the selection state of an entity (Ctrl-click behavior).
    void toggle(world::EntityHandle entity);

    /// Selects a contiguous range of entities from the primary selection to targetEntity.
    void selectRange(std::span<const world::EntityHandle> orderedEntities,
                     world::EntityHandle targetEntity);

    /// Deselects a specific entity.
    void deselect(world::EntityHandle entity);

    /// Clears the entire selection.
    void clear() noexcept;

    /// Selects all entities in the provided span.
    void selectAll(std::span<const world::EntityHandle> entities);

    /// Evaluates whether an entity is currently selected.
    [[nodiscard]] bool isSelected(world::EntityHandle entity) const noexcept;

    /// Returns the primary (most recently selected) entity handle.
    [[nodiscard]] world::EntityHandle getPrimarySelected() const noexcept {
        return primarySelected_;
    }

    /// Returns read-only view of all currently selected entity handles.
    [[nodiscard]] std::span<const world::EntityHandle> getSelectedEntities() const noexcept {
        return selectedEntities_;
    }

    /// Returns total count of selected entities.
    [[nodiscard]] size_t count() const noexcept { return selectedEntities_.size(); }

    /// Returns true if no entities are currently selected.
    [[nodiscard]] bool empty() const noexcept { return selectedEntities_.empty(); }

    /// Validates all selected handles against the world, purging destroyed or stale entities.
    void validate(const world::World& world);

    /// Returns whether selection has changed since the last clearChanged() call.
    [[nodiscard]] bool hasChanged() const noexcept { return hasChanged_; }

    /// Resets the changed flag.
    void clearChanged() noexcept { hasChanged_ = false; }

private:
    std::vector<world::EntityHandle> selectedEntities_;
    world::EntityHandle primarySelected_{};
    bool hasChanged_{false};
};

} // namespace eng::editor
