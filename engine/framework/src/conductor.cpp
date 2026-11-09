/// @file conductor.cpp
/// @brief Two-State Conductor execution runtime implementation.

#include "framework/conductor.h"

#include "core/assert.h"
#include "core/log.h"

namespace eng::framework {

Conductor::Conductor(world::World& world, std::unique_ptr<Mode> modeA, std::unique_ptr<Mode> modeB,
                     const ConductorConfig& config) noexcept
    : world_(world), modeA_(std::move(modeA)), modeB_(std::move(modeB)), config_(config) {
    ENG_ASSERT(modeA_ != nullptr && modeB_ != nullptr,
               "Conductor requires valid Mode instances for both Slot A and Slot B");
}

Conductor::~Conductor() noexcept {
    shutdown();
}

Result<void> Conductor::initialize(ActiveSlot initialSlot) {
    if (state_ != ConductorState::Uninitialized) {
        return Error{.code = ErrorCode::InvalidState,
                     .message = "Conductor is already initialized"};
    }

    activeSlot_ = initialSlot;
    world_.setSystemGroupActive(world::SystemGroup::Continuous, true);
    world_.setSystemGroupActive(world::SystemGroup::StateA, activeSlot_ == ActiveSlot::SlotA);
    world_.setSystemGroupActive(world::SystemGroup::StateB, activeSlot_ == ActiveSlot::SlotB);

    auto enterRes = getActiveMode().enter(world_);
    if (!enterRes.hasValue()) {
        return enterRes;
    }

    state_ = ConductorState::Active;
    return Result<void>::success();
}

void Conductor::update(Duration realDelta) {
    if (state_ == ConductorState::Uninitialized || state_ == ConductorState::Terminated) {
        return;
    }

    // 1. Decoupled Clock updates
    worldClock_.tick(realDelta);
    uiClock_.tick(realDelta);

    // 2. Slow-motion transition curve evaluation
    if (state_ == ConductorState::Transitioning) {
        transitionElapsed_ = transitionElapsed_ + realDelta;
        if (transitionElapsed_ < config_.slowMotionDuration) {
            const auto elapsedUs = static_cast<float>(transitionElapsed_.asMicroseconds());
            const auto totalUs = static_cast<float>(config_.slowMotionDuration.asMicroseconds());
            const float t = (totalUs > 0.0F) ? (elapsedUs / totalUs) : 1.0F;
            const float initScale = config_.slowMotionInitialScale;
            const float currentScale = initScale + (t * (1.0F - initScale));
            actionClock_.setTimeScale(currentScale);
        } else {
            actionClock_.setTimeScale(1.0F);
            state_ = ConductorState::Active;
            ENG_LOG_FORMAT(LogLevel::Info, "Conductor",
                           "Transition curve complete: %.*s is now in steady Active state",
                           static_cast<int>(getActiveMode().getName().size()),
                           getActiveMode().getName().data());
        }
    }

    // 3. Tick active gameplay Action clock
    actionClock_.tick(realDelta);

    // 4. Update active mode
    getActiveMode().update(world_, actionClock_.deltaTime());

    // 5. Progress shared world simulation
    const auto dtSeconds = actionClock_.deltaTime().toSecondsF();
    world_.progress(dtSeconds);

    // 6. Incremental standby preparation
    if (state_ == ConductorState::Preparing) {
        const auto budget = Budget::fromMicroseconds(config_.prepareBudgetUs);
        auto prepRes = getStandbyMode().prepare(world_, budget);
        if (prepRes.hasValue()) {
            if (prepRes.value() == PreparationStatus::Ready) {
                state_ = ConductorState::Prepared;
                ENG_LOG_FORMAT(LogLevel::Info, "Conductor",
                               "Standby preparation complete for %.*s: Ready to commit transition",
                               static_cast<int>(getStandbyMode().getName().size()),
                               getStandbyMode().getName().data());
                if (pendingRequest_.valid && pendingRequest_.autoCommit) {
                    (void)executeTransition();
                }
            } else if (prepRes.value() == PreparationStatus::Failed) {
                ENG_LOG_FORMAT(
                    LogLevel::Warning, "Conductor",
                    "Standby mode preparation failed for %.*s; aborting transition request",
                    static_cast<int>(getStandbyMode().getName().size()),
                    getStandbyMode().getName().data());
                getStandbyMode().cancelPrepare();
                pendingRequest_ = {};
                state_ = ConductorState::Active;
                ++failedTransitions_;
            }
        } else {
            ENG_LOG_FORMAT(LogLevel::Warning, "Conductor",
                           "Standby mode preparation returned error for %.*s: %.*s; aborting "
                           "transition request",
                           static_cast<int>(getStandbyMode().getName().size()),
                           getStandbyMode().getName().data(),
                           static_cast<int>(prepRes.error().message.size()),
                           prepRes.error().message.data());
            getStandbyMode().cancelPrepare();
            pendingRequest_ = {};
            state_ = ConductorState::Active;
            ++failedTransitions_;
        }
    }
}

Result<void> Conductor::requestTransition(bool autoCommit) {
    const auto targetSlot =
        (activeSlot_ == ActiveSlot::SlotA) ? ActiveSlot::SlotB : ActiveSlot::SlotA;
    return requestTransition(targetSlot, autoCommit);
}

Result<void> Conductor::requestTransition(ActiveSlot targetSlot, bool autoCommit) {
    if (state_ == ConductorState::Uninitialized || state_ == ConductorState::Terminated) {
        ENG_LOG_FORMAT(LogLevel::Warning, "Conductor",
                       "Transition request rejected: Conductor is in state %s",
                       conductorStateToString(state_).data());
        return Error{.code = ErrorCode::InvalidState,
                     .message = "Conductor is not in an active state"};
    }

    if (state_ != ConductorState::Active || pendingRequest_.valid) {
        ENG_LOG_FORMAT(
            LogLevel::Warning, "Conductor",
            "Transition request rejected: transition already pending or in progress (state: %s)",
            conductorStateToString(state_).data());
        return Error{.code = ErrorCode::InvalidTransition,
                     .message =
                         "Transition requested while another transition is pending or in progress"};
    }

    if (targetSlot == activeSlot_) {
        ENG_LOG_FORMAT(LogLevel::Warning, "Conductor",
                       "Transition request rejected: target state (Slot %s) is already active",
                       (targetSlot == ActiveSlot::SlotA) ? "A" : "B");
        return Error{.code = ErrorCode::InvalidTransition,
                     .message = "Target state is already active"};
    }

    const auto fromSlot = activeSlot_;
    const auto toSlot = targetSlot;
    const auto fromName = getSlotDisplayName(fromSlot);
    const auto toName = getSlotDisplayName(toSlot);

    ENG_LOG_FORMAT(LogLevel::Info, "Conductor",
                   "Transition request received: from Slot %s (%.*s) to Slot %s (%.*s)",
                   (fromSlot == ActiveSlot::SlotA) ? "A" : "B", static_cast<int>(fromName.size()),
                   fromName.data(), (toSlot == ActiveSlot::SlotA) ? "A" : "B",
                   static_cast<int>(toName.size()), toName.data());

    ENG_LOG_FORMAT(LogLevel::Info, "Conductor",
                   "Transition validation result: SUCCESS (target: %.*s)",
                   static_cast<int>(toName.size()), toName.data());

    pendingRequest_ = PendingTransitionRequest{
        .fromSlot = fromSlot,
        .toSlot = toSlot,
        .autoCommit = autoCommit,
        .valid = true,
    };

    state_ = ConductorState::Preparing;

    ENG_LOG_FORMAT(LogLevel::Info, "Conductor",
                   "Transition queued: standby preparation in progress (autoCommit: %s)",
                   autoCommit ? "true" : "false");

    return Result<void>::success();
}

Result<void> Conductor::requestTransition(std::string_view targetStateName, bool autoCommit) {
    if (targetStateName.empty()) {
        ENG_LOG_WARN("Conductor", "Transition request rejected: target state name is empty");
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Missing target state: target state name is empty"};
    }

    std::optional<ActiveSlot> targetSlot;
    if (targetStateName == modeA_->getName() ||
        targetStateName == getSlotDisplayName(ActiveSlot::SlotA)) {
        targetSlot = ActiveSlot::SlotA;
    } else if (targetStateName == modeB_->getName() ||
               targetStateName == getSlotDisplayName(ActiveSlot::SlotB)) {
        targetSlot = ActiveSlot::SlotB;
    }

    if (!targetSlot.has_value()) {
        ENG_LOG_FORMAT(LogLevel::Warning, "Conductor",
                       "Transition request rejected: target state '%.*s' not found",
                       static_cast<int>(targetStateName.size()), targetStateName.data());
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Missing target state: state not found in Conductor slots"};
    }

    return requestTransition(targetSlot.value(), autoCommit);
}

bool Conductor::hasPendingTransition() const noexcept {
    return pendingRequest_.valid;
}

std::optional<ActiveSlot> Conductor::getPendingTargetSlot() const noexcept {
    if (pendingRequest_.valid) {
        return pendingRequest_.toSlot;
    }
    return std::nullopt;
}

Result<void> Conductor::executeTransition() {
    if (state_ != ConductorState::Prepared) {
        ENG_LOG_FORMAT(
            LogLevel::Warning, "Conductor",
            "Transition execution rejected: Conductor is not in Prepared state (current state: %s)",
            conductorStateToString(state_).data());
        return Error{.code = ErrorCode::InvalidTransition,
                     .message = "Cannot execute transition unless standby mode is Prepared"};
    }

    const auto fromSlot = activeSlot_;
    const auto toSlot = (activeSlot_ == ActiveSlot::SlotA) ? ActiveSlot::SlotB : ActiveSlot::SlotA;
    const auto fromName = getActiveMode().getName();
    const auto toName = getStandbyMode().getName();

    ENG_LOG_FORMAT(LogLevel::Info, "Conductor",
                   "Executing transition: %.*s (Slot %s) -> %.*s (Slot %s)",
                   static_cast<int>(fromName.size()), fromName.data(),
                   (fromSlot == ActiveSlot::SlotA) ? "A" : "B", static_cast<int>(toName.size()),
                   toName.data(), (toSlot == ActiveSlot::SlotA) ? "A" : "B");

    state_ = ConductorState::Committing;

    // Pre-commit validation of speculative cache
    auto commitRes = getStandbyMode().commit(world_);
    if (!commitRes.hasValue()) {
        ENG_LOG_FORMAT(LogLevel::Warning, "Conductor",
                       "W-0101: Commit validation failed on speculative cache for %.*s; transition "
                       "aborted: %.*s",
                       static_cast<int>(toName.size()), toName.data(),
                       static_cast<int>(commitRes.error().message.size()),
                       commitRes.error().message.data());
        getStandbyMode().cancelPrepare();
        pendingRequest_ = {};
        state_ = ConductorState::Active;
        ++failedTransitions_;
        return Error{.code = ErrorCode::CommitValidationFailed,
                     .message = "Commit validation failed; active state preserved via rollback"};
    }

    ENG_LOG_FORMAT(LogLevel::Info, "Conductor", "Transition validation result: SUCCESS for %.*s",
                   static_cast<int>(toName.size()), toName.data());

    // Validation succeeded: Perform atomic role reversal
    ENG_LOG_FORMAT(LogLevel::Info, "Conductor", "Source state exit: %.*s",
                   static_cast<int>(fromName.size()), fromName.data());
    auto exitRes = getActiveMode().exit(world_);
    (void)exitRes; // Mode exit cleanup

    activeSlot_ = toSlot;
    world_.setSystemGroupActive(world::SystemGroup::StateA, activeSlot_ == ActiveSlot::SlotA);
    world_.setSystemGroupActive(world::SystemGroup::StateB, activeSlot_ == ActiveSlot::SlotB);

    ENG_LOG_FORMAT(LogLevel::Info, "Conductor", "Target state entry: %.*s (Active Role)",
                   static_cast<int>(toName.size()), toName.data());
    auto enterRes = getActiveMode().enter(world_);
    (void)enterRes;

    // Start transition slow-motion curve
    state_ = ConductorState::Transitioning;
    transitionElapsed_ = Duration::zero();
    actionClock_.setTimeScale(config_.slowMotionInitialScale);
    ++successfulTransitions_;
    pendingRequest_ = {};

    ENG_LOG_FORMAT(LogLevel::Info, "Conductor", "Active state after processing: %.*s (Slot %s)",
                   static_cast<int>(toName.size()), toName.data(),
                   (activeSlot_ == ActiveSlot::SlotA) ? "A" : "B");

    return Result<void>::success();
}

void Conductor::cancelTransition() {
    if (state_ == ConductorState::Preparing || state_ == ConductorState::Prepared) {
        ENG_LOG_FORMAT(
            LogLevel::Info, "Conductor", "Transition cancelled; reverting to active state %.*s",
            static_cast<int>(getActiveMode().getName().size()), getActiveMode().getName().data());
        getStandbyMode().cancelPrepare();
        pendingRequest_ = {};
        state_ = ConductorState::Active;
    }
}

void Conductor::shutdown() {
    if (state_ == ConductorState::Preparing || state_ == ConductorState::Prepared) {
        getStandbyMode().cancelPrepare();
        pendingRequest_ = {};
    }
    if (state_ != ConductorState::Uninitialized && state_ != ConductorState::Terminated) {
        auto exitRes = getActiveMode().exit(world_);
        (void)exitRes;
    }
    state_ = ConductorState::Terminated;
}

Mode& Conductor::getActiveMode() noexcept {
    return (activeSlot_ == ActiveSlot::SlotA) ? *modeA_ : *modeB_;
}

const Mode& Conductor::getActiveMode() const noexcept {
    return (activeSlot_ == ActiveSlot::SlotA) ? *modeA_ : *modeB_;
}

Mode& Conductor::getStandbyMode() noexcept {
    return (activeSlot_ == ActiveSlot::SlotA) ? *modeB_ : *modeA_;
}

const Mode& Conductor::getStandbyMode() const noexcept {
    return (activeSlot_ == ActiveSlot::SlotA) ? *modeB_ : *modeA_;
}

void Conductor::setSlotMetadata(ActiveSlot slot, StateMetadata metadata) noexcept {
    if (slot == ActiveSlot::SlotA) {
        config_.slotAMetadata = metadata;
    } else {
        config_.slotBMetadata = metadata;
    }
}

const StateMetadata& Conductor::getSlotMetadata(ActiveSlot slot) const noexcept {
    return (slot == ActiveSlot::SlotA) ? config_.slotAMetadata : config_.slotBMetadata;
}

std::string_view Conductor::getSlotDisplayName(ActiveSlot slot) const noexcept {
    const auto& meta = (slot == ActiveSlot::SlotA) ? config_.slotAMetadata : config_.slotBMetadata;
    if (!meta.displayName.empty()) {
        return meta.displayName;
    }
    const auto* mode = (slot == ActiveSlot::SlotA) ? modeA_.get() : modeB_.get();
    if (mode != nullptr) {
        std::string_view modeDisplay = mode->getDisplayName();
        if (!modeDisplay.empty()) {
            return modeDisplay;
        }
    }
    return (slot == ActiveSlot::SlotA) ? "State A" : "State B";
}

} // namespace eng::framework
