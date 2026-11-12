/// @file shared_world_conductor_test.cpp
/// @brief Behavioral regression tests verifying the Two-State Conductor over a Single Shared World.
///
/// Invariants verified:
/// 1. Exactly one authoritative World exists for gameplay states A and B.
/// 2. Switching between State A and State B operates on the same world identity (&world).
/// 3. Entity IDs and component states are preserved across mode switches (zero world cloning).
/// 4. Continuous simulation systems (SystemGroup::Continuous) remain active across transitions.
/// 5. Transition systems (SystemGroup::Transition) are active only during slow-motion transitions.
/// 6. StateContext stores mode-specific configuration independently of shared world ownership.
/// 7. Failed commit validation preserves active state authority without world corruption.

#include "core/time.h"
#include "framework/conductor.h"
#include "framework/mode.h"
#include "framework/runtime_session.h"
#include "framework/state_context.h"
#include "world/components.h"
#include "world/system_group.h"
#include "world/world.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

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

namespace {

class SharedWorldMockMode : public Mode {
public:
    explicit SharedWorldMockMode(std::string_view name) noexcept : name_(name) {}

    [[nodiscard]] std::string_view getName() const noexcept override { return name_; }

    [[nodiscard]] Result<void> enter(World& world) override {
        lastObservedWorld_ = &world;
        ++enterCount;
        return Result<void>::success();
    }

    void update(World& world, Duration /*delta*/) override {
        lastObservedWorld_ = &world;
        ++updateCount;
    }

    [[nodiscard]] Result<void> exit(World& world) override {
        lastObservedWorld_ = &world;
        ++exitCount;
        return Result<void>::success();
    }

    [[nodiscard]] Result<PreparationStatus> prepare(World& world,
                                                    const Budget& /*budget*/) override {
        lastObservedWorld_ = &world;
        ++prepareCount;
        if (failPreparation) {
            return PreparationStatus::Failed;
        }
        return PreparationStatus::Ready;
    }

    [[nodiscard]] Result<void> commit(World& world) override {
        lastObservedWorld_ = &world;
        ++commitCount;
        if (failCommit) {
            return Error{.code = ErrorCode::CommitValidationFailed,
                         .message = "Simulated validation failure on speculative cache"};
        }
        return Result<void>::success();
    }

    void cancelPrepare() override { ++cancelPrepareCount; }

    [[nodiscard]] ModeState getModeState() const noexcept override { return ModeState::Active; }

    std::string_view name_;
    const World* lastObservedWorld_{nullptr};
    uint32_t enterCount{0};
    uint32_t updateCount{0};
    uint32_t exitCount{0};
    uint32_t prepareCount{0};
    uint32_t commitCount{0};
    uint32_t cancelPrepareCount{0};
    bool failPreparation{false};
    bool failCommit{false};
};

} // namespace

// 1. Verify single shared world identity across A -> B -> A transitions
static int testSingleSharedWorldIdentityAcrossTransitions() {
    World world;
    auto modeA = std::make_unique<SharedWorldMockMode>("State A");
    auto modeB = std::make_unique<SharedWorldMockMode>("State B");

    auto* rawA = modeA.get();
    auto* rawB = modeB.get();

    Conductor conductor(world, std::move(modeA), std::move(modeB));
    auto initRes = conductor.initialize(ActiveSlot::SlotA);
    TEST_CHECK(initRes.hasValue());

    // Both modes must observe the EXACT same World pointer as passed to Conductor
    TEST_CHECK(rawA->lastObservedWorld_ == &world);

    // Create persistent entities in the shared world during State A
    auto e1 = world.createEntity("SharedHero");
    auto e2 = world.createEntity("SharedEnemy");
    TEST_CHECK(world.isAlive(e1));
    TEST_CHECK(world.isAlive(e2));

    // Request transition to State B with auto-commit
    auto transRes1 = conductor.requestTransition(ActiveSlot::SlotB, true);
    TEST_CHECK(transRes1.hasValue());
    conductor.update(Duration::fromMilliseconds(16)); // prepare -> auto-commit -> Transitioning
    TEST_CHECK(conductor.getActiveSlot() == ActiveSlot::SlotB);

    // Verify State B observes the exact same world address
    TEST_CHECK(rawB->lastObservedWorld_ == &world);

    // Verify entities are still alive with exact same IDs across transition
    TEST_CHECK(world.isAlive(e1));
    TEST_CHECK(world.isAlive(e2));
    TEST_CHECK(world.getEntityName(e1) == "SharedHero");
    TEST_CHECK(world.getEntityName(e2) == "SharedEnemy");

    // Complete transition curve into steady State B
    conductor.update(Duration::fromMilliseconds(400));
    TEST_CHECK(conductor.getState() == ConductorState::Active);

    // Transition back from State B to State A
    auto transRes2 = conductor.requestTransition(ActiveSlot::SlotA, true);
    TEST_CHECK(transRes2.hasValue());
    conductor.update(Duration::fromMilliseconds(16));
    TEST_CHECK(conductor.getActiveSlot() == ActiveSlot::SlotA);

    // Entities must remain identical (zero cloning, zero recreation)
    TEST_CHECK(world.isAlive(e1));
    TEST_CHECK(world.isAlive(e2));
    TEST_CHECK(rawA->lastObservedWorld_ == &world);

    conductor.shutdown();
    std::cout << "[PASS] testSingleSharedWorldIdentityAcrossTransitions\n";
    return 0;
}

// 2. Verify continuous simulation systems remain active throughout all transitions
static int testContinuousSimulationPreservedAcrossTransitions() {
    World world;
    auto modeA = std::make_unique<SharedWorldMockMode>("State A");
    auto modeB = std::make_unique<SharedWorldMockMode>("State B");

    uint32_t continuousExecutionCount = 0;
    flecs::system continuousSys =
        world.getRawWorld().system<>("ContinuousCounter").kind(flecs::OnUpdate).run(
            [&](flecs::iter&) { ++continuousExecutionCount; });
    world.registerSystem(SystemGroup::Continuous, continuousSys);

    Conductor conductor(world, std::move(modeA), std::move(modeB));
    auto initRes = conductor.initialize(ActiveSlot::SlotA);
    TEST_CHECK(initRes.hasValue());

    // Continuous group must be active
    TEST_CHECK(world.isSystemGroupActive(SystemGroup::Continuous));
    TEST_CHECK(world.isSystemGroupActive(SystemGroup::StateA));
    TEST_CHECK(!world.isSystemGroupActive(SystemGroup::StateB));

    // Update frame in State A
    conductor.update(Duration::fromMilliseconds(16));
    TEST_CHECK(continuousExecutionCount == 1);

    // Transition to State B
    (void)conductor.requestTransition(ActiveSlot::SlotB, true);
    conductor.update(Duration::fromMilliseconds(16));
    TEST_CHECK(conductor.getActiveSlot() == ActiveSlot::SlotB);

    // Continuous system group must STILL be active
    TEST_CHECK(world.isSystemGroupActive(SystemGroup::Continuous));
    TEST_CHECK(!world.isSystemGroupActive(SystemGroup::StateA));
    TEST_CHECK(world.isSystemGroupActive(SystemGroup::StateB));

    // Update frame in State B: continuous system increments again
    conductor.update(Duration::fromMilliseconds(16));
    TEST_CHECK(continuousExecutionCount == 3);

    conductor.shutdown();
    std::cout << "[PASS] testContinuousSimulationPreservedAcrossTransitions\n";
    return 0;
}

// 3. Verify SystemGroup::Transition gating during slow-motion transition
static int testTransitionSystemGroupGating() {
    World world;
    auto modeA = std::make_unique<SharedWorldMockMode>("State A");
    auto modeB = std::make_unique<SharedWorldMockMode>("State B");

    uint32_t transitionExecutionCount = 0;
    flecs::system transSys =
        world.getRawWorld().system<>("TransitionCounter").kind(flecs::OnUpdate).run(
            [&](flecs::iter&) { ++transitionExecutionCount; });
    world.registerSystem(SystemGroup::Transition, transSys);

    ConductorConfig config{};
    config.slowMotionDuration = Duration::fromMilliseconds(200);
    Conductor conductor(world, std::move(modeA), std::move(modeB), config);
    auto initRes = conductor.initialize(ActiveSlot::SlotA);
    TEST_CHECK(initRes.hasValue());

    // Transition group must be inactive in steady State A
    TEST_CHECK(!world.isSystemGroupActive(SystemGroup::Transition));
    conductor.update(Duration::fromMilliseconds(16));
    TEST_CHECK(transitionExecutionCount == 0);

    // Request and commit transition
    (void)conductor.requestTransition(ActiveSlot::SlotB, true);
    conductor.update(Duration::fromMilliseconds(16)); // Enters Transitioning state
    TEST_CHECK(conductor.getState() == ConductorState::Transitioning);

    // Transition group must be ACTIVE during Transitioning
    TEST_CHECK(world.isSystemGroupActive(SystemGroup::Transition));
    conductor.update(Duration::fromMilliseconds(50));
    TEST_CHECK(transitionExecutionCount > 0);

    // Complete transition duration
    conductor.update(Duration::fromMilliseconds(250));
    TEST_CHECK(conductor.getState() == ConductorState::Active);

    // Transition group must be DEACTIVATED upon reaching steady Active state
    TEST_CHECK(!world.isSystemGroupActive(SystemGroup::Transition));

    conductor.shutdown();
    std::cout << "[PASS] testTransitionSystemGroupGating\n";
    return 0;
}

// 4. Verify StateContext separates mode configuration from shared world ownership
static int testStateContextSeparation() {
    World world;
    auto modeA = std::make_unique<SharedWorldMockMode>("State A");
    auto modeB = std::make_unique<SharedWorldMockMode>("State B");

    Conductor conductor(world, std::move(modeA), std::move(modeB));
    auto initRes = conductor.initialize(ActiveSlot::SlotA);
    TEST_CHECK(initRes.hasValue());

    auto camA = world.createEntity("CameraA");
    auto camB = world.createEntity("CameraB");
    auto targetA = world.createEntity("TargetA");

    // Configure State A context
    auto& ctxA = conductor.getStateContext(ActiveSlot::SlotA);
    ctxA.camera.activeCamera = camA;
    ctxA.camera.fovDegrees = 75.0F;
    ctxA.targeting.primaryTarget = targetA;
    ctxA.ui.documentPath = "ui/hud_state_a.rml";
    ctxA.input.contextTag = "ExplorationInput";

    // Configure State B context
    auto& ctxB = conductor.getStateContext(ActiveSlot::SlotB);
    ctxB.camera.activeCamera = camB;
    ctxB.camera.fovDegrees = 45.0F;
    ctxB.ui.documentPath = "ui/hud_state_b.rml";
    ctxB.input.contextTag = "CombatInput";

    // In State A, active context is Slot A
    TEST_CHECK(conductor.getActiveStateContext().camera.activeCamera == camA);
    TEST_CHECK(std::abs(conductor.getActiveStateContext().camera.fovDegrees - 75.0F) < 0.01F);
    TEST_CHECK(conductor.getActiveStateContext().input.contextTag == "ExplorationInput");

    // Standby context is Slot B
    TEST_CHECK(conductor.getStandbyStateContext().camera.activeCamera == camB);
    TEST_CHECK(std::abs(conductor.getStandbyStateContext().camera.fovDegrees - 45.0F) < 0.01F);
    TEST_CHECK(conductor.getStandbyStateContext().input.contextTag == "CombatInput");

    // Transition roles
    (void)conductor.requestTransition(ActiveSlot::SlotB, true);
    conductor.update(Duration::fromMilliseconds(16));
    TEST_CHECK(conductor.getActiveSlot() == ActiveSlot::SlotB);

    // Now active context must be Slot B, without having mutated Slot A
    TEST_CHECK(conductor.getActiveStateContext().camera.activeCamera == camB);
    TEST_CHECK(conductor.getActiveStateContext().input.contextTag == "CombatInput");
    TEST_CHECK(conductor.getStandbyStateContext().camera.activeCamera == camA);
    TEST_CHECK(conductor.getStandbyStateContext().input.contextTag == "ExplorationInput");

    conductor.shutdown();
    std::cout << "[PASS] testStateContextSeparation\n";
    return 0;
}

// 5. Verify failed validation aborts without corrupting active state or shared world
static int testFailedCommitPreservesActiveStateAndWorld() {
    World world;
    auto modeA = std::make_unique<SharedWorldMockMode>("State A");
    auto modeB = std::make_unique<SharedWorldMockMode>("State B");

    auto* rawA = modeA.get();
    auto* rawB = modeB.get();
    rawB->failCommit = true; // Inject validation failure

    Conductor conductor(world, std::move(modeA), std::move(modeB));
    auto initRes = conductor.initialize(ActiveSlot::SlotA);
    TEST_CHECK(initRes.hasValue());

    auto hero = world.createEntity("HeroEntity");
    TEST_CHECK(world.isAlive(hero));

    // Request transition
    auto reqRes = conductor.requestTransition(ActiveSlot::SlotB);
    TEST_CHECK(reqRes.hasValue());
    conductor.update(Duration::fromMilliseconds(16));
    TEST_CHECK(conductor.getState() == ConductorState::Prepared);

    // Commit transition: must fail and cleanly roll back
    auto execRes = conductor.executeTransition();
    TEST_CHECK(!execRes.hasValue());
    TEST_CHECK(execRes.error().code == ErrorCode::CommitValidationFailed);

    // State A must remain active and authoritative
    TEST_CHECK(conductor.getState() == ConductorState::Active);
    TEST_CHECK(conductor.getActiveSlot() == ActiveSlot::SlotA);
    TEST_CHECK(rawA->exitCount == 0); // Active mode was never exited

    // Shared world entity must be untouched
    TEST_CHECK(world.isAlive(hero));
    TEST_CHECK(world.getEntityName(hero) == "HeroEntity");

    conductor.shutdown();
    std::cout << "[PASS] testFailedCommitPreservesActiveStateAndWorld\n";
    return 0;
}

// 6. Verify non-isolated RuntimeSession operates directly on the shared world
static int testNonIsolatedRuntimeSessionPreservesSingleWorld() {
    World sharedWorld;
    auto e1 = sharedWorld.createEntity("SharedEntity");

    RuntimeSessionConfig config{};
    config.isolateWorld = false; // Direct shared world mode
    RuntimeSession session(config);
    session.setEditWorld(&sharedWorld);

    // In Edit mode, active world is sharedWorld
    TEST_CHECK(session.getActiveWorld() == &sharedWorld);

    // Play simulation directly on sharedWorld
    auto playRes = session.play();
    TEST_CHECK(playRes.hasValue());
    TEST_CHECK(session.isSimulating());
    TEST_CHECK(session.getActiveWorld() == &sharedWorld); // EXACT SAME WORLD

    // Update tick
    session.update(Duration::fromMilliseconds(16));
    TEST_CHECK(sharedWorld.isAlive(e1));

    // Stop simulation
    auto stopRes = session.stop();
    TEST_CHECK(stopRes.hasValue());
    TEST_CHECK(!session.isSimulating());
    TEST_CHECK(session.getActiveWorld() == &sharedWorld); // Not destroyed!
    TEST_CHECK(sharedWorld.isAlive(e1));

    std::cout << "[PASS] testNonIsolatedRuntimeSessionPreservesSingleWorld\n";
    return 0;
}

int main() {
    std::cout << "Running Shared-World Conductor Invariant Tests...\n";

    if (testSingleSharedWorldIdentityAcrossTransitions() != 0) return 1;
    if (testContinuousSimulationPreservedAcrossTransitions() != 0) return 1;
    if (testTransitionSystemGroupGating() != 0) return 1;
    if (testStateContextSeparation() != 0) return 1;
    if (testFailedCommitPreservesActiveStateAndWorld() != 0) return 1;
    if (testNonIsolatedRuntimeSessionPreservesSingleWorld() != 0) return 1;

    std::cout << "All Shared-World Conductor Invariant Tests PASSED!\n";
    return 0;
}
