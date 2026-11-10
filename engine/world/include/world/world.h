/// @file world.h
/// @brief Unified simulation World encapsulating Flecs ECS and system group gating.
///
/// Purpose:
/// Represents the single shared simulation world for Hikari. Manages entities, component storage,
/// queries, and mode-gated system execution without duplicating state across mode switches.
///
/// Ownership and Lifetime:
/// Owned by the game application or simulation runtime via std::unique_ptr<World>.
/// Outlives all Modes and the Conductor.
///
/// Thread-safety:
/// World mutations (entity creation/destruction, component modification) must occur on the
/// main thread or via Flecs deferred operations. Reading components from non-mutating queries
/// is safe across worker jobs.
///
/// Performance:
/// Archetype-based contiguous memory iteration. Zero dynamic heap allocation in the hot path.

#pragma once

#include "core/assert.h"
#include "core/result.h"
#include "world/components.h"
#include "world/entity.h"
#include "world/system_group.h"

// Third-party Flecs ECS header
#include <array>
#include <flecs.h>
#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

namespace eng::world {

/// @class World
/// @brief Central ECS world and system group manager.
class World {
public:
    World();
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&& other) noexcept;
    World& operator=(World&& other) noexcept;

    /// Spawns a new entity in the shared world.
    [[nodiscard]] EntityHandle createEntity();

    /// Spawns a new entity with a diagnostic name.
    [[nodiscard]] EntityHandle createEntity(std::string_view name);

    /// Destroys an entity and reclaims its slot.
    void destroyEntity(EntityHandle entity);

    /// Destroys all user-created entities in the World, leaving registered components,
    /// system pipelines, and modules completely intact.
    void clearUserEntities();

    /// Checks if an entity handle references an active, alive entity in the world.
    [[nodiscard]] bool isAlive(EntityHandle entity) const;

    /// Retrieves the name of an entity if set, or empty string_view if unnamed.
    [[nodiscard]] std::string_view getEntityName(EntityHandle entity) const;

    /// Sets or updates the diagnostic name of an entity.
    void setEntityName(EntityHandle entity, std::string_view name);

    /// Sets or replaces a component on an entity.
    /// If T is an empty struct (tag component), adds the tag without allocating component storage.
    template <typename T> void setComponent(EntityHandle entity, T component) {
        ENG_ASSERT(isAlive(entity), "Attempted to set component on dead or invalid entity");
        if constexpr (std::is_empty_v<T>) {
            (void)component;
            ecs_.entity(entity.id).template add<T>();
        } else {
            ecs_.entity(entity.id).set<T>(std::move(component));
            if constexpr (std::is_same_v<T, TransformComponent>) {
                if (!ecs_.entity(entity.id).template has<WorldTransformComponent>()) {
                    ecs_.entity(entity.id).template set<WorldTransformComponent>({});
                }
            }
        }
    }

    /// Adds a zero-sized tag component to an entity without data storage.
    template <typename T> void addTag(EntityHandle entity) {
        ENG_ASSERT(isAlive(entity), "Attempted to add tag on dead or invalid entity");
        ecs_.entity(entity.id).template add<T>();
    }

    /// Retrieves a const pointer to an entity's component, or nullptr if not present or zero-sized.
    template <typename T> [[nodiscard]] const T* getComponent(EntityHandle entity) const {
        if constexpr (std::is_empty_v<T>) {
            return nullptr;
        } else {
            if (!isAlive(entity)) {
                return nullptr;
            }
            return ecs_.entity(entity.id).template get<T>();
        }
    }

    /// Retrieves a mutable pointer to an entity's component, or nullptr if not present or
    /// zero-sized.
    template <typename T> [[nodiscard]] T* getMutableComponent(EntityHandle entity) {
        if constexpr (std::is_empty_v<T>) {
            return nullptr;
        } else {
            if (!isAlive(entity)) {
                return nullptr;
            }
            return ecs_.entity(entity.id).template get_mut<T>();
        }
    }

    /// Checks whether an entity has a specific component.
    template <typename T> [[nodiscard]] bool hasComponent(EntityHandle entity) const {
        if (!isAlive(entity)) {
            return false;
        }
        return ecs_.entity(entity.id).template has<T>();
    }

    /// Removes a component from an entity.
    template <typename T> void removeComponent(EntityHandle entity) {
        if (isAlive(entity)) {
            ecs_.entity(entity.id).template remove<T>();
        }
    }

    /// Associates an existing Flecs system with a specific SystemGroup phase.
    void registerSystem(SystemGroup group, flecs::system system);

    /// Enables or disables execution for all systems in a SystemGroup.
    void setSystemGroupActive(SystemGroup group, bool active);

    /// Returns whether a SystemGroup is currently active.
    [[nodiscard]] bool isSystemGroupActive(SystemGroup group) const noexcept;

    /// Advances world simulation time, executing active system pipelines.
    ///
    /// @param deltaSeconds Frame delta time in seconds.
    void progress(float deltaSeconds);

    /// Retrieves all currently active user entities in the World.
    [[nodiscard]] std::vector<EntityHandle> getAllEntities() const;

    /// Validator function signature for hierarchy operations.
    using HierarchyValidator = bool (*)(const World& world, EntityHandle child,
                                        EntityHandle parent);

    /// Establishes a parent-child relationship. Pass EntityHandle{} as parent to detach to root.
    ///
    /// Preconditions:
    /// - child must be an alive entity.
    /// - If parent is valid, parent must be an alive entity.
    /// - child != parent.
    /// - parent must not be a descendant of child (acyclic invariant).
    /// - Any registered hierarchy validators must return true.
    /// - If preserveWorldTransform is true, target parent matrix must be invertible.
    ///
    /// @param child The child entity to attach or detach.
    /// @param parent The new parent entity, or invalid handle to make root.
    /// @param preserveWorldTransform If true, recalculates local transform to keep world position
    /// stationary.
    /// @return true if parenting was successful; false if rejected.
    bool setParent(EntityHandle child, EntityHandle parent, bool preserveWorldTransform = true);

    /// Retrieves the direct parent entity, or an invalid handle if entity is root or dead.
    [[nodiscard]] EntityHandle getParent(EntityHandle child) const;

    /// Retrieves all immediate children of the specified parent entity.
    [[nodiscard]] std::vector<EntityHandle> getChildren(EntityHandle parent) const;

    /// Iterates immediate children of parent without allocating dynamic memory.
    template <typename Fn> void forEachChild(EntityHandle parent, Fn&& fn) const {
        if (!isAlive(parent)) {
            return;
        }
        flecs::entity p = ecs_.entity(parent.id);
        p.children([&fn](flecs::entity child) { fn(EntityHandle(child.id())); });
    }

    /// Checks if 'descendant' is an ancestor-descendant child of 'ancestor'.
    [[nodiscard]] bool isDescendantOf(EntityHandle descendant, EntityHandle ancestor) const;

    /// Retrieves all root entities (alive entities without a parent).
    [[nodiscard]] std::vector<EntityHandle> getRootEntities() const;

    /// Iterates all root entities without allocating dynamic memory.
    template <typename Fn> void forEachRootEntity(Fn&& fn) const {
        for (EntityHandle h : activeEntities_) {
            if (isAlive(h) && !getParent(h).isValid()) {
                fn(h);
            }
        }
    }

    /// Registers a validation callback invoked by setParent() before establishing relationships.
    void addHierarchyValidator(HierarchyValidator validator);

    /// Direct access to underlying Flecs world instance.
    [[nodiscard]] flecs::world& getRawWorld() noexcept { return ecs_; }

    /// Direct const access to underlying Flecs world instance.
    [[nodiscard]] const flecs::world& getRawWorld() const noexcept { return ecs_; }

private:
    flecs::world ecs_{};
    std::vector<EntityHandle> activeEntities_{};
    std::array<bool, static_cast<size_t>(SystemGroup::Count)> groupActive_{};
    std::array<std::vector<flecs::system>, static_cast<size_t>(SystemGroup::Count)> groupSystems_{};
    std::vector<HierarchyValidator> hierarchyValidators_{};
};

} // namespace eng::world
