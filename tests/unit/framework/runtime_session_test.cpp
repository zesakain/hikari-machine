/// @file runtime_session_test.cpp
/// @brief Comprehensive regression test suite for RuntimeSession, Simulation State Machine,
/// and World Isolation.

#include "core/log.h"
#include "framework/conductor.h"
#include "framework/runtime_session.h"
#include "framework/scene.h"
#include "physics/physics_components.h"
#include "script/script_components.h"
#include "world/world.h"

#include <cmath>
#include <iostream>

namespace {

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << '\n'; \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

using namespace eng;
using namespace eng::framework;
using namespace eng::world;

int testStateMachineTransitions() {
    World editWorld;
    RuntimeSession session;
    session.setEditWorld(&editWorld);

    // 1. Initial State must be Edit
    TEST_CHECK(session.getSimulationState() == SimulationState::Edit);
    TEST_CHECK(!session.isSimulating());
    TEST_CHECK(session.getActiveWorld() == &editWorld);
    TEST_CHECK(session.getRuntimeWorld() == nullptr);

    // 2. Invalid transitions from Edit
    auto pauseFromEdit = session.pause();
    TEST_CHECK(pauseFromEdit.isError());
    TEST_CHECK(session.getSimulationState() == SimulationState::Edit);

    auto resumeFromEdit = session.resume();
    TEST_CHECK(resumeFromEdit.isError());
    TEST_CHECK(session.getSimulationState() == SimulationState::Edit);

    auto stepFromEdit = session.step();
    TEST_CHECK(stepFromEdit.isError());

    // 3. Edit -> Playing
    auto playRes = session.play();
    TEST_CHECK(playRes.hasValue());
    TEST_CHECK(session.getSimulationState() == SimulationState::Playing);
    TEST_CHECK(session.isSimulating());
    TEST_CHECK(session.getRuntimeWorld() != nullptr);
    TEST_CHECK(session.getActiveWorld() == session.getRuntimeWorld());
    TEST_CHECK(session.getActiveWorld() != &editWorld);

    // 4. Repeated play when already Playing must return error
    auto repeatPlay = session.play();
    TEST_CHECK(repeatPlay.isError());
    TEST_CHECK(session.getSimulationState() == SimulationState::Playing);

    // 5. Playing -> Paused
    auto pauseRes = session.pause();
    TEST_CHECK(pauseRes.hasValue());
    TEST_CHECK(session.getSimulationState() == SimulationState::Paused);
    TEST_CHECK(session.isSimulating());
    TEST_CHECK(session.getActiveWorld() == session.getRuntimeWorld());

    // 6. Step while Paused
    auto stepRes = session.step(Duration::fromMilliseconds(16));
    TEST_CHECK(stepRes.hasValue());
    TEST_CHECK(session.getSimulationState() == SimulationState::Paused);

    // 7. Paused -> Playing (Resume)
    auto resumeRes = session.resume();
    TEST_CHECK(resumeRes.hasValue());
    TEST_CHECK(session.getSimulationState() == SimulationState::Playing);

    // 8. Playing -> Paused -> Play (Play when Paused also transitions to Playing)
    (void)session.pause();
    TEST_CHECK(session.getSimulationState() == SimulationState::Paused);
    auto playFromPause = session.play();
    TEST_CHECK(playFromPause.hasValue());
    TEST_CHECK(session.getSimulationState() == SimulationState::Playing);

    // 9. Playing -> Stopped (returns to Edit)
    auto stopRes = session.stop();
    TEST_CHECK(stopRes.hasValue());
    TEST_CHECK(session.getSimulationState() == SimulationState::Edit);
    TEST_CHECK(!session.isSimulating());
    TEST_CHECK(session.getRuntimeWorld() == nullptr);
    TEST_CHECK(session.getActiveWorld() == &editWorld);

    // 10. Repeated stop must be idempotent and succeed
    auto repeatStop = session.stop();
    TEST_CHECK(repeatStop.hasValue());
    TEST_CHECK(session.getSimulationState() == SimulationState::Edit);

    // 11. Stopped -> Playing -> Pause -> Stop
    auto playAgain = session.play();
    TEST_CHECK(playAgain.hasValue());
    (void)session.pause();
    TEST_CHECK(session.getSimulationState() == SimulationState::Paused);
    auto stopFromPause = session.stop();
    TEST_CHECK(stopFromPause.hasValue());
    TEST_CHECK(session.getSimulationState() == SimulationState::Edit);
    TEST_CHECK(session.getRuntimeWorld() == nullptr);

    std::cout << "[PASS] testStateMachineTransitions\n";
    return 0;
}

int testWorldIsolationOnPlayAndStop() {
    World editWorld;
    physics::registerPhysicsComponents(editWorld);

    // Create an entity in the Edit World
    auto eEdit = editWorld.createEntity("PlayerBox");
    editWorld.setComponent(
        eEdit, physics::PhysicsTransformComponent{
                   .transform =
                       Transform(Vec3(1.0F, 10.0F, 2.0F), Quat::identity(), Vec3(1.0F, 1.0F, 1.0F)),
               });
    editWorld.setComponent(eEdit, physics::RigidBodyComponent{
                                      .bodyHandle = {},
                                      .motionType = physics::MotionType::Dynamic,
                                      .collisionLayer = physics::CollisionLayer::kMoving,
                                  });
    editWorld.setComponent(eEdit,
                           physics::ColliderComponent{
                               .desc = physics::ColliderDesc::makeBox(Vec3(0.5F, 0.5F, 0.5F)),
                           });
    editWorld.setComponent(eEdit, StableIdComponent{1001});

    TEST_CHECK(editWorld.getAllEntities().size() == 1);

    RuntimeSession session;
    session.setEditWorld(&editWorld);

    // 1. Start simulation
    auto playRes = session.play();
    TEST_CHECK(playRes.hasValue());

    World* runtimeWorld = session.getRuntimeWorld();
    TEST_CHECK(runtimeWorld != nullptr);
    TEST_CHECK(runtimeWorld != &editWorld);
    TEST_CHECK(runtimeWorld->getAllEntities().size() == 1);

    // 2. Retrieve cloned entity in Runtime World
    auto runtimeEntities = runtimeWorld->getAllEntities();
    auto eRuntime = runtimeEntities[0];
    auto* rtTr = runtimeWorld->getComponent<physics::PhysicsTransformComponent>(eRuntime);
    TEST_CHECK(rtTr != nullptr);
    TEST_CHECK(rtTr->transform.position.y == 10.0F);

    // 3. Mutate Runtime World entity directly and via simulation updates
    auto* rtTrMut = runtimeWorld->getMutableComponent<physics::PhysicsTransformComponent>(eRuntime);
    rtTrMut->transform.position.y = -50.0F; // Huge modification during runtime

    // Spawn an ephemeral runtime entity
    auto ephemeralEntity = runtimeWorld->createEntity("RuntimeSpawnedBullet");
    (void)ephemeralEntity;
    TEST_CHECK(runtimeWorld->getAllEntities().size() == 2);

    // Step simulation frames
    for (int i = 0; i < 10; ++i) {
        session.update(Duration::fromMilliseconds(16));
    }

    // 4. VERIFY EDIT WORLD REMAINS COMPLETELY UNTOUCHED!
    TEST_CHECK(editWorld.getAllEntities().size() == 1);
    auto* editTr = editWorld.getComponent<physics::PhysicsTransformComponent>(eEdit);
    TEST_CHECK(editTr != nullptr);
    TEST_CHECK(editTr->transform.position.y == 10.0F); // Position is unchanged!
    TEST_CHECK(editTr->transform.position.x == 1.0F);
    TEST_CHECK(editTr->transform.position.z == 2.0F);

    // 5. Stop simulation and verify Edit World remains intact
    auto stopRes = session.stop();
    TEST_CHECK(stopRes.hasValue());
    TEST_CHECK(session.getRuntimeWorld() == nullptr);
    TEST_CHECK(session.getActiveWorld() == &editWorld);

    // Edit world still has its 1 entity with original coordinates
    TEST_CHECK(editWorld.getAllEntities().size() == 1);
    TEST_CHECK(editWorld.isAlive(eEdit));
    auto* editTrAfterStop = editWorld.getComponent<physics::PhysicsTransformComponent>(eEdit);
    TEST_CHECK(editTrAfterStop != nullptr);
    TEST_CHECK(editTrAfterStop->transform.position.y == 10.0F);

    std::cout << "[PASS] testWorldIsolationOnPlayAndStop\n";
    return 0;
}

int testSimulationPhysicsAndScriptExecution() {
    World editWorld;
    physics::registerPhysicsComponents(editWorld);

    // Create a dynamic falling sphere at y = 20.0F
    auto ball = editWorld.createEntity("FallingBall");
    editWorld.setComponent(
        ball, physics::PhysicsTransformComponent{
                  .transform =
                      Transform(Vec3(0.0F, 20.0F, 0.0F), Quat::identity(), Vec3(1.0F, 1.0F, 1.0F)),
              });
    editWorld.setComponent(ball, physics::RigidBodyComponent{
                                     .bodyHandle = {},
                                     .motionType = physics::MotionType::Dynamic,
                                     .collisionLayer = physics::CollisionLayer::kMoving,
                                 });
    editWorld.setComponent(ball, physics::ColliderComponent{
                                     .desc = physics::ColliderDesc::makeSphere(0.5F),
                                 });

    RuntimeSession session;
    session.setEditWorld(&editWorld);

    // 1. Play starts simulation
    TEST_CHECK(session.play().hasValue());
    World* rtWorld = session.getRuntimeWorld();
    auto rtEntities = rtWorld->getAllEntities();
    TEST_CHECK(rtEntities.size() == 1);
    auto rtBall = rtEntities[0];

    // 2. Advance 15 frames of simulation (15 * 16ms = 240ms)
    for (int i = 0; i < 15; ++i) {
        session.update(Duration::fromMilliseconds(16));
    }

    auto* posAfterFall = rtWorld->getComponent<physics::PhysicsTransformComponent>(rtBall);
    TEST_CHECK(posAfterFall != nullptr);
    float yAfterFall = posAfterFall->transform.position.y;
    // Under gravity (-9.81 m/s^2), dynamic body should have fallen: y < 20.0F
    TEST_CHECK(yAfterFall < 20.0F);

    // 3. Pause simulation
    TEST_CHECK(session.pause().hasValue());
    float yAtPause = posAfterFall->transform.position.y;

    // Advance frames while paused: physics MUST NOT step!
    for (int i = 0; i < 15; ++i) {
        session.update(Duration::fromMilliseconds(16));
    }

    auto* posDuringPause = rtWorld->getComponent<physics::PhysicsTransformComponent>(rtBall);
    TEST_CHECK(posDuringPause != nullptr);
    TEST_CHECK(posDuringPause->transform.position.y == yAtPause); // Frozen!

    // 4. Resume simulation: physics resumes falling!
    TEST_CHECK(session.resume().hasValue());
    for (int i = 0; i < 15; ++i) {
        session.update(Duration::fromMilliseconds(16));
    }

    auto* posAfterResume = rtWorld->getComponent<physics::PhysicsTransformComponent>(rtBall);
    TEST_CHECK(posAfterResume != nullptr);
    TEST_CHECK(posAfterResume->transform.position.y < yAtPause); // Fallen further!

    // 5. Stop and verify Edit World ball is still at initial y = 20.0F
    TEST_CHECK(session.stop().hasValue());
    auto* editBallPos = editWorld.getComponent<physics::PhysicsTransformComponent>(ball);
    TEST_CHECK(editBallPos != nullptr);
    TEST_CHECK(editBallPos->transform.position.y == 20.0F);

    std::cout << "[PASS] testSimulationPhysicsAndScriptExecution\n";
    return 0;
}

int testFailureHandlingAndCleanup() {
    RuntimeSession session;

    // 1. Play with null Edit World fails cleanly
    session.setEditWorld(nullptr);
    auto nullWorldPlay = session.play();
    TEST_CHECK(nullWorldPlay.isError());
    TEST_CHECK(session.getSimulationState() == SimulationState::Edit);
    TEST_CHECK(session.getRuntimeWorld() == nullptr);

    // 2. Play with valid edit world containing an entity
    World editWorld;
    auto e = editWorld.createEntity("ValidEntity");
    editWorld.setComponent(e, physics::PhysicsTransformComponent{
                                  .transform = Transform(Vec3(0.0F, 1.0F, 0.0F), Quat::identity(),
                                                         Vec3(1.0F, 1.0F, 1.0F)),
                              });
    session.setEditWorld(&editWorld);
    TEST_CHECK(session.play().hasValue());
    TEST_CHECK(session.getSimulationState() == SimulationState::Playing);

    // 3. Stop while paused cleans up completely
    TEST_CHECK(session.pause().hasValue());
    TEST_CHECK(session.stop().hasValue());
    TEST_CHECK(session.getSimulationState() == SimulationState::Edit);
    TEST_CHECK(session.getRuntimeWorld() == nullptr);

    std::cout << "[PASS] testFailureHandlingAndCleanup\n";
    return 0;
}

} // namespace

int main() {
    std::cout << "Running RuntimeSession unit tests...\n";
    if (testStateMachineTransitions() != 0)
        return 1;
    if (testWorldIsolationOnPlayAndStop() != 0)
        return 1;
    if (testSimulationPhysicsAndScriptExecution() != 0)
        return 1;
    if (testFailureHandlingAndCleanup() != 0)
        return 1;
    std::cout << "All RuntimeSession tests PASSED!\n";
    return 0;
}
