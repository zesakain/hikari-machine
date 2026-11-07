/// @file entity.h
/// @brief Strongly-typed generational entity identifier wrapping Flecs entity IDs.
///
/// Purpose:
/// Provides a lightweight, 64-bit hardware-neutral entity handle for referencing entities
/// in the simulation World without exposing internal Flecs pointer structures.
///
/// Ownership and Lifetime:
/// Pure value type (8 bytes). Does not own the referenced entity. Stale entity reference
/// detection is guaranteed via World::isAlive().
///
/// Thread-safety:
/// Value type with pure constexpr operations. Safe to copy and compare concurrently.
///
/// Performance:
/// Trivially copyable layout fitting in a single 64-bit CPU register. Zero heap allocation.

#pragma once

#include <compare>
#include <cstdint>

namespace eng::world {

/// @struct EntityHandle
/// @brief 64-bit generational entity identifier.
struct EntityHandle {
    /// Sentinel indicating an unassigned or invalid entity identifier.
    static constexpr uint64_t kInvalidId = 0;

    /// 64-bit numerical entity identifier (matching Flecs ecs_entity_t).
    uint64_t id{kInvalidId};

    constexpr EntityHandle() noexcept = default;
    constexpr explicit EntityHandle(uint64_t entityId) noexcept : id(entityId) {}

    /// Checks if this handle holds a non-sentinel entity identifier.
    [[nodiscard]] constexpr bool isValid() const noexcept { return id != kInvalidId; }

    /// Boolean conversion operator evaluating validity.
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return isValid(); }

    /// Three-way comparison operator.
    constexpr auto operator<=>(const EntityHandle&) const = default;
};

} // namespace eng::world
