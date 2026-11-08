/// @file runtime_session.h
/// @brief Lifecycle manager for isolated runtime simulation sessions.
///
/// Purpose:
/// Coordinates execution between the authoritative Edit World and an isolated Runtime World.
/// Manages the Play -> Pause -> Resume -> Stop state machine, in-memory scene staging,
/// runtime physics/script/conductor setup, and clean resource destruction upon stop.
///
/// Guarantees:
/// - Absolute isolation: modifications during simulation (physics stepping, entity additions,
///   script executions, component edits) NEVER mutate the authoring Edit World.
/// - Atomic teardown: upon Stop, the Runtime World is cleanly destroyed, and the editor
///   returns to the exact Edit World state as before Play.
///
/// Ownership and Lifetime:
/// Owned by EditorEngine or application runner. Holds a non-owning pointer to the Edit World.
/// Owns the Runtime World, runtime PhysicsWorld, runtime ScriptEngine, and runtime Conductor.
///
/// Thread-safety:
/// Main thread only.
///
/// Performance:
/// Hot path inside update() performs zero dynamic allocations. In-memory serialization on Play
/// avoids disk I/O.
///
/// Example:
/// @code
/// RuntimeSession session;
/// session.setEditWorld(&editWorld);
/// auto playRes = session.play();
/// while (session.getSimulationState() == SimulationState::Playing) {
///     session.update(Duration::fromMilliseconds(16));
/// }
/// session.stop();
/// @endcode

#pragma once

#include "core/error.h"
#include "core/result.h"
#include "core/time.h"
#include "framework/conductor.h"
#include "framework/conductor_types.h"
#include "framework/mode.h"
#include "framework/simulation_state.h"
#include "physics/physics_world.h"

#include <functional>
#include <memory>
#include <utility>

namespace eng::world {
class World;
} // namespace eng::world

namespace eng::framework {

class Conductor;

/// @struct RuntimeSessionConfig
/// @brief Configuration settings for initializing a RuntimeSession.
struct RuntimeSessionConfig {
    /// Optional mode factory callback to instantiate custom Slot A and Slot B modes.
    /// If null, default generic runtime modes ("State A", "State B") are instantiated.
    std::function<std::pair<std::unique_ptr<Mode>, std::unique_ptr<Mode>>()> modeFactory{nullptr};

    /// Conductor configuration for the runtime simulation.
    ConductorConfig conductorConfig{};

    /// Physics world configuration for the runtime simulation.
    physics::PhysicsWorldSettings physicsSettings{};
};

/// @class RuntimeSession
/// @brief Primary engine orchestrator managing the isolated runtime world lifecycle.
class RuntimeSession {
public:
    explicit RuntimeSession(const RuntimeSessionConfig& config = {}) noexcept;
    ~RuntimeSession() noexcept;

    RuntimeSession(const RuntimeSession&) = delete;
    RuntimeSession& operator=(const RuntimeSession&) = delete;
    RuntimeSession(RuntimeSession&&) noexcept;
    RuntimeSession& operator=(RuntimeSession&&) noexcept;

    /// Sets the non-owning source Edit World.
    /// Preconditions: simulation state is Edit or Stopped.
    void setEditWorld(world::World* world) noexcept;

    /// Sets the non-owning Edit Conductor (optional, for Edit mode telemetry).
    void setEditConductor(Conductor* conductor) noexcept;

    /// Returns the currently configured Edit World.
    [[nodiscard]] world::World* getEditWorld() noexcept;
    [[nodiscard]] const world::World* getEditWorld() const noexcept;

    /// Returns the isolated Runtime World (nullptr when in Edit or Stopped state).
    [[nodiscard]] world::World* getRuntimeWorld() noexcept;
    [[nodiscard]] const world::World* getRuntimeWorld() const noexcept;

    /// Returns the active World based on the current simulation state.
    /// In Edit/Stopped: returns Edit World.
    /// In Playing/Paused: returns Runtime World.
    [[nodiscard]] world::World* getActiveWorld() noexcept;
    [[nodiscard]] const world::World* getActiveWorld() const noexcept;

    /// Returns the active Conductor for the current simulation state.
    /// In Playing/Paused: returns Runtime Conductor.
    /// In Edit/Stopped: returns Edit Conductor (or nullptr).
    [[nodiscard]] Conductor* getActiveConductor() noexcept;
    [[nodiscard]] const Conductor* getActiveConductor() const noexcept;

    /// Returns the current simulation state.
    [[nodiscard]] SimulationState getSimulationState() const noexcept;

    /// Checks whether simulation is currently active (either Playing or Paused).
    [[nodiscard]] bool isSimulating() const noexcept;

    /// Transitions to Playing: clones Edit World into a fresh Runtime World in memory,
    /// initializes runtime physics, scripting, and conductor, and starts simulation.
    ///
    /// Preconditions: State is Edit, Stopped, or Paused (acts as resume).
    /// Errors: InvalidTransition if already playing; InvalidArgument if edit world is null;
    ///         DeserializationFailed if scene staging fails.
    [[nodiscard]] Result<void> play();

    /// Transitions from Playing to Paused: halts simulation stepping.
    ///
    /// Preconditions: State is Playing.
    /// Errors: InvalidTransition if not playing.
    [[nodiscard]] Result<void> pause();

    /// Transitions from Paused to Playing: resumes simulation stepping.
    ///
    /// Preconditions: State is Paused.
    /// Errors: InvalidTransition if not paused.
    [[nodiscard]] Result<void> resume();

    /// Advances the simulation by a single discrete step while paused.
    ///
    /// Preconditions: State is Paused.
    /// Errors: InvalidTransition if not paused.
    [[nodiscard]] Result<void> step(Duration delta = Duration::fromMilliseconds(16));

    /// Stops simulation: tears down Runtime World and runtime systems,
    /// safely destroys all runtime resources, and returns to Edit state.
    /// Idempotent if already in Edit or Stopped.
    [[nodiscard]] Result<void> stop();

    /// Frame update called each frame: if Playing, ticks Conductor and steps World simulation.
    /// Performs zero allocations in the hot path.
    void update(Duration delta);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace eng::framework
