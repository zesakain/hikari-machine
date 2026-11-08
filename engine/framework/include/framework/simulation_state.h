/// @file simulation_state.h
/// @brief Explicit simulation lifecycle states for Hikari's Runtime Session.
///
/// Purpose:
/// Defines the explicit state machine controlling editor authoring and isolated runtime
/// simulation execution.
///
/// Invariants & Rules:
/// - Distinct enumeration values preventing ambiguous state combinations.
/// - Exception-free and RTTI-free.
/// - Thread-safe value type.

#pragma once

#include <cstdint>
#include <string_view>

namespace eng::framework {

/// @enum SimulationState
/// @brief Explicit state of the engine simulation session.
enum class SimulationState : uint8_t {
    /// Edit Mode: Authoring mode where scenes are modified. Simulation is not running.
    Edit = 0,

    /// Playing: Simulation is actively advancing in the isolated Runtime World.
    Playing = 1,

    /// Paused: Simulation is temporarily frozen; systems do not step. UI & inspection responsive.
    Paused = 2,

    /// Stopped: Simulation has terminated; Runtime World teardown complete.
    Stopped = 3,
};

/// Converts a SimulationState enum value to a human-readable display string.
[[nodiscard]] constexpr std::string_view simulationStateToString(SimulationState state) noexcept {
    switch (state) {
    case SimulationState::Edit:
        return "Edit";
    case SimulationState::Playing:
        return "Playing";
    case SimulationState::Paused:
        return "Paused";
    case SimulationState::Stopped:
        return "Stopped";
    }
    return "Unknown";
}

} // namespace eng::framework
