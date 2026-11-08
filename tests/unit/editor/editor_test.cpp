/// @file editor_test.cpp
/// @brief Comprehensive test suite for the Dear ImGui editor tooling subsystem.

#include "editor/editor_engine.h"
#include "editor/editor_theme.h"
#include "editor/hierarchy_panel.h"
#include "editor/selection_model.h"
#include "editor/workspace_manager.h"
#include "framework/conductor.h"
#include "framework/mode.h"
#include "physics/physics_components.h"
#include "platform/input_snapshot.h"
#include "rhi/command_list.h"
#include "rhi/rhi_device.h"
#include "script/script_components.h"
#include "script/script_engine.h"
#include "world/world.h"

#include <cstdlib>
#include <fstream>
#include <iostream>

using namespace eng;
using namespace eng::editor;

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << '\n'; \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)

// Minimal test mode for Conductor binding
class DummyEditorMode final : public framework::Mode {
public:
    explicit DummyEditorMode(std::string_view name) : name_(name) {}

    [[nodiscard]] std::string_view getName() const noexcept override { return name_; }

    [[nodiscard]] Result<void> enter(world::World&) override { return Result<void>::success(); }

    void update(world::World&, Duration) override {}

    [[nodiscard]] Result<void> exit(world::World&) override { return Result<void>::success(); }

    [[nodiscard]] Result<framework::PreparationStatus> prepare(world::World&,
                                                               const Budget&) override {
        return framework::PreparationStatus::Ready;
    }

    [[nodiscard]] Result<void> commit(world::World&) override { return Result<void>::success(); }

    void cancelPrepare() override {}

    [[nodiscard]] framework::ModeState getModeState() const noexcept override {
        return framework::ModeState::Active;
    }

private:
    std::string_view name_;
};

static void testEditorLifecycle() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto nullDevice = std::move(devRes.value());

    EditorEngine editor;
    TEST_CHECK(!editor.isInitialized());

    bool initOk = editor.initialize(*nullDevice);
    TEST_CHECK(initOk);
    TEST_CHECK(editor.isInitialized());

    editor.shutdown();
    TEST_CHECK(!editor.isInitialized());

    // Re-initialization
    initOk = editor.initialize(*nullDevice);
    TEST_CHECK(initOk);
    TEST_CHECK(editor.isInitialized());
    editor.shutdown();

    std::cout << "[PASS] testEditorLifecycle\n";
}

static void testEditorFrameAndPanels() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto nullDevice = std::move(devRes.value());

    auto cmdRes = nullDevice->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto nullCmd = std::move(cmdRes.value());

    world::World world;
    auto modeA = std::make_unique<DummyEditorMode>("ModeA");
    auto modeB = std::make_unique<DummyEditorMode>("ModeB");
    framework::Conductor conductor(world, std::move(modeA), std::move(modeB));
    TEST_CHECK(conductor.initialize().hasValue());

    EditorEngine editor;
    TEST_CHECK(editor.initialize(*nullDevice));

    editor.bindWorld(&world);
    editor.bindConductor(&conductor);

    // Create test entities in the world to verify hierarchy and inspector
    auto e1 = world.createEntity("TestEntity_Alpha");
    auto e2 = world.createEntity("TestEntity_Beta");
    TEST_CHECK(world.isAlive(e1));
    TEST_CHECK(world.isAlive(e2));

    // Run 3 editor frames
    for (int frame = 0; frame < 3; ++frame) {
        nullCmd->begin();
        editor.beginFrame(Duration::fromMilliseconds(16));
        editor.drawPanels();
        editor.endFrame(*nullCmd);
        nullCmd->end();
        TEST_CHECK(editor.lastDrawCallCount() > 0);
        TEST_CHECK(editor.lastVertexCount() > 0);
        TEST_CHECK(editor.lastIndexCount() > 0);
        TEST_CHECK(editor.executedEditorPasses() == static_cast<uint64_t>(frame + 1));
    }

    editor.shutdown();
    conductor.shutdown();

    std::cout << "[PASS] testEditorFrameAndPanels\n";
}

static void testEditorInputProcessing() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto nullDevice = std::move(devRes.value());

    auto cmdRes = nullDevice->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto nullCmd = std::move(cmdRes.value());

    EditorEngine editor;
    TEST_CHECK(editor.initialize(*nullDevice));

    platform::InputSnapshot input{};
    input.mouseX = 200.0f;
    input.mouseY = 150.0f;
    input.mouseButtons.set(static_cast<size_t>(platform::MouseButton::Left), true);

    editor.processInput(input);

    nullCmd->begin();
    editor.beginFrame(Duration::fromMilliseconds(16));
    editor.drawPanels();
    editor.endFrame(*nullCmd);
    nullCmd->end();

    editor.shutdown();
    std::cout << "[PASS] testEditorInputProcessing\n";
}

static void testPanelManagerAndRegistry() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto nullDevice = std::move(devRes.value());

    EditorEngine editor;
    TEST_CHECK(editor.initialize(*nullDevice));

    auto& wm = editor.getWorkspaceManager();
    TEST_CHECK(!wm.getPanels().empty());

    // Verify all standard panels exist in registry
    auto* hierarchy = wm.findPanel("hierarchy");
    TEST_CHECK(hierarchy != nullptr);
    TEST_CHECK(hierarchy->getId() == "hierarchy");
    TEST_CHECK(hierarchy->getDisplayName() == "Scene Hierarchy");
    TEST_CHECK(hierarchy->isOpen());

    auto* inspector = wm.findPanel("inspector");
    TEST_CHECK(inspector != nullptr);
    TEST_CHECK(inspector->getId() == "inspector");

    auto* viewport = wm.findPanel("viewport");
    TEST_CHECK(viewport != nullptr);

    auto* console = wm.findPanel("console");
    TEST_CHECK(console != nullptr);

    auto* metrics = wm.findPanel("metrics");
    TEST_CHECK(metrics != nullptr);

    auto* conductor = wm.findPanel("conductor");
    TEST_CHECK(conductor != nullptr);

    auto* contentBrowser = wm.findPanel("content_browser");
    TEST_CHECK(contentBrowser != nullptr);

    // Test non-existent panel
    TEST_CHECK(wm.findPanel("non_existent_panel") == nullptr);

    // Test visibility toggling
    hierarchy->setOpen(false);
    TEST_CHECK(!hierarchy->isOpen());
    hierarchy->setOpen(true);
    TEST_CHECK(hierarchy->isOpen());

    editor.shutdown();
    std::cout << "[PASS] testPanelManagerAndRegistry\n";
}

static void testWorkspacePersistenceAndRecovery() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto nullDevice = std::move(devRes.value());

    const char* testIniPath = "/tmp/hikari_test_workspace.ini";
    (void)std::remove(testIniPath);

    EditorEngine editor;
    TEST_CHECK(editor.initialize(*nullDevice));

    // 1. Save layout to custom path
    bool saveOk = editor.saveWorkspaceLayout(testIniPath);
    TEST_CHECK(saveOk);

    // 2. Load layout from custom path
    bool loadOk = editor.loadWorkspaceLayout(testIniPath);
    TEST_CHECK(loadOk);

    // 3. Test layout reset request
    TEST_CHECK(!editor.getWorkspaceManager().isLayoutResetPending());
    editor.resetWorkspaceLayout();
    TEST_CHECK(editor.getWorkspaceManager().isLayoutResetPending());

    // 4. Test recovery from corrupted / empty file
    const char* corruptIniPath = "/tmp/hikari_corrupt_workspace.ini";
    {
        std::ofstream corruptFile(corruptIniPath);
        corruptFile << "bad";
    }
    bool corruptHandled = editor.loadWorkspaceLayout(corruptIniPath);
    // Should return false and mark layout reset pending
    TEST_CHECK(!corruptHandled);
    TEST_CHECK(editor.getWorkspaceManager().isLayoutResetPending());

    (void)std::remove(testIniPath);
    (void)std::remove(corruptIniPath);

    editor.shutdown();
    std::cout << "[PASS] testWorkspacePersistenceAndRecovery\n";
}

static void testThemeTokens() {
    ThemeColors dark = ThemeColors::defaultDark();
    TEST_CHECK(dark.backgroundApp.a == 1.0F);
    TEST_CHECK(dark.accentPrimary.r > 0.0F);
    TEST_CHECK(dark.colorAxisX.r > 0.9F);
    TEST_CHECK(dark.colorAxisY.g > 0.7F);
    TEST_CHECK(dark.colorAxisZ.b > 0.9F);

    Color4f testColor(1.0F, 0.0F, 0.0F, 1.0F);
    uint32_t packed = testColor.toRgba32();
    TEST_CHECK((packed & 0xFFU) == 255U); // Red component

    std::cout << "[PASS] testThemeTokens\n";
}

static void testResponsiveViewports() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto nullDevice = std::move(devRes.value());

    auto cmdRes = nullDevice->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto nullCmd = std::move(cmdRes.value());

    EditorEngine editor;
    TEST_CHECK(editor.initialize(*nullDevice));

    // Test responsive resizing across multiple standard desktop resolutions
    struct Resolution {
        uint32_t w;
        uint32_t h;
    };
    const Resolution resolutions[] = {
        {1280, 720},  // 720p HD
        {1366, 768},  // Common Laptop
        {1600, 900},  // HD+
        {1920, 1080}, // 1080p FHD
        {2560, 1440}, // 1440p QHD
    };

    for (const auto& res : resolutions) {
        nullCmd->begin();
        editor.beginFrame(Duration::fromMilliseconds(16), res.w, res.h);
        editor.drawPanels();
        editor.endFrame(*nullCmd);
        nullCmd->end();

        TEST_CHECK(editor.lastDrawCallCount() > 0);
        TEST_CHECK(editor.lastVertexCount() > 0);
    }

    editor.shutdown();
    std::cout << "[PASS] testResponsiveViewports\n";
}

static void testGenericConductorStateIdentity() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto nullDevice = std::move(devRes.value());

    auto cmdRes = nullDevice->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto nullCmd = std::move(cmdRes.value());

    world::World world;
    auto modeA = std::make_unique<DummyEditorMode>("ModeA");
    auto modeB = std::make_unique<DummyEditorMode>("ModeB");
    framework::Conductor conductor(world, std::move(modeA), std::move(modeB));
    TEST_CHECK(conductor.initialize(framework::ActiveSlot::SlotA).hasValue());

    // 1. Verify pure generic default identities
    TEST_CHECK(conductor.getSlotDisplayName(framework::ActiveSlot::SlotA) == "State A");
    TEST_CHECK(conductor.getSlotDisplayName(framework::ActiveSlot::SlotB) == "State B");

    EditorEngine editor;
    TEST_CHECK(editor.initialize(*nullDevice));
    editor.bindWorld(&world);
    editor.bindConductor(&conductor);

    // Frame with Slot A active
    nullCmd->begin();
    editor.beginFrame(Duration::fromMilliseconds(16), 1920, 1080);
    editor.drawPanels();
    editor.endFrame(*nullCmd);
    nullCmd->end();
    TEST_CHECK(editor.lastDrawCallCount() > 0);

    // Switch roles to Slot B active
    TEST_CHECK(conductor.requestTransition().hasValue());
    conductor.update(Duration::fromMilliseconds(16));
    TEST_CHECK(conductor.executeTransition().hasValue());
    TEST_CHECK(conductor.getActiveSlot() == framework::ActiveSlot::SlotB);

    // Frame with Slot B active
    nullCmd->begin();
    editor.beginFrame(Duration::fromMilliseconds(16), 1920, 1080);
    editor.drawPanels();
    editor.endFrame(*nullCmd);
    nullCmd->end();
    TEST_CHECK(editor.lastDrawCallCount() > 0);

    // 2. Set optional user metadata and verify rendering
    conductor.setSlotMetadata(
        framework::ActiveSlot::SlotA,
        framework::StateMetadata{.displayName = "Exploration", .description = "Free roaming"});
    conductor.setSlotMetadata(
        framework::ActiveSlot::SlotB,
        framework::StateMetadata{.displayName = "Skill", .description = "Skill casting"});

    TEST_CHECK(conductor.getSlotDisplayName(framework::ActiveSlot::SlotA) == "Exploration");
    TEST_CHECK(conductor.getSlotDisplayName(framework::ActiveSlot::SlotB) == "Skill");

    nullCmd->begin();
    editor.beginFrame(Duration::fromMilliseconds(16), 1920, 1080);
    editor.drawPanels();
    editor.endFrame(*nullCmd);
    nullCmd->end();
    TEST_CHECK(editor.lastDrawCallCount() > 0);

    editor.shutdown();
    std::cout << "[PASS] testGenericConductorStateIdentity\n";
}

static void testSelectionModelBasicOperations() {
    SelectionModel model;
    TEST_CHECK(model.empty());
    TEST_CHECK(model.count() == 0);
    TEST_CHECK(!model.getPrimarySelected().isValid());

    world::EntityHandle e1{101};
    world::EntityHandle e2{102};
    world::EntityHandle e3{103};
    world::EntityHandle e4{104};
    world::EntityHandle e5{105};

    // 1. Single selection
    model.select(e1, false);
    TEST_CHECK(!model.empty());
    TEST_CHECK(model.count() == 1);
    TEST_CHECK(model.isSelected(e1));
    TEST_CHECK(!model.isSelected(e2));
    TEST_CHECK(model.getPrimarySelected() == e1);

    // 2. Single selection replacement
    model.select(e2, false);
    TEST_CHECK(model.count() == 1);
    TEST_CHECK(!model.isSelected(e1));
    TEST_CHECK(model.isSelected(e2));
    TEST_CHECK(model.getPrimarySelected() == e2);

    // 3. Multi-selection addition (Ctrl-click / additive)
    model.select(e3, true);
    TEST_CHECK(model.count() == 2);
    TEST_CHECK(model.isSelected(e2));
    TEST_CHECK(model.isSelected(e3));
    TEST_CHECK(model.getPrimarySelected() == e3);

    // 4. Toggle selection
    model.toggle(e2); // remove e2
    TEST_CHECK(model.count() == 1);
    TEST_CHECK(!model.isSelected(e2));
    TEST_CHECK(model.isSelected(e3));
    TEST_CHECK(model.getPrimarySelected() == e3);

    model.toggle(e1); // add e1
    TEST_CHECK(model.count() == 2);
    TEST_CHECK(model.isSelected(e1));
    TEST_CHECK(model.isSelected(e3));
    TEST_CHECK(model.getPrimarySelected() == e1);

    // 5. Select range (Shift-click)
    std::vector<world::EntityHandle> ordered = {e1, e2, e3, e4, e5};
    // Primary is e1; select range to e4 -> should contain e1, e2, e3, e4
    model.selectRange(ordered, e4);
    TEST_CHECK(model.count() == 4);
    TEST_CHECK(model.isSelected(e1));
    TEST_CHECK(model.isSelected(e2));
    TEST_CHECK(model.isSelected(e3));
    TEST_CHECK(model.isSelected(e4));
    TEST_CHECK(!model.isSelected(e5));
    TEST_CHECK(model.getPrimarySelected() == e4);

    // Reverse range selection: from e4 to e2 -> e2, e3, e4
    model.selectRange(ordered, e2);
    TEST_CHECK(model.count() == 3);
    TEST_CHECK(model.isSelected(e2));
    TEST_CHECK(model.isSelected(e3));
    TEST_CHECK(model.isSelected(e4));
    TEST_CHECK(!model.isSelected(e1));
    TEST_CHECK(model.getPrimarySelected() == e2);

    // 6. Select all
    model.selectAll(ordered);
    TEST_CHECK(model.count() == 5);
    for (const auto& h : ordered) {
        TEST_CHECK(model.isSelected(h));
    }

    // 7. Deselect single
    model.deselect(e3);
    TEST_CHECK(model.count() == 4);
    TEST_CHECK(!model.isSelected(e3));

    // 8. Clear
    model.clear();
    TEST_CHECK(model.empty());
    TEST_CHECK(model.count() == 0);
    TEST_CHECK(!model.getPrimarySelected().isValid());

    std::cout << "[PASS] testSelectionModelBasicOperations\n";
}

static void testSelectionModelValidationAndStaleHandles() {
    world::World world;
    auto e1 = world.createEntity("Alpha");
    auto e2 = world.createEntity("Beta");
    auto e3 = world.createEntity("Gamma");
    auto e4 = world.createEntity("Delta");

    SelectionModel model;
    std::vector<world::EntityHandle> entities = {e1, e2, e3, e4};
    model.selectAll(entities);
    TEST_CHECK(model.count() == 4);
    TEST_CHECK(model.getPrimarySelected() == e4);

    // Destroy e2 in the World
    world.destroyEntity(e2);
    TEST_CHECK(!world.isAlive(e2));

    // Before validation, model still has cached handle
    TEST_CHECK(model.count() == 4);

    // Validate against world
    model.validate(world);
    TEST_CHECK(model.count() == 3);
    TEST_CHECK(model.isSelected(e1));
    TEST_CHECK(!model.isSelected(e2));
    TEST_CHECK(model.isSelected(e3));
    TEST_CHECK(model.isSelected(e4));
    TEST_CHECK(model.getPrimarySelected() == e4);

    // Destroy the primary selected entity (e4)
    world.destroyEntity(e4);
    TEST_CHECK(!world.isAlive(e4));

    model.validate(world);
    TEST_CHECK(model.count() == 2);
    TEST_CHECK(!model.isSelected(e4));
    // Primary must fall back to a remaining alive handle (e3)
    TEST_CHECK(model.getPrimarySelected() == e3);

    // Destroy all remaining entities
    world.destroyEntity(e1);
    world.destroyEntity(e3);
    model.validate(world);
    TEST_CHECK(model.empty());
    TEST_CHECK(model.count() == 0);
    TEST_CHECK(!model.getPrimarySelected().isValid());

    std::cout << "[PASS] testSelectionModelValidationAndStaleHandles\n";
}

static void testEditorEntityLifecycleAndConductorGating() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto nullDevice = std::move(devRes.value());

    auto cmdRes = nullDevice->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto nullCmd = std::move(cmdRes.value());

    world::World world;
    auto modeA = std::make_unique<DummyEditorMode>("StateA");
    auto modeB = std::make_unique<DummyEditorMode>("StateB");
    framework::Conductor conductor(world, std::move(modeA), std::move(modeB));
    TEST_CHECK(conductor.initialize(framework::ActiveSlot::SlotA).hasValue());

    EditorEngine editor;
    TEST_CHECK(editor.initialize(*nullDevice));
    editor.bindWorld(&world);
    editor.bindConductor(&conductor);

    // 1. Create entities in Shared World
    auto hero = world.createEntity("HeroPlayer");
    auto enemy = world.createEntity("EnemyTarget");
    TEST_CHECK(world.isAlive(hero));
    TEST_CHECK(world.isAlive(enemy));

    // 2. Select via SelectionModel
    editor.getSelectionModel().select(hero, false);
    TEST_CHECK(editor.getSelectionModel().isSelected(hero));
    TEST_CHECK(editor.getSelectionModel().getPrimarySelected() == hero);

    // 3. Render frame in State A
    nullCmd->begin();
    editor.beginFrame(Duration::fromMilliseconds(16));
    editor.drawPanels();
    editor.endFrame(*nullCmd);
    nullCmd->end();

    TEST_CHECK(editor.getSelectionModel().isSelected(hero));

    // 4. Mode transition State A -> State B
    TEST_CHECK(conductor.requestTransition().hasValue());
    conductor.update(Duration::fromMilliseconds(16));
    TEST_CHECK(conductor.executeTransition().hasValue());
    TEST_CHECK(conductor.getActiveSlot() == framework::ActiveSlot::SlotB);

    // 5. Verification: Shared World maintains entity persistence across mode transitions
    TEST_CHECK(world.isAlive(hero));
    TEST_CHECK(world.isAlive(enemy));

    // Render frame in State B
    nullCmd->begin();
    editor.beginFrame(Duration::fromMilliseconds(16));
    editor.drawPanels();
    editor.endFrame(*nullCmd);
    nullCmd->end();

    TEST_CHECK(editor.getSelectionModel().isSelected(hero));

    // 6. Multi-select in State B
    editor.getSelectionModel().select(enemy, true);
    TEST_CHECK(editor.getSelectionModel().count() == 2);
    TEST_CHECK(editor.getSelectionModel().isSelected(hero));
    TEST_CHECK(editor.getSelectionModel().isSelected(enemy));

    // 7. Destroy entity during simulation
    world.destroyEntity(hero);
    TEST_CHECK(!world.isAlive(hero));

    // On next frame, beginFrame() must automatically purge stale entity
    nullCmd->begin();
    editor.beginFrame(Duration::fromMilliseconds(16));
    editor.drawPanels();
    editor.endFrame(*nullCmd);
    nullCmd->end();

    TEST_CHECK(editor.getSelectionModel().count() == 1);
    TEST_CHECK(!editor.getSelectionModel().isSelected(hero));
    TEST_CHECK(editor.getSelectionModel().isSelected(enemy));
    TEST_CHECK(editor.getSelectionModel().getPrimarySelected() == enemy);

    editor.shutdown();
    conductor.shutdown();
    std::cout << "[PASS] testEditorEntityLifecycleAndConductorGating\n";
}

static void testInvalidEntityLookupSafety() {
    world::World world;
    flecs::world& ecs = world.getRawWorld();

    // 1. Looking up a non-existent entity name returns a flecs::entity with id_ == 0
    auto missing = ecs.lookup("NonExistent_PhysicsCube_Probe");
    TEST_CHECK(missing.id() == 0);

    // 2. Calling missing.is_valid() returns false safely without asserting in ecs_is_alive
    TEST_CHECK(!missing.is_valid());

    // 3. EntityHandle with id 0 is invalid and world.isAlive() returns false safely
    world::EntityHandle zeroHandle(0);
    TEST_CHECK(!zeroHandle.isValid());
    TEST_CHECK(!world.isAlive(zeroHandle));

    // 4. Once an entity is created, lookup finds it and is_valid() is true
    auto created = world.createEntity("ExistingEntity");
    TEST_CHECK(created.isValid());
    TEST_CHECK(world.isAlive(created));

    auto found = ecs.lookup("ExistingEntity");
    TEST_CHECK(found.id() == created.id);
    TEST_CHECK(found.is_valid());
    TEST_CHECK(found.is_alive());

    std::cout << "[PASS] testInvalidEntityLookupSafety\n";
}

static void testPhysicsObjectCreationAndUniqueNamingRegression() {
    world::World world;
    flecs::world& ecs = world.getRawWorld();

    // 1. Calling generateUniqueEntityName for a non-existent name returns the exact name without
    // crashing
    std::string name1 = HierarchyPanel::generateUniqueEntityName(ecs, "PhysicsCube");
    TEST_CHECK(name1 == "PhysicsCube");

    // 2. Create the first Physics Object with transform and rigid body components
    auto cube1 = world.createEntity(name1);
    TEST_CHECK(cube1.isValid());
    world.setComponent(cube1,
                       physics::PhysicsTransformComponent{.transform = Transform{Vec3{0, 2, 0}}});
    world.setComponent(cube1, physics::RigidBodyComponent{
                                  .bodyHandle = {},
                                  .motionType = physics::MotionType::Dynamic,
                                  .collisionLayer = physics::CollisionLayer::kMoving,
                              });
    TEST_CHECK(world.isAlive(cube1));

    // 3. Second call generates collision-free suffix "_1"
    std::string name2 = HierarchyPanel::generateUniqueEntityName(ecs, "PhysicsCube");
    TEST_CHECK(name2 == "PhysicsCube_1");

    auto cube2 = world.createEntity(name2);
    TEST_CHECK(cube2.isValid());
    world.setComponent(cube2,
                       physics::PhysicsTransformComponent{.transform = Transform{Vec3{1, 3, 0}}});
    world.setComponent(cube2, physics::RigidBodyComponent{
                                  .bodyHandle = {},
                                  .motionType = physics::MotionType::Dynamic,
                                  .collisionLayer = physics::CollisionLayer::kMoving,
                              });

    // 4. Third call generates suffix "_2"
    std::string name3 = HierarchyPanel::generateUniqueEntityName(ecs, "PhysicsCube");
    TEST_CHECK(name3 == "PhysicsCube_2");

    // 5. If cube2 is destroyed, recycled name "PhysicsCube_1" is cleanly available
    world.destroyEntity(cube2);
    TEST_CHECK(!world.isAlive(cube2));

    std::string recycled = HierarchyPanel::generateUniqueEntityName(ecs, "PhysicsCube");
    TEST_CHECK(recycled == "PhysicsCube_1");

    // 6. Null and empty base names fall back safely to "Entity"
    std::string nullFallback = HierarchyPanel::generateUniqueEntityName(ecs, nullptr);
    TEST_CHECK(nullFallback == "Entity");

    std::string emptyFallback = HierarchyPanel::generateUniqueEntityName(ecs, "");
    TEST_CHECK(emptyFallback == "Entity");

    // 7. World-based overload works identically
    std::string sphereName = HierarchyPanel::generateUniqueEntityName(world, "PhysicsSphere");
    TEST_CHECK(sphereName == "PhysicsSphere");

    std::cout << "[PASS] testPhysicsObjectCreationAndUniqueNamingRegression\n";
}

static void testRepeatedPhysicsObjectCreationAndHierarchyRendering() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto nullDevice = std::move(devRes.value());

    auto cmdRes = nullDevice->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto nullCmd = std::move(cmdRes.value());

    world::World world;
    auto modeA = std::make_unique<DummyEditorMode>("StateA");
    auto modeB = std::make_unique<DummyEditorMode>("StateB");
    framework::Conductor conductor(world, std::move(modeA), std::move(modeB));
    TEST_CHECK(conductor.initialize(framework::ActiveSlot::SlotA).hasValue());

    EditorEngine editor;
    TEST_CHECK(editor.initialize(*nullDevice));
    editor.bindWorld(&world);
    editor.bindConductor(&conductor);

    // 1. Repeatedly create 10 Physics Objects (alternating cubes and spheres)
    std::vector<world::EntityHandle> createdPhysicsObjects;
    createdPhysicsObjects.reserve(10);

    for (int i = 0; i < 10; ++i) {
        const char* baseName = (i % 2 == 0) ? "PhysicsCube" : "PhysicsSphere";
        std::string uniqueName =
            HierarchyPanel::generateUniqueEntityName(world.getRawWorld(), baseName);
        auto h = world.createEntity(uniqueName);
        TEST_CHECK(h.isValid());
        TEST_CHECK(world.isAlive(h));

        world.setComponent(h, physics::PhysicsTransformComponent{
                                  .transform = Transform{Vec3{static_cast<float>(i), 5.0F, 0.0F}},
                              });
        world.setComponent(h, physics::RigidBodyComponent{
                                  .bodyHandle = {},
                                  .motionType = physics::MotionType::Dynamic,
                                  .collisionLayer = physics::CollisionLayer::kMoving,
                              });

        createdPhysicsObjects.push_back(h);
    }

    // 2. Select all physics objects
    editor.getSelectionModel().selectAll(createdPhysicsObjects);
    TEST_CHECK(editor.getSelectionModel().count() == 10);

    // 3. Render 3 full editor frames with all 10 physics entities active
    for (int f = 0; f < 3; ++f) {
        nullCmd->begin();
        editor.beginFrame(Duration::fromMilliseconds(16));
        editor.drawPanels();
        editor.endFrame(*nullCmd);
        nullCmd->end();

        TEST_CHECK(editor.lastDrawCallCount() > 0);
        TEST_CHECK(editor.lastVertexCount() > 0);
    }

    // 4. Destroy half of the physics entities mid-simulation
    for (size_t i = 0; i < 5; ++i) {
        world.destroyEntity(createdPhysicsObjects[i]);
        TEST_CHECK(!world.isAlive(createdPhysicsObjects[i]));
    }

    // 5. Subsequent frames must immediately purge destroyed entities from selection and render
    // remaining 5 cleanly
    nullCmd->begin();
    editor.beginFrame(Duration::fromMilliseconds(16));
    editor.drawPanels();
    editor.endFrame(*nullCmd);
    nullCmd->end();

    TEST_CHECK(editor.getSelectionModel().count() == 5);
    TEST_CHECK(editor.lastDrawCallCount() > 0);

    editor.shutdown();
    conductor.shutdown();
    std::cout << "[PASS] testRepeatedPhysicsObjectCreationAndHierarchyRendering\n";
}

static void testScriptComponentEnableToggleBehavior() {
    world::World world;
    script::ScriptEngine scriptEngine;
    auto initRes = scriptEngine.initialize();
    TEST_CHECK(initRes.hasValue());
    scriptEngine.bindWorld(&world);

    script::registerScriptComponents(world);
    script::registerScriptSystems(world, scriptEngine);

    // 1. Create an entity with a non-existent script function name (the bug state)
    auto entityBug = world.createEntity("BuggyScriptEntity");
    auto& ecs = world.getRawWorld();
    auto eViewBug = ecs.entity(entityBug.id);
    eViewBug.set<script::ScriptComponent>({
        .scriptName = "scripts/main.lua",
        .enabled = true,
    });

    TEST_CHECK(eViewBug.get<script::ScriptComponent>()->enabled == true);

    // Simulation frame runs: ScriptUpdateSystem fails to find 'scripts/main.lua'
    world.progress(0.016f);

    // Component gets automatically disabled due to failure isolation
    TEST_CHECK(eViewBug.get<script::ScriptComponent>()->enabled == false);

    // If the user tries to toggle the checkbox back to enabled in the Inspector
    auto* scMut = eViewBug.get_mut<script::ScriptComponent>();
    scMut->enabled = true;
    TEST_CHECK(eViewBug.get<script::ScriptComponent>()->enabled == true);

    // On the next frame, because scriptName is still invalid, it gets disabled again!
    world.progress(0.016f);
    TEST_CHECK(eViewBug.get<script::ScriptComponent>()->enabled == false);

    // 2. Unassigned script component behavior (scriptName is empty "")
    auto entityUnassigned = world.createEntity("UnassignedScriptEntity");
    auto eViewUnassigned = ecs.entity(entityUnassigned.id);
    eViewUnassigned.set<script::ScriptComponent>({
        .scriptName = "",
        .enabled = true,
    });

    // An unassigned script component must NOT fail and must remain enabled across frames
    for (int i = 0; i < 5; ++i) {
        world.progress(0.016f);
        TEST_CHECK(eViewUnassigned.get<script::ScriptComponent>()->enabled == true);
    }

    // 3. Valid registered routine behavior
    auto execRes = scriptEngine.executeString("test_tick_count = 0\nfunction on_test_tick(e, dt)\n "
                                              " test_tick_count = test_tick_count + 1\nend\n");
    TEST_CHECK(execRes.hasValue());
    auto entityValid = world.createEntity("ValidScriptEntity");
    auto eViewValid = ecs.entity(entityValid.id);
    eViewValid.set<script::ScriptComponent>({
        .scriptName = "on_test_tick",
        .enabled = true,
    });

    world.progress(0.016f);
    TEST_CHECK(eViewValid.get<script::ScriptComponent>()->enabled == true);
    auto tickVal = scriptEngine.getGlobal("test_tick_count");
    TEST_CHECK(tickVal.hasValue() && std::holds_alternative<double>(tickVal.value()));
    TEST_CHECK(std::get<double>(tickVal.value()) == 1.0);

    // When disabled explicitly, on_test_tick must not be invoked
    eViewValid.get_mut<script::ScriptComponent>()->enabled = false;
    world.progress(0.016f);
    TEST_CHECK(eViewValid.get<script::ScriptComponent>()->enabled == false);
    tickVal = scriptEngine.getGlobal("test_tick_count");
    TEST_CHECK(std::get<double>(tickVal.value()) == 1.0); // unchanged

    // Re-enabling explicitly resumes execution
    eViewValid.get_mut<script::ScriptComponent>()->enabled = true;
    world.progress(0.016f);
    TEST_CHECK(eViewValid.get<script::ScriptComponent>()->enabled == true);
    tickVal = scriptEngine.getGlobal("test_tick_count");
    TEST_CHECK(std::get<double>(tickVal.value()) == 2.0); // incremented

    scriptEngine.shutdown();
    std::cout << "[PASS] testScriptComponentEnableToggleBehavior\n";
}

int main() {
    std::cout << "Running Editor subsystem unit tests...\n";
    testThemeTokens();
    testEditorLifecycle();
    testPanelManagerAndRegistry();
    testWorkspacePersistenceAndRecovery();
    testEditorFrameAndPanels();
    testEditorInputProcessing();
    testResponsiveViewports();
    testGenericConductorStateIdentity();
    testSelectionModelBasicOperations();
    testSelectionModelValidationAndStaleHandles();
    testEditorEntityLifecycleAndConductorGating();
    testInvalidEntityLookupSafety();
    testPhysicsObjectCreationAndUniqueNamingRegression();
    testRepeatedPhysicsObjectCreationAndHierarchyRendering();
    testScriptComponentEnableToggleBehavior();
    std::cout << "All Editor subsystem tests PASSED!\n";
    return 0;
}
