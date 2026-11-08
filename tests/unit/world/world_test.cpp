/// @file world_test.cpp
/// @brief Unit tests for World, EntityHandle, component operations, and system group phase gating.

#include "world/entity.h"
#include "world/system_group.h"
#include "world/world.h"

#include <iostream>

using namespace eng::world;

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << '\n'; \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

struct Position {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
};

struct Velocity {
    float dx{0.0F};
    float dy{0.0F};
    float dz{0.0F};
};

struct TagEnemy {};

static int testEntityLifecycle() {
    World world;

    EntityHandle e1 = world.createEntity();
    TEST_CHECK(e1.isValid());
    TEST_CHECK(world.isAlive(e1));

    EntityHandle e2 = world.createEntity();
    TEST_CHECK(e2.isValid());
    TEST_CHECK(e2 != e1);
    TEST_CHECK(world.isAlive(e2));

    world.destroyEntity(e1);
    TEST_CHECK(!world.isAlive(e1));
    TEST_CHECK(world.isAlive(e2));

    // Stale/invalid handle checks
    EntityHandle invalidHandle{};
    TEST_CHECK(!invalidHandle.isValid());
    TEST_CHECK(!world.isAlive(invalidHandle));

    return 0;
}

static int testComponentStorageAndAccess() {
    World world;
    EntityHandle e = world.createEntity();

    TEST_CHECK(!world.hasComponent<Position>(e));
    TEST_CHECK(world.getComponent<Position>(e) == nullptr);

    world.setComponent<Position>(e, Position{.x = 10.0F, .y = 20.0F, .z = 30.0F});
    TEST_CHECK(world.hasComponent<Position>(e));

    const auto* pos = world.getComponent<Position>(e);
    TEST_CHECK(pos != nullptr);
    TEST_CHECK(pos->x == 10.0F);
    TEST_CHECK(pos->y == 20.0F);
    TEST_CHECK(pos->z == 30.0F);

    // Multi-component support
    world.setComponent<Velocity>(e, Velocity{.dx = 1.0F, .dy = 2.0F, .dz = 3.0F});
    TEST_CHECK(world.hasComponent<Velocity>(e));

    // Tag component
    world.setComponent<TagEnemy>(e, TagEnemy{});
    TEST_CHECK(world.hasComponent<TagEnemy>(e));

    // Remove component
    world.removeComponent<Position>(e);
    TEST_CHECK(!world.hasComponent<Position>(e));
    TEST_CHECK(world.getComponent<Position>(e) == nullptr);
    TEST_CHECK(world.hasComponent<Velocity>(e));
    TEST_CHECK(world.hasComponent<TagEnemy>(e));

    // Stale / destroyed entity
    world.destroyEntity(e);
    TEST_CHECK(!world.isAlive(e));
    TEST_CHECK(world.getComponent<Position>(e) == nullptr);
    TEST_CHECK(!world.hasComponent<Position>(e));

    return 0;
}

static int testSystemGroupPhaseGating() {
    World world;

    EntityHandle e = world.createEntity();
    world.setComponent<Position>(e, Position{.x = 0.0F, .y = 0.0F, .z = 0.0F});

    uint32_t continuousTicks = 0;
    uint32_t stateATicks = 0;
    uint32_t stateBTicks = 0;

    auto sysCont = world.getRawWorld().system<Position>().each(
        [&continuousTicks](Position&) { ++continuousTicks; });
    world.registerSystem(SystemGroup::Continuous, sysCont);

    auto sysA =
        world.getRawWorld().system<Position>().each([&stateATicks](Position&) { ++stateATicks; });
    world.registerSystem(SystemGroup::StateA, sysA);

    auto sysB =
        world.getRawWorld().system<Position>().each([&stateBTicks](Position&) { ++stateBTicks; });
    world.registerSystem(SystemGroup::StateB, sysB);

    // Phase 1: Continuous + State A active; State B disabled
    world.setSystemGroupActive(SystemGroup::Continuous, true);
    world.setSystemGroupActive(SystemGroup::StateA, true);
    world.setSystemGroupActive(SystemGroup::StateB, false);

    TEST_CHECK(world.isSystemGroupActive(SystemGroup::Continuous));
    TEST_CHECK(world.isSystemGroupActive(SystemGroup::StateA));
    TEST_CHECK(!world.isSystemGroupActive(SystemGroup::StateB));

    world.progress(0.016F);

    TEST_CHECK(continuousTicks == 1);
    TEST_CHECK(stateATicks == 1);
    TEST_CHECK(stateBTicks == 0);

    // Phase 2: Switch gating to State B; State A disabled
    world.setSystemGroupActive(SystemGroup::StateA, false);
    world.setSystemGroupActive(SystemGroup::StateB, true);

    world.progress(0.016F);

    TEST_CHECK(continuousTicks == 2);
    TEST_CHECK(stateATicks == 1);
    TEST_CHECK(stateBTicks == 1);

    // Phase 3: Disable Continuous as well
    world.setSystemGroupActive(SystemGroup::Continuous, false);

    world.progress(0.016F);

    TEST_CHECK(continuousTicks == 2);
    TEST_CHECK(stateATicks == 1);
    TEST_CHECK(stateBTicks == 2);

    return 0;
}

static int testEntityNaming() {
    World world;

    // 1. Unnamed entity
    EntityHandle unnamed = world.createEntity();
    TEST_CHECK(unnamed.isValid());
    TEST_CHECK(world.getEntityName(unnamed).empty());

    // 2. Named entity at creation
    EntityHandle named = world.createEntity("PlayerCharacter");
    TEST_CHECK(named.isValid());
    TEST_CHECK(world.getEntityName(named) == "PlayerCharacter");

    // 3. Rename existing entity
    world.setEntityName(named, "HeroKnight");
    TEST_CHECK(world.getEntityName(named) == "HeroKnight");

    // 4. Rename unnamed entity
    world.setEntityName(unnamed, "StaticObstacle");
    TEST_CHECK(world.getEntityName(unnamed) == "StaticObstacle");

    // 5. Invalid handle handling
    EntityHandle invalidHandle{};
    world.setEntityName(invalidHandle, "Invalid");
    TEST_CHECK(world.getEntityName(invalidHandle).empty());

    return 0;
}

static int testClearUserEntities() {
    World world;

    EntityHandle e1 = world.createEntity("Entity1");
    EntityHandle e2 = world.createEntity("Entity2");
    world.setComponent(e1, Position{1.0F, 2.0F, 3.0F});
    world.setComponent(e2, Velocity{0.5F, 0.0F, 0.0F});

    TEST_CHECK(world.isAlive(e1));
    TEST_CHECK(world.isAlive(e2));
    TEST_CHECK(world.getAllEntities().size() == 2);

    world.clearUserEntities();

    TEST_CHECK(!world.isAlive(e1));
    TEST_CHECK(!world.isAlive(e2));
    TEST_CHECK(world.getAllEntities().empty());

    // After clearing, new entities can still be created and mutated
    EntityHandle e3 = world.createEntity("Entity3");
    world.setComponent(e3, Position{10.0F, 20.0F, 30.0F});
    TEST_CHECK(world.isAlive(e3));
    TEST_CHECK(world.getComponent<Position>(e3)->x == 10.0F);
    TEST_CHECK(world.getAllEntities().size() == 1);

    return 0;
}

int main() {
    if (testEntityLifecycle() != 0) {
        return 1;
    }
    if (testComponentStorageAndAccess() != 0) {
        return 1;
    }
    if (testEntityNaming() != 0) {
        return 1;
    }
    if (testSystemGroupPhaseGating() != 0) {
        return 1;
    }
    if (testClearUserEntities() != 0) {
        return 1;
    }
    std::cout << "All World unit tests passed successfully.\n";
    return 0;
}
