/// @file script_test.cpp
/// @brief Comprehensive test suite for the sandboxed Lua 5.4 scripting subsystem.

#include "script/script_components.h"
#include "script/script_engine.h"
#include "world/world.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

using namespace eng;
using namespace eng::script;

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << '\n'; \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)

static void testLifecycle() {
    ScriptEngine engine;
    TEST_CHECK(!engine.isInitialized());

    auto initRes = engine.initialize();
    TEST_CHECK(initRes.hasValue());
    TEST_CHECK(engine.isInitialized());

    engine.shutdown();
    TEST_CHECK(!engine.isInitialized());

    // Re-initialization should succeed cleanly
    initRes = engine.initialize();
    TEST_CHECK(initRes.hasValue());
    TEST_CHECK(engine.isInitialized());
    engine.shutdown();
    std::cout << "[PASS] testLifecycle\n";
}

static void testSandboxSecurity() {
    ScriptEngine engine;
    auto initRes = engine.initialize();
    TEST_CHECK(initRes.hasValue());

    // os library must be completely absent
    auto res = engine.executeString("return os == nil");
    TEST_CHECK(res.hasValue());
    TEST_CHECK(std::holds_alternative<bool>(res.value()));
    TEST_CHECK(std::get<bool>(res.value()) == true);

    // io library must be completely absent
    res = engine.executeString("return io == nil");
    TEST_CHECK(res.hasValue());
    TEST_CHECK(std::holds_alternative<bool>(res.value()));
    TEST_CHECK(std::get<bool>(res.value()) == true);

    // package library must be completely absent
    res = engine.executeString("return package == nil");
    TEST_CHECK(res.hasValue());
    TEST_CHECK(std::holds_alternative<bool>(res.value()));
    TEST_CHECK(std::get<bool>(res.value()) == true);

    // dofile and loadfile must be purged
    res = engine.executeString("return dofile == nil and loadfile == nil");
    TEST_CHECK(res.hasValue());
    TEST_CHECK(std::holds_alternative<bool>(res.value()));
    TEST_CHECK(std::get<bool>(res.value()) == true);

    engine.shutdown();
    std::cout << "[PASS] testSandboxSecurity\n";
}

static void testExecutionAndValues() {
    ScriptEngine engine;
    TEST_CHECK(engine.initialize().hasValue());

    // Basic arithmetic
    auto res = engine.executeString("return 40 + 2");
    TEST_CHECK(res.hasValue());
    TEST_CHECK(std::holds_alternative<double>(res.value()));
    TEST_CHECK(std::get<double>(res.value()) == 42.0);

    // String manipulation
    res = engine.executeString("return 'Hikari' .. ' Engine'");
    TEST_CHECK(res.hasValue());
    TEST_CHECK(std::holds_alternative<std::string>(res.value()));
    TEST_CHECK(std::get<std::string>(res.value()) == "Hikari Engine");

    // Syntax error handling
    res = engine.executeString("return 10 + ");
    TEST_CHECK(!res.hasValue());
    TEST_CHECK(res.error() == ScriptError::SyntaxError);

    // Global variable get/set
    auto setRes = engine.setGlobal("gravityMultiplier", ScriptValue(9.81));
    TEST_CHECK(setRes.hasValue());

    auto getRes = engine.getGlobal("gravityMultiplier");
    TEST_CHECK(getRes.hasValue());
    TEST_CHECK(std::holds_alternative<double>(getRes.value()));
    TEST_CHECK(std::abs(std::get<double>(getRes.value()) - 9.81) < 0.001);

    // Function calls
    res = engine.executeString(R"(
        function addThree(a, b, c)
            return a + b + c
        end
    )");
    TEST_CHECK(res.hasValue());

    ScriptValue args[3] = {ScriptValue(10.0), ScriptValue(20.0), ScriptValue(30.0)};
    auto callRes = engine.callFunction("addThree", args);
    TEST_CHECK(callRes.hasValue());
    TEST_CHECK(std::holds_alternative<double>(callRes.value()));
    TEST_CHECK(std::get<double>(callRes.value()) == 60.0);

    // Calling non-existent function
    auto missingRes = engine.callFunction("doesNotExist", {});
    TEST_CHECK(!missingRes.hasValue());
    TEST_CHECK(missingRes.error() == ScriptError::NotFound);

    engine.shutdown();
    std::cout << "[PASS] testExecutionAndValues\n";
}

static void testInstructionWatchdog() {
    ScriptEngine engine;
    ScriptConfig config;
    config.instructionLimit = 5'000; // Small limit to test watchdog
    TEST_CHECK(engine.initialize(config).hasValue());

    // Runaway script with infinite loop
    auto res = engine.executeString(R"(
        local counter = 0
        while true do
            counter = counter + 1
        end
    )");

    TEST_CHECK(!res.hasValue());
    TEST_CHECK(res.error() == ScriptError::BudgetExceeded);

    // Verify engine remains responsive after watchdog abort
    res = engine.executeString("return 123");
    TEST_CHECK(res.hasValue());
    TEST_CHECK(std::get<double>(res.value()) == 123.0);

    engine.shutdown();
    std::cout << "[PASS] testInstructionWatchdog\n";
}

static void testVec3Bindings() {
    ScriptEngine engine;
    TEST_CHECK(engine.initialize().hasValue());

    auto res = engine.executeString(R"(
        local v1 = Vec3(1.0, 2.0, 3.0)
        local v2 = Vec3(4.0, 5.0, 6.0)
        local sum = v1 + v2
        return sum
    )");

    TEST_CHECK(res.hasValue());
    TEST_CHECK(std::holds_alternative<Vec3>(res.value()));
    Vec3 sum = std::get<Vec3>(res.value());
    TEST_CHECK(sum.x == 5.0f && sum.y == 7.0f && sum.z == 9.0f);

    // Methods
    res = engine.executeString(R"(
        local v = Vec3(3.0, 0.0, 4.0)
        return v:length()
    )");
    TEST_CHECK(res.hasValue());
    TEST_CHECK(std::abs(std::get<double>(res.value()) - 5.0) < 0.001);

    engine.shutdown();
    std::cout << "[PASS] testVec3Bindings\n";
}

static void testEntityScriptComponentIntegration() {
    ScriptEngine engine;
    TEST_CHECK(engine.initialize().hasValue());

    world::World world;
    engine.bindWorld(&world);

    registerScriptComponents(world);
    registerScriptSystems(world, engine, world::SystemGroup::Continuous);

    // Load Lua script with an entity update hook
    auto loadRes = engine.executeString(R"(
        tickCounter = 0
        lastDelta = 0.0
        lastEntityId = 0

        function on_player_tick(entity, dt)
            tickCounter = tickCounter + 1
            lastDelta = dt
            lastEntityId = entity:id()
        end
    )");
    TEST_CHECK(loadRes.hasValue());

    auto entity = world.createEntity("Player");
    ScriptComponent scriptComp{};
    std::strncpy(scriptComp.scriptName, "on_player_tick", sizeof(scriptComp.scriptName) - 1);
    scriptComp.enabled = true;
    scriptComp.updateInterval = 0.0f; // update every tick
    world.setComponent(entity, scriptComp);

    // Step world simulation
    world.progress(0.016f);

    auto counterVal = engine.getGlobal("tickCounter");
    TEST_CHECK(counterVal.hasValue());
    TEST_CHECK(std::get<double>(counterVal.value()) == 1.0);

    auto entityIdVal = engine.getGlobal("lastEntityId");
    TEST_CHECK(entityIdVal.hasValue());
    TEST_CHECK(std::get<double>(entityIdVal.value()) == static_cast<double>(entity.id));

    // Step again
    world.progress(0.016f);
    counterVal = engine.getGlobal("tickCounter");
    TEST_CHECK(counterVal.hasValue());
    TEST_CHECK(std::get<double>(counterVal.value()) == 2.0);

    // Test error isolation: assign non-existent script name
    auto badEntity = world.createEntity("BadActor");
    ScriptComponent badScriptComp{};
    std::strncpy(badScriptComp.scriptName, "non_existent_func",
                 sizeof(badScriptComp.scriptName) - 1);
    badScriptComp.enabled = true;
    world.setComponent(badEntity, badScriptComp);

    // Progress should not crash or throw, but disable the failing component
    world.progress(0.016f);
    const auto* compAfter = world.getComponent<ScriptComponent>(badEntity);
    TEST_CHECK(compAfter != nullptr);
    TEST_CHECK(compAfter->enabled == false); // verified disabled!

    engine.unbindWorld();
    engine.shutdown();
    std::cout << "[PASS] testEntityScriptComponentIntegration\n";
}

static void testScriptEnableLifecycle() {
    world::World world;
    ScriptEngine engine;
    auto initRes = engine.initialize();
    TEST_CHECK(initRes.hasValue());
    engine.bindWorld(&world);

    registerScriptComponents(world);
    registerScriptSystems(world, engine, world::SystemGroup::Continuous);

    auto loadRes = engine.executeString(R"(
        entityA_ticks = 0
        entityB_ticks = 0

        function on_entityA_tick(entity, dt)
            entityA_ticks = entityA_ticks + 1
        end

        function on_entityB_tick(entity, dt)
            entityB_ticks = entityB_ticks + 1
        end
    )");
    TEST_CHECK(loadRes.hasValue());

    // 1. Create two entities with distinct script routines
    auto entityA = world.createEntity("EntityA");
    ScriptComponent compA{};
    std::strncpy(compA.scriptName, "on_entityA_tick", sizeof(compA.scriptName) - 1);
    compA.enabled = true;
    world.setComponent(entityA, compA);

    auto entityB = world.createEntity("EntityB");
    ScriptComponent compB{};
    std::strncpy(compB.scriptName, "on_entityB_tick", sizeof(compB.scriptName) - 1);
    compB.enabled = true;
    world.setComponent(entityB, compB);

    // 2. Both entities tick together
    world.progress(0.016f);
    auto valA = engine.getGlobal("entityA_ticks");
    auto valB = engine.getGlobal("entityB_ticks");
    TEST_CHECK(valA.hasValue() && std::get<double>(valA.value()) == 1.0);
    TEST_CHECK(valB.hasValue() && std::get<double>(valB.value()) == 1.0);

    // 3. Disable Entity B. Entity A must continue ticking, Entity B must halt.
    auto& ecs = world.getRawWorld();
    auto eViewB = ecs.entity(entityB.id);
    eViewB.get_mut<ScriptComponent>()->enabled = false;

    for (int i = 0; i < 3; ++i) {
        world.progress(0.016f);
    }

    valA = engine.getGlobal("entityA_ticks");
    valB = engine.getGlobal("entityB_ticks");
    TEST_CHECK(valA.hasValue() && std::get<double>(valA.value()) == 4.0);
    TEST_CHECK(valB.hasValue() &&
               std::get<double>(valB.value()) == 1.0); // unchanged while disabled

    // 4. Re-enable Entity B. Both entities resume ticking.
    eViewB.get_mut<ScriptComponent>()->enabled = true;
    world.progress(0.016f);

    valA = engine.getGlobal("entityA_ticks");
    valB = engine.getGlobal("entityB_ticks");
    TEST_CHECK(valA.hasValue() && std::get<double>(valA.value()) == 5.0);
    TEST_CHECK(valB.hasValue() && std::get<double>(valB.value()) == 2.0); // resumed and incremented

    // 5. Unassigned entity script component (empty string) does not fail or disable
    auto entityC = world.createEntity("EntityC");
    ScriptComponent compC{};
    compC.scriptName[0] = '\0';
    compC.enabled = true;
    world.setComponent(entityC, compC);

    world.progress(0.016f);
    const auto* compCAfter = world.getComponent<ScriptComponent>(entityC);
    TEST_CHECK(compCAfter != nullptr);
    TEST_CHECK(compCAfter->enabled == true);

    engine.unbindWorld();
    engine.shutdown();
    std::cout << "[PASS] testScriptEnableLifecycle\n";
}

int main() {
    std::cout << "Running Script subsystem unit tests...\n";
    testLifecycle();
    testSandboxSecurity();
    testExecutionAndValues();
    testInstructionWatchdog();
    testVec3Bindings();
    testEntityScriptComponentIntegration();
    testScriptEnableLifecycle();
    std::cout << "All Script subsystem tests PASSED!\n";
    return 0;
}
