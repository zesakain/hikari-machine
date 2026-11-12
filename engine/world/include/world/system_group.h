/// @file system_group.h
/// @brief System group classifications for mode-gated simulation scheduling.
///
/// Purpose:
/// Classifies simulation and gameplay systems into continuous versus mode-gated execution phases.
/// Allows the Conductor to toggle entire groups of systems on or off across mode transitions
/// without recreating entities, systems, or simulation state.
///
/// Ownership and Lifetime:
/// Pure enumeration; value type with zero heap overhead.
///
/// Thread-safety:
/// Value type; inherently thread-safe.

#pragma once

#include <cstdint>

namespace eng::world {

/// @enum SystemGroup
/// @brief Partitioning tags for system scheduling in World.
enum class SystemGroup : uint8_t {
    /// Continuous systems executed every frame regardless of active mode (e.g. physics, transforms,
    /// audio).
    Continuous = 0,

    /// Systems active exclusively while State A is active.
    StateA = 1,

    /// Systems active exclusively while State B is active.
    StateB = 2,

    /// Systems active exclusively during slow-motion mode transitions.
    Transition = 3,

    /// Total count of recognized system groups.
    Count = 4
};

} // namespace eng::world
