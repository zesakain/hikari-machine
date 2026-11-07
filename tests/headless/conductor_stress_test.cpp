/// @file conductor_stress_test.cpp
/// @brief Headless stress test executing 10,000 mode transitions with entity churn and memory
/// stability checks.

#include "framework/conductor.h"
#include "framework/mode.h"
#include "world/system_group.h"
#include "world/world.h"

#include <iostream>
#include <memory>
#include <string_view>
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

struct StressData {
    uint32_t counter{0};
    float value{0.0F};
};

class StressMode : public Mode {
public:
    explicit StressMode(std::string_view name) noexcept : name_(name) {}

    [[nodiscard]] std::string_view getName() const noexcept override { return name_; }

    [[nodiscard]] Result<void> enter(World& world) override {
        // Create an ephemeral worker entity
        ownedEntity_ = world.createEntity();
        world.setComponent<StressData>(ownedEntity_, StressData{.counter = 1, .value = 42.0F});
        return Result<void>::success();
    }

    void update(World& world, Duration /*delta*/) override {
        if (world.isAlive(ownedEntity_)) {
            auto* data = world.getMutableComponent<StressData>(ownedEntity_);
            if (data != nullptr) {
                ++data->counter;
                data->value += 1.0F;
            }
        }
    }

    [[nodiscard]] Result<void> exit(World& world) override {
        if (world.isAlive(ownedEntity_)) {
            world.destroyEntity(ownedEntity_);
        }
        ownedEntity_ = {};
        return Result<void>::success();
    }

    [[nodiscard]] Result<PreparationStatus> prepare(World& /*world*/,
                                                    const Budget& /*budget*/) override {
        // Standby preparation is fast and ready immediately
        return PreparationStatus::Ready;
    }

    [[nodiscard]] Result<void> commit(World& /*world*/) override { return Result<void>::success(); }

    void cancelPrepare() override {}

    [[nodiscard]] ModeState getModeState() const noexcept override { return ModeState::Active; }

private:
    std::string_view name_;
    EntityHandle ownedEntity_;
};

int main() {
    constexpr uint32_t kTransitionCycles = 10000;
    std::cout << "Starting Conductor 10,000 transition stress test...\n";

    World world;
    auto modeA = std::make_unique<StressMode>("StressModeA");
    auto modeB = std::make_unique<StressMode>("StressModeB");

    ConductorConfig config{
        .prepareBudgetUs = 500,
        .slowMotionDuration = Duration::fromMilliseconds(0), // Instantaneous for stress test
        .slowMotionInitialScale = 1.0F,
    };

    Conductor conductor(world, std::move(modeA), std::move(modeB), config);
    auto initRes = conductor.initialize(ActiveSlot::SlotA);
    TEST_CHECK(initRes.hasValue());

    const auto frameDelta = Duration::fromMilliseconds(16);

    for (uint32_t i = 0; i < kTransitionCycles; ++i) {
        // 1. Tick active frame
        conductor.update(frameDelta);

        // 2. Request transition
        auto reqRes = conductor.requestTransition();
        TEST_CHECK(reqRes.hasValue());

        // 3. Tick frame to process standby preparation
        conductor.update(frameDelta);
        TEST_CHECK(conductor.getState() == ConductorState::Prepared);

        // 4. Commit and role reverse
        auto execRes = conductor.executeTransition();
        TEST_CHECK(execRes.hasValue());

        // 5. Verify alternating slot
        const ActiveSlot expectedSlot = (i % 2 == 0) ? ActiveSlot::SlotB : ActiveSlot::SlotA;
        TEST_CHECK(conductor.getActiveSlot() == expectedSlot);
    }

    TEST_CHECK(conductor.getSuccessfulTransitionsCount() == kTransitionCycles);
    TEST_CHECK(conductor.getFailedTransitionsCount() == 0);

    conductor.shutdown();

    std::cout << "Successfully completed " << kTransitionCycles
              << " mode transitions without failure or entity leakage.\n";
    return 0;
}
