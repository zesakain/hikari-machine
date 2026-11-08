/// @file conductor_test.cpp
/// @brief Unit tests for Two-State Conductor execution runtime, role reversal, and rollback safety.

#include "framework/conductor.h"
#include "framework/mode.h"
#include "world/system_group.h"
#include "world/world.h"

#include <iostream>
#include <memory>
#include <string_view>

using namespace eng;
using namespace eng::framework;
using namespace eng::world;

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << '\n'; \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

class MockMode : public Mode {
public:
    explicit MockMode(std::string_view name) noexcept : name_(name) {}

    [[nodiscard]] std::string_view getName() const noexcept override { return name_; }

    [[nodiscard]] Result<void> enter(World& /*world*/) override {
        ++enterCount;
        return Result<void>::success();
    }

    void update(World& /*world*/, Duration /*delta*/) override { ++updateCount; }

    [[nodiscard]] Result<void> exit(World& /*world*/) override {
        ++exitCount;
        return Result<void>::success();
    }

    [[nodiscard]] Result<PreparationStatus> prepare(World& /*world*/,
                                                    const Budget& /*budget*/) override {
        ++prepareCount;
        if (failPreparation) {
            return PreparationStatus::Failed;
        }

        if (prepareStep < framesRequiredForPreparation) {
            ++prepareStep;
            return PreparationStatus::InProgress;
        }

        return PreparationStatus::Ready;
    }

    [[nodiscard]] Result<void> commit(World& world) override {
        ++commitCount;
        if (failCommit) {
            return Error{.code = ErrorCode::CommitValidationFailed,
                         .message = "Simulated cache validation failure"};
        }

        if (validateTargetEntity.isValid() && !world.isAlive(validateTargetEntity)) {
            return Error{.code = ErrorCode::CommitValidationFailed,
                         .message = "Target entity was destroyed during standby preparation"};
        }

        return Result<void>::success();
    }

    void cancelPrepare() override {
        ++cancelPrepareCount;
        prepareStep = 0;
    }

    [[nodiscard]] ModeState getModeState() const noexcept override { return modeState_; }

    std::string_view name_;
    ModeState modeState_{ModeState::Uninitialized};
    uint32_t enterCount{0};
    uint32_t updateCount{0};
    uint32_t exitCount{0};
    uint32_t prepareCount{0};
    uint32_t commitCount{0};
    uint32_t cancelPrepareCount{0};

    uint32_t prepareStep{0};
    uint32_t framesRequiredForPreparation{0};
    bool failPreparation{false};
    bool failCommit{false};
    EntityHandle validateTargetEntity;
};

static int testConductorInitialization() {
    World world;
    auto modeA = std::make_unique<MockMode>("ModeA");
    auto modeB = std::make_unique<MockMode>("ModeB");

    auto* rawA = modeA.get();
    auto* rawB = modeB.get();

    Conductor conductor(world, std::move(modeA), std::move(modeB));
    TEST_CHECK(conductor.getState() == ConductorState::Uninitialized);

    auto initRes = conductor.initialize(ActiveSlot::SlotA);
    TEST_CHECK(initRes.hasValue());
    TEST_CHECK(conductor.getState() == ConductorState::Active);
    TEST_CHECK(conductor.getActiveSlot() == ActiveSlot::SlotA);

    TEST_CHECK(rawA->enterCount == 1);
    TEST_CHECK(rawB->enterCount == 0);

    TEST_CHECK(world.isSystemGroupActive(SystemGroup::Continuous));
    TEST_CHECK(world.isSystemGroupActive(SystemGroup::StateA));
    TEST_CHECK(!world.isSystemGroupActive(SystemGroup::StateB));

    conductor.update(Duration::fromMilliseconds(16));
    TEST_CHECK(rawA->updateCount == 1);
    TEST_CHECK(rawB->updateCount == 0);

    return 0;
}

static int testIncrementalPreparationAndCommit() {
    World world;
    auto modeA = std::make_unique<MockMode>("ModeA");
    auto modeB = std::make_unique<MockMode>("ModeB");

    auto* rawA = modeA.get();
    auto* rawB = modeB.get();

    // Configure Mode B to require 2 frames of standby preparation
    rawB->framesRequiredForPreparation = 2;

    Conductor conductor(world, std::move(modeA), std::move(modeB));
    (void)conductor.initialize(ActiveSlot::SlotA);

    // Request transition
    auto reqRes = conductor.requestTransition();
    TEST_CHECK(reqRes.hasValue());
    TEST_CHECK(conductor.getState() == ConductorState::Preparing);

    // Frame 1: standby prepare in progress
    conductor.update(Duration::fromMilliseconds(16));
    TEST_CHECK(rawB->prepareCount == 1);
    TEST_CHECK(conductor.getState() == ConductorState::Preparing);
    TEST_CHECK(rawA->updateCount == 1);

    // Frame 2: standby prepare in progress
    conductor.update(Duration::fromMilliseconds(16));
    TEST_CHECK(rawB->prepareCount == 2);
    TEST_CHECK(conductor.getState() == ConductorState::Preparing);

    // Frame 3: standby prepare completes -> Ready
    conductor.update(Duration::fromMilliseconds(16));
    TEST_CHECK(rawB->prepareCount == 3);
    TEST_CHECK(conductor.getState() == ConductorState::Prepared);

    // Execute Transition
    auto execRes = conductor.executeTransition();
    TEST_CHECK(execRes.hasValue());

    // Role Reversal Verification
    TEST_CHECK(conductor.getActiveSlot() == ActiveSlot::SlotB);
    TEST_CHECK(rawA->exitCount == 1);
    TEST_CHECK(rawB->commitCount == 1);
    TEST_CHECK(rawB->enterCount == 1);

    // System group gating reversed
    TEST_CHECK(world.isSystemGroupActive(SystemGroup::Continuous));
    TEST_CHECK(!world.isSystemGroupActive(SystemGroup::StateA));
    TEST_CHECK(world.isSystemGroupActive(SystemGroup::StateB));

    TEST_CHECK(conductor.getSuccessfulTransitionsCount() == 1);
    TEST_CHECK(conductor.getState() == ConductorState::Transitioning);

    return 0;
}

static int testAtomicRollbackOnCommitValidationFailure() {
    World world;
    auto modeA = std::make_unique<MockMode>("ModeA");
    auto modeB = std::make_unique<MockMode>("ModeB");

    auto* rawA = modeA.get();
    auto* rawB = modeB.get();

    // Create target entity in world
    EntityHandle target = world.createEntity();
    rawB->validateTargetEntity = target;
    rawB->framesRequiredForPreparation = 0; // ready on first call

    Conductor conductor(world, std::move(modeA), std::move(modeB));
    (void)conductor.initialize(ActiveSlot::SlotA);

    // Standby prepares
    (void)conductor.requestTransition();
    conductor.update(Duration::fromMilliseconds(16));
    TEST_CHECK(conductor.getState() == ConductorState::Prepared);

    // Simulate entity destruction between prepare and commit
    world.destroyEntity(target);
    TEST_CHECK(!world.isAlive(target));

    // Execute transition: must fail and rollback atomically
    auto execRes = conductor.executeTransition();
    TEST_CHECK(!execRes.hasValue());
    TEST_CHECK(execRes.error().code == ErrorCode::CommitValidationFailed);

    // Verify Active state is preserved completely
    TEST_CHECK(conductor.getActiveSlot() == ActiveSlot::SlotA);
    TEST_CHECK(conductor.getState() == ConductorState::Active);
    TEST_CHECK(rawA->exitCount == 0);          // Mode A never exited!
    TEST_CHECK(rawB->enterCount == 0);         // Mode B never entered!
    TEST_CHECK(rawB->cancelPrepareCount == 1); // Speculative cache discarded

    // System groups remain preserved
    TEST_CHECK(world.isSystemGroupActive(SystemGroup::StateA));
    TEST_CHECK(!world.isSystemGroupActive(SystemGroup::StateB));

    TEST_CHECK(conductor.getFailedTransitionsCount() == 1);
    TEST_CHECK(conductor.getSuccessfulTransitionsCount() == 0);

    // Active mode continues uninterrupted
    conductor.update(Duration::fromMilliseconds(16));
    TEST_CHECK(rawA->updateCount == 2);
    TEST_CHECK(rawB->updateCount == 0);

    return 0;
}

static int testCancelTransition() {
    World world;
    auto modeA = std::make_unique<MockMode>("ModeA");
    auto modeB = std::make_unique<MockMode>("ModeB");

    auto* rawB = modeB.get();
    rawB->framesRequiredForPreparation = 5;

    Conductor conductor(world, std::move(modeA), std::move(modeB));
    (void)conductor.initialize(ActiveSlot::SlotA);

    (void)conductor.requestTransition();
    conductor.update(Duration::fromMilliseconds(16));
    TEST_CHECK(conductor.getState() == ConductorState::Preparing);

    conductor.cancelTransition();
    TEST_CHECK(conductor.getState() == ConductorState::Active);
    TEST_CHECK(rawB->cancelPrepareCount == 1);

    return 0;
}

static int testGenericStateIdentityWithoutMetadata() {
    World world;
    auto modeA = std::make_unique<MockMode>("ModeA");
    auto modeB = std::make_unique<MockMode>("ModeB");

    Conductor conductor(world, std::move(modeA), std::move(modeB));
    (void)conductor.initialize(ActiveSlot::SlotA);

    // 1. Initial State: Slot A is Active, Slot B is Standby
    TEST_CHECK(conductor.getActiveSlot() == ActiveSlot::SlotA);
    TEST_CHECK(conductor.getSlotDisplayName(ActiveSlot::SlotA) == "State A");
    TEST_CHECK(conductor.getSlotDisplayName(ActiveSlot::SlotB) == "State B");
    TEST_CHECK(conductor.getSlotMetadata(ActiveSlot::SlotA).displayName.empty());
    TEST_CHECK(conductor.getSlotMetadata(ActiveSlot::SlotB).displayName.empty());
    TEST_CHECK(conductor.getActiveMode().getName() == "ModeA");
    TEST_CHECK(conductor.getStandbyMode().getName() == "ModeB");
    TEST_CHECK(conductor.getModeA().getName() == "ModeA");
    TEST_CHECK(conductor.getModeB().getName() == "ModeB");

    // 2. Perform transition to Slot B
    (void)conductor.requestTransition();
    conductor.update(Duration::fromMilliseconds(16));
    TEST_CHECK(conductor.getState() == ConductorState::Prepared);

    (void)conductor.executeTransition();
    TEST_CHECK(conductor.getState() == ConductorState::Transitioning);

    // 3. Reversed Roles: Slot B is now Active, Slot A is Standby
    TEST_CHECK(conductor.getActiveSlot() == ActiveSlot::SlotB);
    TEST_CHECK(conductor.getActiveMode().getName() == "ModeB");
    TEST_CHECK(conductor.getStandbyMode().getName() == "ModeA");

    // Identities remain generically State A and State B
    TEST_CHECK(conductor.getSlotDisplayName(ActiveSlot::SlotA) == "State A");
    TEST_CHECK(conductor.getSlotDisplayName(ActiveSlot::SlotB) == "State B");

    return 0;
}

static int testUserDefinedStateMetadata() {
    World world;
    auto modeA = std::make_unique<MockMode>("ModeA");
    auto modeB = std::make_unique<MockMode>("ModeB");

    ConductorConfig config;
    config.slotAMetadata = StateMetadata{
        .displayName = "Overworld",
        .description = "Primary continuous exploration and navigation state",
    };
    config.slotBMetadata = StateMetadata{
        .displayName = "Tactical",
        .description = "Target selection and precision interaction mode",
    };

    Conductor conductor(world, std::move(modeA), std::move(modeB), config);
    (void)conductor.initialize(ActiveSlot::SlotA);

    // Verify configured user metadata
    TEST_CHECK(conductor.getSlotDisplayName(ActiveSlot::SlotA) == "Overworld");
    TEST_CHECK(conductor.getSlotDisplayName(ActiveSlot::SlotB) == "Tactical");
    TEST_CHECK(conductor.getSlotMetadata(ActiveSlot::SlotA).description ==
               "Primary continuous exploration and navigation state");
    TEST_CHECK(conductor.getSlotMetadata(ActiveSlot::SlotB).description ==
               "Target selection and precision interaction mode");

    // Dynamically update metadata at runtime
    conductor.setSlotMetadata(ActiveSlot::SlotA, StateMetadata{.displayName = "Combat",
                                                               .description = "Real-time action"});
    TEST_CHECK(conductor.getSlotDisplayName(ActiveSlot::SlotA) == "Combat");
    TEST_CHECK(conductor.getSlotMetadata(ActiveSlot::SlotA).description == "Real-time action");

    // Clearing display name falls back to generic "State A"
    conductor.setSlotMetadata(ActiveSlot::SlotA, StateMetadata{});
    TEST_CHECK(conductor.getSlotDisplayName(ActiveSlot::SlotA) == "State A");

    return 0;
}

int main() {
    if (testConductorInitialization() != 0) {
        return 1;
    }
    if (testIncrementalPreparationAndCommit() != 0) {
        return 1;
    }
    if (testAtomicRollbackOnCommitValidationFailure() != 0) {
        return 1;
    }
    if (testCancelTransition() != 0) {
        return 1;
    }
    if (testGenericStateIdentityWithoutMetadata() != 0) {
        return 1;
    }
    if (testUserDefinedStateMetadata() != 0) {
        return 1;
    }
    std::cout << "All Conductor unit tests passed successfully.\n";
    return 0;
}
