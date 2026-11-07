/// @file conductor_types.h
/// @brief State enumerations and event definitions for the Two-State Conductor runtime.
///
/// Purpose:
/// Defines the authoritative states and lifecycle events for mode orchestration.
///
/// Ownership and Lifetime:
/// Value types and plain data event structs.
///
/// Thread-safety:
/// Value types; thread-safe for reading and dispatching.

#pragma once

#include "core/time.h"

#include <cstdint>
#include <string_view>

namespace eng::framework {

/// @enum ConductorState
/// @brief Authoritative lifecycle state of the Two-State Conductor.
enum class ConductorState : uint8_t {
    Uninitialized = 0,
    Active = 1,
    Preparing = 2,
    Prepared = 3,
    Committing = 4,
    Transitioning = 5,
    Terminated = 6
};

/// @enum ActiveSlot
/// @brief Identifier of the mode slot currently occupying the Active role.
enum class ActiveSlot : uint8_t { SlotA = 0, SlotB = 1 };

/// @struct ModeTransitionRequestedEvent
/// @brief Emitted when a mode transition begins standby preparation.
struct ModeTransitionRequestedEvent {
    ActiveSlot fromSlot;
    ActiveSlot toSlot;
    std::string_view targetModeName;
};

/// @struct ModeTransitionCommittedEvent
/// @brief Emitted when pre-commit validation succeeds and roles are reversed.
struct ModeTransitionCommittedEvent {
    ActiveSlot previousActiveSlot;
    ActiveSlot newActiveSlot;
    std::string_view activatedModeName;
};

/// @struct ModeTransitionCancelledEvent
/// @brief Emitted when an in-flight standby preparation or transition request is aborted.
struct ModeTransitionCancelledEvent {
    ActiveSlot currentActiveSlot;
    std::string_view activeModeName;
};

/// @struct ModeTransitionFailedEvent
/// @brief Emitted when commit validation fails and the active state is preserved via rollback.
struct ModeTransitionFailedEvent {
    ActiveSlot preservedActiveSlot;
    std::string_view reason;
};

} // namespace eng::framework
