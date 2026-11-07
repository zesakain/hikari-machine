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
            } else if (prepRes.value() == PreparationStatus::Failed) {
                ENG_LOG_WARN("Conductor",
                             "Standby mode preparation failed; aborting transition request");
                getStandbyMode().cancelPrepare();
                state_ = ConductorState::Active;
                ++failedTransitions_;
            }
        } else {
            ENG_LOG_WARN("Conductor",
                         "Standby mode preparation returned error; aborting transition request");
            getStandbyMode().cancelPrepare();
            state_ = ConductorState::Active;
            ++failedTransitions_;
        }
    }
}

Result<void> Conductor::requestTransition() {
    if (state_ != ConductorState::Active) {
        return Error{.code = ErrorCode::InvalidTransition,
                     .message = "Cannot request transition unless Conductor is in Active state"};
    }

    state_ = ConductorState::Preparing;
    return Result<void>::success();
}

Result<void> Conductor::executeTransition() {
    if (state_ != ConductorState::Prepared) {
        return Error{.code = ErrorCode::InvalidTransition,
                     .message = "Cannot execute transition unless standby mode is Prepared"};
    }

    state_ = ConductorState::Committing;

    // Pre-commit validation of speculative cache
    auto commitRes = getStandbyMode().commit(world_);
    if (!commitRes.hasValue()) {
        // Rollback: Preserve current active state completely
        ENG_LOG_WARN("Conductor",
                     "W-0101: Commit validation failed on speculative cache; transition aborted");
        getStandbyMode().cancelPrepare();
        state_ = ConductorState::Active;
        ++failedTransitions_;
        return Error{.code = ErrorCode::CommitValidationFailed,
                     .message = "Commit validation failed; active state preserved via rollback"};
    }

    // Validation succeeded: Perform atomic role reversal
    auto exitRes = getActiveMode().exit(world_);
    (void)exitRes; // Mode exit cleanup

    activeSlot_ = (activeSlot_ == ActiveSlot::SlotA) ? ActiveSlot::SlotB : ActiveSlot::SlotA;
    world_.setSystemGroupActive(world::SystemGroup::StateA, activeSlot_ == ActiveSlot::SlotA);
    world_.setSystemGroupActive(world::SystemGroup::StateB, activeSlot_ == ActiveSlot::SlotB);

    auto enterRes = getActiveMode().enter(world_);
    (void)enterRes;

    // Start transition slow-motion curve
    state_ = ConductorState::Transitioning;
    transitionElapsed_ = Duration::zero();
    actionClock_.setTimeScale(config_.slowMotionInitialScale);
    ++successfulTransitions_;

    return Result<void>::success();
}

void Conductor::cancelTransition() {
    if (state_ == ConductorState::Preparing || state_ == ConductorState::Prepared) {
        getStandbyMode().cancelPrepare();
        state_ = ConductorState::Active;
    }
}

void Conductor::shutdown() {
    if (state_ == ConductorState::Preparing || state_ == ConductorState::Prepared) {
        getStandbyMode().cancelPrepare();
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

} // namespace eng::framework
