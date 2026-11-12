/// @file conductor.h
/// @brief Central Two-State Conductor execution runtime and state transition orchestrator.
///
/// Purpose:
/// Coordinates execution between two alternating operational states (State A and State B)
/// over a single shared simulation World. Manages decoupled time domains (World, Action, UI),
/// incremental budget-aware standby preparation, atomic commit validation, and rollback safety.
///
/// Ownership and Lifetime:
/// Owned by the application engine runtime via std::unique_ptr<Conductor>.
/// Holds a reference to the shared World and owns Mode instances A and B.
///
/// Thread-safety:
/// Main thread only.
///
/// Performance:
/// Hot path execution inside update() performs zero dynamic heap allocations.

#pragma once

#include "core/result.h"
#include "core/time.h"
#include "framework/conductor_types.h"
#include "framework/mode.h"
#include "framework/state_context.h"
#include "world/world.h"

#include <memory>
#include <optional>

namespace eng::framework {

/// @struct ConductorConfig
/// @brief Configuration settings for Conductor preparation and transition curves.
struct ConductorConfig {
    /// Maximum execution time budget per frame for standby preparation in microseconds.
    uint32_t prepareBudgetUs{500};

    /// Total duration of the slow-motion entry transition curve.
    Duration slowMotionDuration{Duration::fromMilliseconds(300)};

    /// Initial time scale applied to ActionClock at the start of a transition entry curve.
    float slowMotionInitialScale{0.2F};

    /// Optional user-defined display and diagnostic metadata for Slot A.
    StateMetadata slotAMetadata{};

    /// Optional user-defined display and diagnostic metadata for Slot B.
    StateMetadata slotBMetadata{};
};

/// @class Conductor
/// @brief Orchestrator of the Two-State execution architecture.
class Conductor {
public:
    /// Constructs Conductor with references to shared World and owning Mode pointers.
    ///
    /// @param world Reference to shared simulation world.
    /// @param modeA Owning pointer to Mode for Slot A. Must not be null.
    /// @param modeB Owning pointer to Mode for Slot B. Must not be null.
    /// @param config Optional tuning configuration.
    explicit Conductor(world::World& world, std::unique_ptr<Mode> modeA,
                       std::unique_ptr<Mode> modeB, const ConductorConfig& config = {}) noexcept;

    ~Conductor() noexcept;

    Conductor(const Conductor&) = delete;
    Conductor& operator=(const Conductor&) = delete;
    Conductor(Conductor&&) noexcept = default;
    Conductor& operator=(Conductor&&) noexcept = default;

    /// Activates the initial mode slot and enables corresponding system groups.
    ///
    /// @param initialSlot Slot to designate as initially active (defaults to SlotA).
    /// @return Result indicating success or initial mode enter() error.
    [[nodiscard]] Result<void> initialize(ActiveSlot initialSlot = ActiveSlot::SlotA);

    /// Executes one frame of simulation, clock updates, mode ticking, and standby preparation.
    ///
    /// @param realDelta Elapsed unscaled wall-clock duration for this frame.
    void update(Duration realDelta);

    /// Signals the standby mode to begin incremental preparation for an upcoming transition.
    ///
    /// Preconditions: ConductorState is Active.
    /// Postconditions: ConductorState becomes Preparing.
    ///
    /// @param autoCommit If true, automatically commits the transition once standby preparation
    /// completes.
    /// @return Result indicating success or rejection error.
    [[nodiscard]] Result<void> requestTransition(bool autoCommit = false);

    /// Requests a transition to a specific target slot.
    ///
    /// Preconditions: ConductorState is Active and targetSlot != activeSlot().
    ///
    /// @param targetSlot Target slot to transition into.
    /// @param autoCommit If true, automatically commits the transition once standby preparation
    /// completes.
    /// @return Result indicating success or rejection error.
    [[nodiscard]] Result<void> requestTransition(ActiveSlot targetSlot, bool autoCommit = false);

    /// Requests a transition by target state name.
    ///
    /// Preconditions: ConductorState is Active and targetStateName matches a registered slot.
    ///
    /// @param targetStateName Name or display name of the target state.
    /// @param autoCommit If true, automatically commits the transition once standby preparation
    /// completes.
    /// @return Result indicating success or rejection error (InvalidArgument if missing/unknown
    /// target).
    [[nodiscard]] Result<void> requestTransition(std::string_view targetStateName,
                                                 bool autoCommit = false);

    /// Convenience overload for C-strings to prevent implicit conversion to bool.
    [[nodiscard]] Result<void> requestTransition(const char* targetStateName,
                                                 bool autoCommit = false) {
        return requestTransition(targetStateName != nullptr ? std::string_view(targetStateName)
                                                            : std::string_view{},
                                 autoCommit);
    }

    /// Checks if a transition request is currently pending.
    [[nodiscard]] bool hasPendingTransition() const noexcept;

    /// Returns the target slot of the currently pending transition, if any.
    [[nodiscard]] std::optional<ActiveSlot> getPendingTargetSlot() const noexcept;

    /// Atomically validates speculative cache, reverses mode roles, and starts transition curve.
    ///
    /// Preconditions: ConductorState is Prepared.
    /// Failure mode: If pre-commit validation fails, rolls back immediately, preserving the
    ///               active mode with zero state corruption.
    ///
    /// @return Result indicating success or CommitValidationFailed on abort.
    [[nodiscard]] Result<void> executeTransition();

    /// Cancels active standby preparation or ready transition, returning Conductor to Active.
    void cancelTransition();

    /// Gracefully exits active mode and tears down Conductor runtime.
    void shutdown();

    /// Returns current lifecycle state of the Conductor.
    [[nodiscard]] ConductorState getState() const noexcept { return state_; }

    /// Returns a reference to Mode in Slot A.
    [[nodiscard]] Mode& getModeA() noexcept { return *modeA_; }

    /// Returns a const reference to Mode in Slot A.
    [[nodiscard]] const Mode& getModeA() const noexcept { return *modeA_; }

    /// Returns a reference to Mode in Slot B.
    [[nodiscard]] Mode& getModeB() noexcept { return *modeB_; }

    /// Returns a const reference to Mode in Slot B.
    [[nodiscard]] const Mode& getModeB() const noexcept { return *modeB_; }

    /// Sets optional user-defined metadata for a state slot (e.g., custom display name).
    void setSlotMetadata(ActiveSlot slot, StateMetadata metadata) noexcept;

    /// Retrieves user-defined metadata for a state slot.
    [[nodiscard]] const StateMetadata& getSlotMetadata(ActiveSlot slot) const noexcept;

    /// Returns human-readable label for a state slot ("State A" / "State B", or user-defined
    /// displayName if set).
    [[nodiscard]] std::string_view getSlotDisplayName(ActiveSlot slot) const noexcept;

    /// Returns the currently active mode slot.
    [[nodiscard]] ActiveSlot getActiveSlot() const noexcept { return activeSlot_; }

    /// Returns a reference to the currently active Mode.
    [[nodiscard]] Mode& getActiveMode() noexcept;

    /// Returns a const reference to the currently active Mode.
    [[nodiscard]] const Mode& getActiveMode() const noexcept;

    /// Returns a reference to the standby Mode.
    [[nodiscard]] Mode& getStandbyMode() noexcept;

    /// Returns a const reference to the standby Mode.
    [[nodiscard]] const Mode& getStandbyMode() const noexcept;

    /// Accesses the continuous, unscaled World clock.
    [[nodiscard]] const Clock& getWorldClock() const noexcept { return worldClock_; }

    /// Accesses the active gameplay Action clock (scaled during transitions).
    [[nodiscard]] const Clock& getActionClock() const noexcept { return actionClock_; }

    /// Accesses the unscaled UI clock.
    [[nodiscard]] const Clock& getUiClock() const noexcept { return uiClock_; }

    /// Returns count of successfully committed transitions.
    [[nodiscard]] uint32_t getSuccessfulTransitionsCount() const noexcept {
        return successfulTransitions_;
    }

    /// Returns count of rejected/rolled-back transitions.
    [[nodiscard]] uint32_t getFailedTransitionsCount() const noexcept { return failedTransitions_; }

    /// Returns a const reference to the StateContext for a given slot.
    [[nodiscard]] const StateContext& getStateContext(ActiveSlot slot) const noexcept;

    /// Returns a mutable reference to the StateContext for a given slot.
    [[nodiscard]] StateContext& getStateContext(ActiveSlot slot) noexcept;

    /// Returns a const reference to the active StateContext.
    [[nodiscard]] const StateContext& getActiveStateContext() const noexcept;

    /// Returns a mutable reference to the active StateContext.
    [[nodiscard]] StateContext& getActiveStateContext() noexcept;

    /// Returns a const reference to the standby StateContext.
    [[nodiscard]] const StateContext& getStandbyStateContext() const noexcept;

    /// Returns a mutable reference to the standby StateContext.
    [[nodiscard]] StateContext& getStandbyStateContext() noexcept;

private:
    world::World& world_;
    std::unique_ptr<Mode> modeA_;
    std::unique_ptr<Mode> modeB_;
    ConductorConfig config_{};

    StateContext slotAContext_{};
    StateContext slotBContext_{};

    ConductorState state_{ConductorState::Uninitialized};
    ActiveSlot activeSlot_{ActiveSlot::SlotA};

    Clock worldClock_;
    Clock actionClock_;
    Clock uiClock_;

    Duration transitionElapsed_;
    uint32_t successfulTransitions_{0};
    uint32_t failedTransitions_{0};

    struct PendingTransitionRequest {
        ActiveSlot fromSlot{ActiveSlot::SlotA};
        ActiveSlot toSlot{ActiveSlot::SlotB};
        bool autoCommit{false};
        bool valid{false};
    };
    PendingTransitionRequest pendingRequest_{};
};

} // namespace eng::framework
