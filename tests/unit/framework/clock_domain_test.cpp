/// @file clock_domain_test.cpp
/// @brief Unit tests for Decoupled Clocks (World, Action, UI) and slow-motion transition curves.

#include "framework/conductor.h"
#include "framework/mode.h"
#include "world/world.h"

#include <cmath>
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

class SimpleMode : public Mode {
public:
    explicit SimpleMode(std::string_view name) noexcept : name_(name) {}
    [[nodiscard]] std::string_view getName() const noexcept override { return name_; }
    [[nodiscard]] Result<void> enter(World& /*world*/) override { return Result<void>::success(); }
    void update(World& /*world*/, Duration /*delta*/) override {}
    [[nodiscard]] Result<void> exit(World& /*world*/) override { return Result<void>::success(); }
    [[nodiscard]] Result<PreparationStatus> prepare(World& /*world*/,
                                                    const Budget& /*budget*/) override {
        return PreparationStatus::Ready;
    }
    [[nodiscard]] Result<void> commit(World& /*world*/) override { return Result<void>::success(); }
    void cancelPrepare() override {}
    [[nodiscard]] ModeState getModeState() const noexcept override { return ModeState::Active; }

private:
    std::string_view name_;
};

static int testDecoupledClockDomains() {
    World world;
    auto modeA = std::make_unique<SimpleMode>("ModeA");
    auto modeB = std::make_unique<SimpleMode>("ModeB");

    ConductorConfig config{
        .prepareBudgetUs = 500,
        .slowMotionDuration = Duration::fromMilliseconds(300),
        .slowMotionInitialScale = 0.2F,
    };

    Conductor conductor(world, std::move(modeA), std::move(modeB), config);
    (void)conductor.initialize(ActiveSlot::SlotA);

    // Initial state: all clocks at 1.0 scale
    TEST_CHECK(std::abs(conductor.getWorldClock().timeScale() - 1.0F) < 0.001F);
    TEST_CHECK(std::abs(conductor.getActionClock().timeScale() - 1.0F) < 0.001F);
    TEST_CHECK(std::abs(conductor.getUiClock().timeScale() - 1.0F) < 0.001F);

    // Advance 100ms
    conductor.update(Duration::fromMilliseconds(100));
    TEST_CHECK(conductor.getWorldClock().totalTime() == Duration::fromMilliseconds(100));
    TEST_CHECK(conductor.getActionClock().totalTime() == Duration::fromMilliseconds(100));
    TEST_CHECK(conductor.getUiClock().totalTime() == Duration::fromMilliseconds(100));

    // Request and prepare transition
    (void)conductor.requestTransition();
    conductor.update(Duration::fromMilliseconds(16));
    TEST_CHECK(conductor.getState() == ConductorState::Prepared);

    // Execute transition: slow-motion curve kicks off
    auto execRes = conductor.executeTransition();
    TEST_CHECK(execRes.hasValue());
    TEST_CHECK(conductor.getState() == ConductorState::Transitioning);

    // Initial scale on ActionClock should be 0.2f
    TEST_CHECK(std::abs(conductor.getActionClock().timeScale() - 0.2F) < 0.001F);
    // World and UI clocks must remain unscaled (1.0f)
    TEST_CHECK(std::abs(conductor.getWorldClock().timeScale() - 1.0F) < 0.001F);
    TEST_CHECK(std::abs(conductor.getUiClock().timeScale() - 1.0F) < 0.001F);

    // Advance 150ms (halfway through the 300ms curve)
    conductor.update(Duration::fromMilliseconds(150));
    TEST_CHECK(conductor.getState() == ConductorState::Transitioning);
    // Expected scale at halfway: 0.2 + 0.5 * (1.0 - 0.2) = 0.6
    TEST_CHECK(std::abs(conductor.getActionClock().timeScale() - 0.6F) < 0.05F);
    TEST_CHECK(std::abs(conductor.getWorldClock().timeScale() - 1.0F) < 0.001F);
    TEST_CHECK(std::abs(conductor.getUiClock().timeScale() - 1.0F) < 0.001F);

    // Advance another 160ms (surpassing 300ms total)
    conductor.update(Duration::fromMilliseconds(160));
    // Curve complete: Conductor transitions back to Active, scale restored to 1.0
    TEST_CHECK(conductor.getState() == ConductorState::Active);
    TEST_CHECK(std::abs(conductor.getActionClock().timeScale() - 1.0F) < 0.001F);
    TEST_CHECK(std::abs(conductor.getWorldClock().timeScale() - 1.0F) < 0.001F);
    TEST_CHECK(std::abs(conductor.getUiClock().timeScale() - 1.0F) < 0.001F);

    // Invariant: ActionClock accumulated simulation time is lower than WorldClock real elapsed time
    TEST_CHECK(conductor.getActionClock().totalTime() < conductor.getWorldClock().totalTime());

    return 0;
}

int main() {
    if (testDecoupledClockDomains() != 0) {
        return 1;
    }
    std::cout << "All Clock Domain unit tests passed successfully.\n";
    return 0;
}
