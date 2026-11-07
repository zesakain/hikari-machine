/// @file mode.h
/// @brief Abstract operational Mode interface for the Two-State Conductor architecture.
///
/// Purpose:
/// Defines the lifecycle and preparation contracts for engine execution modes.
/// Supports active gameplay execution, incremental budget-aware standby preparation,
/// and atomic commitment.
///
/// Ownership and Lifetime:
/// Concrete Mode instances are owned via std::unique_ptr<Mode> by the Conductor or Game shell.
/// Outlives individual transitions.
///
/// Thread-safety:
/// enter(), exit(), update(), commit(), and cancelPrepare() are executed on the main thread.
/// prepare() is budget-aware and executed on the main thread during standby frames.
///
/// Performance:
/// commit() must be extremely cheap (target <= 0.2 ms); it only validates speculative caches.
/// Heavy asset or calculation work belongs in prepare() spread over multiple frames.

#pragma once

#include "core/result.h"
#include "core/time.h"
#include "world/world.h"

#include <string_view>

namespace eng::framework {

/// @enum ModeState
/// @brief Current role of a mode within the Conductor.
enum class ModeState : uint8_t { Uninitialized = 0, Active = 1, Standby = 2 };

/// @enum PreparationStatus
/// @brief Incremental preparation execution status reported by prepare().
enum class PreparationStatus : uint8_t {
    /// Preparation is advancing but has not completed all speculative setup.
    InProgress = 0,

    /// Preparation has completed and speculative cache is ready for commit validation.
    Ready = 1,

    /// Preparation encountered an unrecoverable failure and aborted.
    Failed = 2
};

/// @class Mode
/// @brief Abstract operational execution state interface.
class Mode {
public:
    virtual ~Mode() = default;

    /// Invoked on the main thread when this mode transitions into the Active role.
    ///
    /// @param world Reference to the shared simulation World.
    /// @return Result indicating successful activation or initialization failure.
    [[nodiscard]] virtual Result<void> enter(world::World& world) = 0;

    /// Invoked every frame while this mode is in the Active role.
    ///
    /// @param world Reference to the shared simulation World.
    /// @param delta Delta time scaled according to active gameplay action clock.
    virtual void update(world::World& world, Duration delta) = 0;

    /// Invoked on the main thread when this mode leaves the Active role to enter Standby.
    ///
    /// @param world Reference to the shared simulation World.
    /// @return Result indicating successful deactivation.
    [[nodiscard]] virtual Result<void> exit(world::World& world) = 0;

    /// Advances speculative preparation incrementally while this mode is in the Standby role.
    ///
    /// Invariants:
    /// - Must yield immediately when budget.isExhausted() evaluates to true.
    /// - Output is a speculative cache; must never mutate active shared world components directly.
    ///
    /// @param world Reference to the shared simulation World.
    /// @param budget Execution time budget allotted for this frame.
    /// @return PreparationStatus indicating InProgress, Ready, or Failed.
    [[nodiscard]] virtual Result<PreparationStatus> prepare(world::World& world,
                                                            const Budget& budget) = 0;

    /// Validates the speculative cache and installs mode-specific state into the shared world.
    ///
    /// Preconditions: prepare() has returned PreparationStatus::Ready.
    /// Postconditions: All referenced entities are validated. If any entity is stale or missing,
    ///                 handles it safely or returns failure to trigger an atomic rollback.
    ///
    /// @param world Reference to the shared simulation World.
    /// @return Result indicating commit validation success or failure.
    [[nodiscard]] virtual Result<void> commit(world::World& world) = 0;

    /// Aborts active standby preparation and clears speculative cache.
    virtual void cancelPrepare() = 0;

    /// Returns human-readable diagnostic name of this mode.
    [[nodiscard]] virtual std::string_view getName() const noexcept = 0;

    /// Returns the current runtime role of this mode.
    [[nodiscard]] virtual ModeState getModeState() const noexcept = 0;
};

} // namespace eng::framework
