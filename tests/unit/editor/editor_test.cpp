/// @file editor_test.cpp
/// @brief Comprehensive test suite for the Dear ImGui editor tooling subsystem.

#include "editor/content_browser_controller.h"
#include "editor/editor_camera.h"
#include "editor/editor_engine.h"
#include "editor/editor_theme.h"
#include "editor/hierarchy_panel.h"
#include "editor/selection_model.h"
#include "editor/ui_helpers.h"
#include "editor/workspace_manager.h"
#include "framework/conductor.h"
#include "framework/mode.h"
#include "framework/runtime_session.h"
#include "panels/content_browser_panel.h"
#include "physics/physics_components.h"
#include "platform/input_snapshot.h"
#include "render/asset_manager.h"
#include "render/mesh_component.h"
#include "render/scene_renderer.h"
#include "rhi/command_list.h"
#include "rhi/rhi_device.h"
#include "script/script_components.h"
#include "script/script_engine.h"
#include "world/world.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <imgui.h>
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
    // 1. Test Dark Theme
    ThemeColors dark = ThemeColors::hikariDark();
    TEST_CHECK(dark.backgroundApp.a == 1.0F);
    TEST_CHECK(dark.accentPrimary.r > 0.0F);
    TEST_CHECK(dark.colorAxisX.r > 0.9F);
    TEST_CHECK(dark.colorAxisY.g > 0.7F);
    TEST_CHECK(dark.colorAxisZ.b > 0.9F);
    TEST_CHECK(!dark.backgroundPanel.isLight());
    TEST_CHECK(dark.textPrimary.isLight());
    TEST_CHECK(dark.textPrimary.luminance() > 0.8F);

    // 2. Test Midnight Theme
    ThemeColors midnight = ThemeColors::hikariMidnight();
    TEST_CHECK(midnight.backgroundDarkest.r < dark.backgroundDarkest.r);
    TEST_CHECK(midnight.accentPrimary.b > 0.9F);
    TEST_CHECK(!midnight.backgroundPanel.isLight());
    TEST_CHECK(midnight.textPrimary.isLight());

    // 3. Test Light Theme
    ThemeColors light = ThemeColors::hikariLight();
    TEST_CHECK(light.backgroundPanel.isLight());
    TEST_CHECK(!light.textPrimary.isLight());
    TEST_CHECK(light.textPrimary.luminance() < 0.2F);
    TEST_CHECK(light.accentPrimary.b > 0.8F);

    // 4. Test Color4f utilities
    Color4f testColor(1.0F, 0.0F, 0.0F, 1.0F);
    uint32_t packed = testColor.toRgba32();
    TEST_CHECK((packed & 0xFFU) == 255U); // Red component

    Color4f alphaMod = testColor.withAlpha(0.5F);
    TEST_CHECK(alphaMod.a == 0.5F);
    TEST_CHECK(alphaMod.r == 1.0F);

    // 5. Test Theme Switching API
    applyEditorTheme(ThemeType::Midnight);
    TEST_CHECK(getActiveThemeType() == ThemeType::Midnight);
    TEST_CHECK(std::strcmp(getThemeName(getActiveThemeType()), "Hikari Midnight") == 0);

    applyEditorTheme(ThemeType::Light);
    TEST_CHECK(getActiveThemeType() == ThemeType::Light);
    TEST_CHECK(std::strcmp(getThemeName(getActiveThemeType()), "Hikari Light") == 0);

    applyEditorTheme(ThemeType::Dark);
    TEST_CHECK(getActiveThemeType() == ThemeType::Dark);
    TEST_CHECK(std::strcmp(getThemeName(getActiveThemeType()), "Hikari Dark") == 0);

    // 6. Test Editor Icons
    TEST_CHECK(EditorIcons::kPlay != nullptr && EditorIcons::kPlay[0] != '\0');
    TEST_CHECK(EditorIcons::kPause != nullptr && EditorIcons::kPause[0] != '\0');
    TEST_CHECK(EditorIcons::kStop != nullptr && EditorIcons::kStop[0] != '\0');
    TEST_CHECK(EditorIcons::kCamera != nullptr && EditorIcons::kCamera[0] != '\0');
    TEST_CHECK(EditorIcons::kMesh != nullptr && EditorIcons::kMesh[0] != '\0');

    // 7. Test Theme Metrics
    TEST_CHECK(ThemeMetrics::kToolbarHeight >= 40.0F);
    TEST_CHECK(ThemeMetrics::kStatusBarHeight >= 24.0F);
    TEST_CHECK(ThemeMetrics::kSpacingM == 8.0F);

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

static void testInspectorComponentManagementLifecycle() {
    world::World world;
    physics::registerPhysicsComponents(world);
    script::registerScriptComponents(world);

    auto& ecs = world.getRawWorld();
    auto handle = world.createEntity("TestTarget");
    auto e = ecs.entity(handle.id);

    // 1. Initially entity has no custom components
    TEST_CHECK(!e.has<physics::PhysicsTransformComponent>());
    TEST_CHECK(!e.has<physics::RigidBodyComponent>());
    TEST_CHECK(!e.has<physics::ColliderComponent>());
    TEST_CHECK(!e.has<script::ScriptComponent>());

    // 2. Add PhysicsTransformComponent
    e.set<physics::PhysicsTransformComponent>({
        .transform = Transform(Vec3(1.0F, 2.0F, 3.0F)),
    });
    TEST_CHECK(e.has<physics::PhysicsTransformComponent>());
    TEST_CHECK(e.get<physics::PhysicsTransformComponent>()->transform.position.y == 2.0F);

    // 3. Add RigidBodyComponent
    e.set<physics::RigidBodyComponent>({
        .motionType = physics::MotionType::Kinematic,
        .collisionLayer = physics::CollisionLayer::kMoving,
    });
    TEST_CHECK(e.has<physics::RigidBodyComponent>());
    TEST_CHECK(e.get<physics::RigidBodyComponent>()->motionType == physics::MotionType::Kinematic);

    // 4. Add ColliderComponent
    e.set<physics::ColliderComponent>({
        .desc = physics::ColliderDesc::makeBox(Vec3(0.5F, 1.0F, 1.5F)),
    });
    TEST_CHECK(e.has<physics::ColliderComponent>());
    TEST_CHECK(e.get<physics::ColliderComponent>()->desc.type == physics::ColliderType::Box);
    TEST_CHECK(e.get<physics::ColliderComponent>()->desc.halfExtents.z == 1.5F);

    // 5. Add ScriptComponent
    e.set<script::ScriptComponent>({
        .scriptName = "on_update",
        .enabled = true,
        .updateInterval = 0.05F,
        .timeSinceLastUpdate = 0.0F,
    });
    TEST_CHECK(e.has<script::ScriptComponent>());
    TEST_CHECK(e.get<script::ScriptComponent>()->enabled == true);
    TEST_CHECK(std::strcmp(e.get<script::ScriptComponent>()->scriptName, "on_update") == 0);

    // All 4 components exist simultaneously
    TEST_CHECK(e.has<physics::PhysicsTransformComponent>());
    TEST_CHECK(e.has<physics::RigidBodyComponent>());
    TEST_CHECK(e.has<physics::ColliderComponent>());
    TEST_CHECK(e.has<script::ScriptComponent>());

    // 6. Removing ColliderComponent leaves other 3 intact
    e.remove<physics::ColliderComponent>();
    TEST_CHECK(!e.has<physics::ColliderComponent>());
    TEST_CHECK(e.has<physics::PhysicsTransformComponent>());
    TEST_CHECK(e.has<physics::RigidBodyComponent>());
    TEST_CHECK(e.has<script::ScriptComponent>());

    // 7. Removing RigidBodyComponent leaves Transform and Script intact
    e.remove<physics::RigidBodyComponent>();
    TEST_CHECK(!e.has<physics::RigidBodyComponent>());
    TEST_CHECK(e.has<physics::PhysicsTransformComponent>());
    TEST_CHECK(e.has<script::ScriptComponent>());

    // 8. Removing ScriptComponent leaves Transform intact
    e.remove<script::ScriptComponent>();
    TEST_CHECK(!e.has<script::ScriptComponent>());
    TEST_CHECK(e.has<physics::PhysicsTransformComponent>());

    // 9. Removing Transform leaves empty entity
    e.remove<physics::PhysicsTransformComponent>();
    TEST_CHECK(!e.has<physics::PhysicsTransformComponent>());

    // 10. Re-adding components restores them cleanly
    e.set<physics::ColliderComponent>({
        .desc = physics::ColliderDesc::makeSphere(2.5F),
    });
    TEST_CHECK(e.has<physics::ColliderComponent>());
    TEST_CHECK(e.get<physics::ColliderComponent>()->desc.type == physics::ColliderType::Sphere);
    TEST_CHECK(e.get<physics::ColliderComponent>()->desc.radius == 2.5F);

    // 11. Stale / destroyed entity check
    world.destroyEntity(handle);
    TEST_CHECK(!world.isAlive(handle));

    std::cout << "[PASS] testInspectorComponentManagementLifecycle\n";
}

static void testTransformAndPhysicsPropertyEditing() {
    world::World world;
    physics::registerPhysicsComponents(world);

    auto handle = world.createEntity("PropEntity");
    auto& ecs = world.getRawWorld();
    auto e = ecs.entity(handle.id);

    // 1. Transform property modification
    e.set<physics::PhysicsTransformComponent>({
        .transform = Transform(Vec3(0.0F, 0.0F, 0.0F)),
    });
    auto* tc = e.get_mut<physics::PhysicsTransformComponent>();
    tc->transform.position = Vec3(10.5F, -3.2F, 4.0F);
    tc->transform.scale = Vec3(2.0F, 2.0F, 2.0F);
    // 90 deg Yaw rotation (Y-axis)
    constexpr float kDegToRad = 0.0174532925F;
    tc->transform.rotation = Quat::fromAxisAngle(Vec3(0.0F, 1.0F, 0.0F), 90.0F * kDegToRad);
    e.modified<physics::PhysicsTransformComponent>();

    const auto* tcRead = e.get<physics::PhysicsTransformComponent>();
    TEST_CHECK(tcRead->transform.position.x == 10.5F);
    TEST_CHECK(tcRead->transform.position.y == -3.2F);
    TEST_CHECK(tcRead->transform.scale.x == 2.0F);
    // Quat of 90 deg around Y: (0, sin(45), 0, cos(45))
    TEST_CHECK(std::abs(tcRead->transform.rotation.y - 0.7071067F) < 1e-4F);
    TEST_CHECK(std::abs(tcRead->transform.rotation.w - 0.7071067F) < 1e-4F);

    // 2. RigidBody motion type & layer modification
    e.set<physics::RigidBodyComponent>({
        .motionType = physics::MotionType::Static,
        .collisionLayer = physics::CollisionLayer::kNonMoving,
    });
    auto* rb = e.get_mut<physics::RigidBodyComponent>();
    rb->motionType = physics::MotionType::Dynamic;
    rb->collisionLayer = physics::CollisionLayer::kPlayer;
    e.modified<physics::RigidBodyComponent>();

    const auto* rbRead = e.get<physics::RigidBodyComponent>();
    TEST_CHECK(rbRead->motionType == physics::MotionType::Dynamic);
    TEST_CHECK(rbRead->collisionLayer == physics::CollisionLayer::kPlayer);

    // 3. Collider shape & dimension modification
    e.set<physics::ColliderComponent>({
        .desc = physics::ColliderDesc::makeBox(Vec3(1.0F, 2.0F, 3.0F)),
    });
    auto* cc = e.get_mut<physics::ColliderComponent>();
    TEST_CHECK(cc->desc.type == physics::ColliderType::Box);
    TEST_CHECK(cc->desc.halfExtents.y == 2.0F);

    // Switch to Sphere
    cc->desc = physics::ColliderDesc::makeSphere(3.5F, Vec3(0.0F, 1.0F, 0.0F));
    e.modified<physics::ColliderComponent>();
    const auto* ccRead = e.get<physics::ColliderComponent>();
    TEST_CHECK(ccRead->desc.type == physics::ColliderType::Sphere);
    TEST_CHECK(ccRead->desc.radius == 3.5F);
    TEST_CHECK(ccRead->desc.offset.y == 1.0F);

    // Switch to Capsule
    cc = e.get_mut<physics::ColliderComponent>();
    cc->desc = physics::ColliderDesc::makeCapsule(0.8F, 1.8F);
    e.modified<physics::ColliderComponent>();
    ccRead = e.get<physics::ColliderComponent>();
    TEST_CHECK(ccRead->desc.type == physics::ColliderType::Capsule);
    TEST_CHECK(ccRead->desc.radius == 0.8F);
    TEST_CHECK(ccRead->desc.halfHeight == 1.8F);

    std::cout << "[PASS] testTransformAndPhysicsPropertyEditing\n";
}

static void testMultiSelectionPropertyEditingAndBatchOperations() {
    world::World world;
    physics::registerPhysicsComponents(world);
    script::registerScriptComponents(world);

    auto h1 = world.createEntity("Entity_1");
    auto h2 = world.createEntity("Entity_2");
    auto hUnselected = world.createEntity("Unselected");

    auto& ecs = world.getRawWorld();
    auto e1 = ecs.entity(h1.id);
    auto e2 = ecs.entity(h2.id);
    auto eUnsel = ecs.entity(hUnselected.id);

    // 1. Initial setups: e1 and e2 have transforms with different X, same Y and Z
    e1.set<physics::PhysicsTransformComponent>({
        .transform = Transform(Vec3(1.0F, 5.0F, 0.0F)),
    });
    e2.set<physics::PhysicsTransformComponent>({
        .transform = Transform(Vec3(2.0F, 5.0F, 0.0F)),
    });
    eUnsel.set<physics::PhysicsTransformComponent>({
        .transform = Transform(Vec3(0.0F, 0.0F, 0.0F)),
    });

    SelectionModel selection;
    selection.select(h1, false);
    selection.select(h2, true); // multi-select h1 and h2
    TEST_CHECK(selection.count() == 2);
    TEST_CHECK(selection.isSelected(h1));
    TEST_CHECK(selection.isSelected(h2));
    TEST_CHECK(!selection.isSelected(hUnselected));

    // Verify common component presence
    TEST_CHECK(e1.has<physics::PhysicsTransformComponent>());
    TEST_CHECK(e2.has<physics::PhysicsTransformComponent>());

    // 2. Multi-selection batch property edit: modify Y on all selected entities
    for (auto h : selection.getSelectedEntities()) {
        auto* tc = ecs.entity(h.id).get_mut<physics::PhysicsTransformComponent>();
        tc->transform.position.y = 12.0F; // apply unified Y change
        ecs.entity(h.id).modified<physics::PhysicsTransformComponent>();
    }

    // Verify individual X values preserved, Y updated on both, unselected untouched
    TEST_CHECK(e1.get<physics::PhysicsTransformComponent>()->transform.position.x == 1.0F);
    TEST_CHECK(e1.get<physics::PhysicsTransformComponent>()->transform.position.y == 12.0F);

    TEST_CHECK(e2.get<physics::PhysicsTransformComponent>()->transform.position.x == 2.0F);
    TEST_CHECK(e2.get<physics::PhysicsTransformComponent>()->transform.position.y == 12.0F);

    TEST_CHECK(eUnsel.get<physics::PhysicsTransformComponent>()->transform.position.y == 0.0F);

    // 3. Batch Add Component: add ScriptComponent to all selected entities
    TEST_CHECK(!e1.has<script::ScriptComponent>());
    TEST_CHECK(!e2.has<script::ScriptComponent>());
    for (auto h : selection.getSelectedEntities()) {
        auto ent = ecs.entity(h.id);
        if (!ent.has<script::ScriptComponent>()) {
            ent.set<script::ScriptComponent>({
                .scriptName = "on_group_tick",
                .enabled = true,
                .updateInterval = 0.016F,
                .timeSinceLastUpdate = 0.0F,
            });
        }
    }

    TEST_CHECK(e1.has<script::ScriptComponent>());
    TEST_CHECK(e2.has<script::ScriptComponent>());
    TEST_CHECK(!eUnsel.has<script::ScriptComponent>());
    TEST_CHECK(std::strcmp(e1.get<script::ScriptComponent>()->scriptName, "on_group_tick") == 0);
    TEST_CHECK(std::strcmp(e2.get<script::ScriptComponent>()->scriptName, "on_group_tick") == 0);

    // 4. Batch Remove Component: remove ScriptComponent from all selected entities
    for (auto h : selection.getSelectedEntities()) {
        ecs.entity(h.id).remove<script::ScriptComponent>();
    }

    TEST_CHECK(!e1.has<script::ScriptComponent>());
    TEST_CHECK(!e2.has<script::ScriptComponent>());
    TEST_CHECK(e1.has<physics::PhysicsTransformComponent>());
    TEST_CHECK(e2.has<physics::PhysicsTransformComponent>());

    // 5. Batch Entity Deletion: delete all selected entities
    auto toDelete = selection.getSelectedEntities();
    for (auto h : toDelete) {
        world.destroyEntity(h);
    }
    selection.clear();

    TEST_CHECK(!world.isAlive(h1));
    TEST_CHECK(!world.isAlive(h2));
    TEST_CHECK(world.isAlive(hUnselected));
    TEST_CHECK(selection.empty());

    std::cout << "[PASS] testMultiSelectionPropertyEditingAndBatchOperations\n";
}

static void testWorkspaceSceneLifecycleAndFileOperations() {
    world::World world;
    SelectionModel selection;
    WorkspaceManager wm;

    TEST_CHECK(!wm.isSceneDirty());
    TEST_CHECK(wm.getCurrentScenePath().empty());

    // 1. Mark dirty and clear dirty
    wm.markSceneDirty();
    TEST_CHECK(wm.isSceneDirty());
    wm.clearSceneDirty();
    TEST_CHECK(!wm.isSceneDirty());

    // 2. Create entities and select one
    auto e1 = world.createEntity("SceneEntity1");
    auto e2 = world.createEntity("SceneEntity2");
    selection.select(e1);
    TEST_CHECK(selection.count() == 1);
    TEST_CHECK(selection.isSelected(e1));

    // 3. Save Scene to file
    const std::string testScenePath = "/tmp/hikari_editor_test_scene.hikari";
    std::filesystem::remove(testScenePath);

    wm.markSceneDirty();
    bool saveOk = wm.saveScene(world, testScenePath.c_str());
    TEST_CHECK(saveOk);
    TEST_CHECK(!wm.isSceneDirty());
    TEST_CHECK(wm.getCurrentScenePath() == testScenePath);
    TEST_CHECK(std::filesystem::exists(testScenePath));

    // 4. New Scene clears entities, selection, and scene path
    bool newOk = wm.newScene(world, &selection);
    TEST_CHECK(newOk);
    TEST_CHECK(selection.empty());
    TEST_CHECK(!world.isAlive(e1));
    TEST_CHECK(!world.isAlive(e2));
    TEST_CHECK(world.getAllEntities().empty());
    TEST_CHECK(wm.getCurrentScenePath().empty());
    TEST_CHECK(!wm.isSceneDirty());

    // 5. Open Scene restores entities from saved file
    bool openOk = wm.openScene(world, testScenePath.c_str(), &selection);
    TEST_CHECK(openOk);
    TEST_CHECK(wm.getCurrentScenePath() == testScenePath);
    TEST_CHECK(!wm.isSceneDirty());
    auto restoredEntities = world.getAllEntities();
    TEST_CHECK(restoredEntities.size() == 2);

    // 6. Failed Open Scene (non-existent file) does not corrupt state
    bool badOpenOk = wm.openScene(world, "/tmp/non_existent_scene_file.hikari", &selection);
    TEST_CHECK(!badOpenOk);
    // World still has its 2 entities
    TEST_CHECK(world.getAllEntities().size() == 2);
    // Path remains what it was before failure
    TEST_CHECK(wm.getCurrentScenePath() == testScenePath);

    // Clean up
    std::filesystem::remove(testScenePath);
    std::cout << "[PASS] testWorkspaceSceneLifecycleAndFileOperations\n";
}

static void testEditorRuntimeSessionIntegrationAndIsolation() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto nullDevice = std::move(devRes.value());

    auto cmdRes = nullDevice->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto nullCmd = std::move(cmdRes.value());

    world::World editWorld;
    physics::registerPhysicsComponents(editWorld);

    auto e1 = editWorld.createEntity("EditableBox");
    editWorld.setComponent(e1, physics::PhysicsTransformComponent{
                                   .transform = Transform(Vec3(0.0F, 50.0F, 0.0F), Quat::identity(),
                                                          Vec3(1.0F, 1.0F, 1.0F)),
                               });
    editWorld.setComponent(e1, physics::RigidBodyComponent{
                                   .bodyHandle = {},
                                   .motionType = physics::MotionType::Dynamic,
                                   .collisionLayer = physics::CollisionLayer::kMoving,
                               });

    EditorEngine editor;
    TEST_CHECK(editor.initialize(*nullDevice));
    editor.bindWorld(&editWorld);

    // Initial state: Edit Mode
    auto& session = editor.getRuntimeSession();
    TEST_CHECK(session.getSimulationState() == framework::SimulationState::Edit);
    TEST_CHECK(!session.isSimulating());

    // Select e1 in Edit Mode
    editor.getSelectionModel().select(e1);
    TEST_CHECK(editor.getSelectionModel().count() == 1);

    // Run frame in Edit Mode
    nullCmd->begin();
    editor.beginFrame(Duration::fromMilliseconds(16));
    editor.drawPanels();
    editor.endFrame(*nullCmd);
    nullCmd->end();

    // 1. Play simulation
    TEST_CHECK(session.play().hasValue());
    TEST_CHECK(session.getSimulationState() == framework::SimulationState::Playing);
    TEST_CHECK(session.isSimulating());

    // Run 5 frames in Playing Mode
    for (int i = 0; i < 5; ++i) {
        nullCmd->begin();
        editor.beginFrame(Duration::fromMilliseconds(16));
        editor.drawPanels();
        editor.endFrame(*nullCmd);
        nullCmd->end();
        session.update(Duration::fromMilliseconds(16));
    }

    // Active world during Playing is the Runtime World
    auto* rtWorld = session.getRuntimeWorld();
    TEST_CHECK(rtWorld != nullptr);
    TEST_CHECK(rtWorld != &editWorld);

    // Dynamic body in Runtime World has moved under gravity
    auto rtEntities = rtWorld->getAllEntities();
    TEST_CHECK(rtEntities.size() == 1);
    auto* rtTr = rtWorld->getComponent<physics::PhysicsTransformComponent>(rtEntities[0]);
    TEST_CHECK(rtTr != nullptr);
    TEST_CHECK(rtTr->transform.position.y < 50.0F);

    // Edit World remains completely unaffected at y = 50.0F
    auto* editTr = editWorld.getComponent<physics::PhysicsTransformComponent>(e1);
    TEST_CHECK(editTr != nullptr);
    TEST_CHECK(editTr->transform.position.y == 50.0F);

    // 2. Pause simulation
    TEST_CHECK(session.pause().hasValue());
    TEST_CHECK(session.getSimulationState() == framework::SimulationState::Paused);
    float yAtPause = rtTr->transform.position.y;

    // Run frame in Paused Mode
    nullCmd->begin();
    editor.beginFrame(Duration::fromMilliseconds(16));
    editor.drawPanels();
    editor.endFrame(*nullCmd);
    nullCmd->end();
    session.update(Duration::fromMilliseconds(16));

    // While paused, position does not advance
    TEST_CHECK(rtTr->transform.position.y == yAtPause);

    // 3. Resume simulation
    TEST_CHECK(session.resume().hasValue());
    TEST_CHECK(session.getSimulationState() == framework::SimulationState::Playing);

    // 4. Stop simulation
    TEST_CHECK(session.stop().hasValue());
    TEST_CHECK(session.getSimulationState() == framework::SimulationState::Edit);
    TEST_CHECK(!session.isSimulating());
    TEST_CHECK(session.getRuntimeWorld() == nullptr);

    // Run frame in Edit Mode post-Stop
    nullCmd->begin();
    editor.beginFrame(Duration::fromMilliseconds(16));
    editor.drawPanels();
    editor.endFrame(*nullCmd);
    nullCmd->end();

    // Verify Edit World is intact and identical to before Play
    TEST_CHECK(editWorld.getAllEntities().size() == 1);
    TEST_CHECK(editWorld.isAlive(e1));
    auto* finalEditTr = editWorld.getComponent<physics::PhysicsTransformComponent>(e1);
    TEST_CHECK(finalEditTr != nullptr);
    TEST_CHECK(finalEditTr->transform.position.y == 50.0F);

    editor.shutdown();
    std::cout << "[PASS] testEditorRuntimeSessionIntegrationAndIsolation\n";
}

static void testEditorCameraLifecycleAndNavigation() {
    EditorCamera camera;
    TEST_CHECK(camera.getDistance() > 0.0F);
    TEST_CHECK(camera.getNearPlane() == 0.1F);
    TEST_CHECK(camera.getFarPlane() == 1000.0F);

    // Initial framing
    camera.frame(Vec3{0.0F, 0.0F, 0.0F}, 2.0F);
    TEST_CHECK(camera.getPivot().isNearlyEqual(Vec3::zero()));
    TEST_CHECK(camera.getDistance() >= 5.0F);

    // Update matrices
    camera.updateMatrices(1920.0F, 1080.0F);
    const auto& view = camera.getViewMatrix();
    const auto& proj = camera.getProjectionMatrix();
    const auto& vp = camera.getViewProjectionMatrix();
    TEST_CHECK(view.at(3, 3) == 1.0F);
    TEST_CHECK(proj.at(3, 2) == -1.0F); // Perspective w projection
    TEST_CHECK(vp.at(3, 2) != 0.0F || vp.at(3, 0) != 0.0F);

    // Orbit
    float initialYaw = camera.getYaw();
    camera.orbit(0.5F, 0.2F);
    TEST_CHECK(camera.getYaw() == initialYaw + 0.5F);
    TEST_CHECK(camera.getPivot().isNearlyEqual(Vec3::zero())); // Pivot must stay fixed during orbit

    // Pitch clamping prevents singularity
    camera.orbit(0.0F, 10.0F); // Push far up
    TEST_CHECK(camera.getPitch() <= 1.553343F);
    camera.orbit(0.0F, -20.0F); // Push far down
    TEST_CHECK(camera.getPitch() >= -1.553343F);

    // Pan
    camera.resetToDefault();
    Vec3 prePanPos = camera.getPosition();
    Vec3 prePanPivot = camera.getPivot();
    camera.pan(50.0F, 30.0F);
    Vec3 deltaPos = camera.getPosition() - prePanPos;
    Vec3 deltaPivot = camera.getPivot() - prePanPivot;
    TEST_CHECK(deltaPos.isNearlyEqual(deltaPivot, 1e-4F)); // Position and pivot move identically

    // Zoom
    float preZoomDist = camera.getDistance();
    camera.zoom(2.0F);
    TEST_CHECK(camera.getDistance() < preZoomDist);
    camera.zoom(-4.0F);
    TEST_CHECK(camera.getDistance() > preZoomDist);

    // Zoom clamping prevents passing through pivot
    camera.zoom(100.0F);
    TEST_CHECK(camera.getDistance() >= 0.2F);

    // Fly movement
    Vec3 preFlyPos = camera.getPosition();
    camera.fly(Vec3{0.0F, 1.0F, 0.0F}, 0.5F, 1.0F); // Move Up by 0.5s * 5.0m/s = 2.5m
    TEST_CHECK(std::abs(camera.getPosition().y - (preFlyPos.y + 2.5F)) < 1e-4F);

    // Projection and Unprojection accuracy
    camera.resetToDefault();
    camera.updateMatrices(1000.0F, 1000.0F);
    Vec3 canvasOrigin{100.0F, 100.0F, 0.0F};
    Vec3 canvasSize{1000.0F, 1000.0F, 0.0F};

    // Project pivot (0,0,0) -> should land at canvas center (600, 600)
    Vec3 projectedScreen;
    float depth = 0.0F;
    bool visible =
        camera.project(camera.getPivot(), canvasOrigin, canvasSize, projectedScreen, depth);
    TEST_CHECK(visible);
    TEST_CHECK(depth > 0.0F);
    TEST_CHECK(std::abs(projectedScreen.x - 600.0F) < 1.0F);
    TEST_CHECK(std::abs(projectedScreen.y - 600.0F) < 1.0F);

    // Unproject canvas center -> ray must pass through pivot
    Ray centerRay = camera.unproject(Vec3{600.0F, 600.0F, 0.0F}, canvasOrigin, canvasSize);
    auto tHit = centerRay.intersectSphere(camera.getPivot(), 0.5F);
    TEST_CHECK(tHit.has_value());

    std::cout << "[PASS] testEditorCameraLifecycleAndNavigation\n";
}

static void testEditorCameraNavigationAndFlyMode() {
    EditorCamera camera;
    camera.resetToDefault();

    // 1. Forward and backward movement
    {
        Vec3 startPos = camera.getPosition();
        Vec3 fwd = camera.getForward();
        camera.fly(Vec3{0.0F, 0.0F, 1.0F}, 1.0F, 1.0F); // Forward 1s * 5m/s = 5m
        Vec3 expectedPos = startPos + (fwd * 5.0F);
        TEST_CHECK(camera.getPosition().isNearlyEqual(expectedPos, 1e-3F));

        // Backward
        camera.fly(Vec3{0.0F, 0.0F, -1.0F}, 1.0F, 1.0F); // Backward 5m
        TEST_CHECK(camera.getPosition().isNearlyEqual(startPos, 1e-3F));
    }

    // 2. Left and right movement
    {
        Vec3 startPos = camera.getPosition();
        Vec3 right = camera.getRight();
        camera.fly(Vec3{1.0F, 0.0F, 0.0F}, 1.0F, 1.0F); // Right 5m
        Vec3 expectedPos = startPos + (right * 5.0F);
        TEST_CHECK(camera.getPosition().isNearlyEqual(expectedPos, 1e-3F));

        // Left
        camera.fly(Vec3{-1.0F, 0.0F, 0.0F}, 1.0F, 1.0F); // Left 5m
        TEST_CHECK(camera.getPosition().isNearlyEqual(startPos, 1e-3F));
    }

    // 3. Vertical movement (Q/E)
    {
        Vec3 startPos = camera.getPosition();
        camera.fly(Vec3{0.0F, 1.0F, 0.0F}, 1.0F, 1.0F); // Up 5m
        TEST_CHECK(std::abs(camera.getPosition().y - (startPos.y + 5.0F)) < 1e-3F);

        camera.fly(Vec3{0.0F, -1.0F, 0.0F}, 1.0F, 1.0F); // Down 5m
        TEST_CHECK(std::abs(camera.getPosition().y - startPos.y) < 1e-3F);
    }

    // 4. Diagonal movement normalization
    {
        Vec3 startPos = camera.getPosition();
        // Diagonal input: Right (1) + Forward (1) -> magnitude is sqrt(2)
        camera.fly(Vec3{1.0F, 0.0F, 1.0F}, 1.0F, 1.0F);
        float movedDist = (camera.getPosition() - startPos).length();
        // Without normalization, distance would be 5.0 * sqrt(2) = 7.071m
        // With normalization, distance MUST be exactly 5.0m
        TEST_CHECK(std::abs(movedDist - 5.0F) < 1e-3F);
    }

    // 5. Delta-time independence
    {
        camera.resetToDefault();

        // 10 small steps of 0.1s
        for (int i = 0; i < 10; ++i) {
            camera.fly(Vec3{0.0F, 0.0F, 1.0F}, 0.1F, 1.0F);
        }
        Vec3 pos10Steps = camera.getPosition();

        // 1 large step of 1.0s
        camera.resetToDefault();
        camera.fly(Vec3{0.0F, 0.0F, 1.0F}, 1.0F, 1.0F);
        Vec3 pos1Step = camera.getPosition();

        TEST_CHECK(pos10Steps.isNearlyEqual(pos1Step, 1e-3F));
    }

    // 6. Shift speed multiplier (4.0x -> 20.0 m/s)
    {
        camera.resetToDefault();
        Vec3 startPos = camera.getPosition();
        camera.fly(Vec3{0.0F, 0.0F, 1.0F}, 1.0F, EditorCamera::kFastSpeedMultiplier);
        float fastDist = (camera.getPosition() - startPos).length();
        TEST_CHECK(std::abs(fastDist - 20.0F) < 1e-3F);
    }

    // 7. Ctrl precision multiplier (0.2x -> 1.0 m/s)
    {
        camera.resetToDefault();
        Vec3 startPos = camera.getPosition();
        camera.fly(Vec3{0.0F, 0.0F, 1.0F}, 1.0F, EditorCamera::kSlowSpeedMultiplier);
        float slowDist = (camera.getPosition() - startPos).length();
        TEST_CHECK(std::abs(slowDist - 1.0F) < 1e-3F);
    }

    // 8. Mouse rotation & pitch clamping
    {
        camera.resetToDefault();
        float initialYaw = camera.getYaw();
        float initialPitch = camera.getPitch();

        // Mouse look
        camera.look(100.0F, 50.0F);
        TEST_CHECK(camera.getYaw() != initialYaw);
        TEST_CHECK(camera.getPitch() != initialPitch);

        // Clamping check: push upward pitch to extreme
        camera.look(0.0F, -10000.0F);
        TEST_CHECK(camera.getPitch() <= 1.553343F);

        // Push downward pitch to extreme
        camera.look(0.0F, 20000.0F);
        TEST_CHECK(camera.getPitch() >= -1.553343F);
    }

    // 9. Orbit/Fly mode switching
    {
        camera.resetToDefault();
        TEST_CHECK(camera.getCameraMode() == EditorCameraMode::Orbit);

        camera.setCameraMode(EditorCameraMode::Fly);
        TEST_CHECK(camera.getCameraMode() == EditorCameraMode::Fly);

        camera.setCameraMode(EditorCameraMode::Orbit);
        TEST_CHECK(camera.getCameraMode() == EditorCameraMode::Orbit);
    }

    // 10. Navigation activation and deactivation
    {
        camera.resetToDefault();
        TEST_CHECK(!camera.isNavigating());

        camera.setNavigating(true);
        TEST_CHECK(camera.isNavigating());

        camera.setNavigating(false);
        TEST_CHECK(!camera.isNavigating());
    }

    // 11. Input capture safety simulation
    {
        camera.resetToDefault();
        Vec3 initialPos = camera.getPosition();

        // Simulate typing in text input (io.WantTextInput == true)
        bool wantTextInput = true;
        Vec3 inputDir{0.0F, 0.0F, 1.0F};
        if (!wantTextInput) {
            camera.fly(inputDir, 1.0F, 1.0F);
        }
        // Camera position must not move when text input is active
        TEST_CHECK(camera.getPosition().isNearlyEqual(initialPos));

        // Simulate gizmo dragging active
        bool isDraggingGizmo = true;
        if (!isDraggingGizmo) {
            camera.fly(inputDir, 1.0F, 1.0F);
        }
        TEST_CHECK(camera.getPosition().isNearlyEqual(initialPos));
    }

    // 12. Camera independence from ECS entity transforms
    {
        world::World world;
        physics::registerPhysicsComponents(world);
        auto e = world.createEntity("TestEntity");
        world.setComponent(e, physics::PhysicsTransformComponent{
                                  .transform = Transform{Vec3{10.0F, 20.0F, 30.0F}},
                              });

        camera.resetToDefault();
        camera.fly(Vec3{1.0F, 1.0F, 1.0F}, 2.0F, EditorCamera::kFastSpeedMultiplier);
        camera.look(50.0F, 50.0F);
        camera.orbit(1.0F, 0.5F);

        // Entity transform must remain exactly identical
        const auto* ptAfter = world.getComponent<physics::PhysicsTransformComponent>(e);
        TEST_CHECK(ptAfter != nullptr);
        TEST_CHECK(ptAfter->transform.position.isNearlyEqual(Vec3{10.0F, 20.0F, 30.0F}));
    }

    // 13. Existing framing and orbit behavior preserved
    {
        camera.resetToDefault();
        camera.frame(Vec3{5.0F, 0.0F, 5.0F}, 3.0F);
        TEST_CHECK(camera.getPivot().isNearlyEqual(Vec3{5.0F, 0.0F, 5.0F}));
        TEST_CHECK(camera.getDistance() > 3.0F);

        Vec3 pivotBefore = camera.getPivot();
        camera.orbit(0.5F, 0.3F);
        TEST_CHECK(camera.getPivot().isNearlyEqual(pivotBefore));
    }

    // 14. Speed adjustment & clamping
    {
        camera.resetToDefault();
        camera.setMoveSpeed(10.0F);
        TEST_CHECK(camera.getMoveSpeed() == 10.0F);

        camera.adjustMoveSpeed(1.0F); // increase
        TEST_CHECK(camera.getMoveSpeed() > 10.0F);

        camera.adjustMoveSpeed(-1.0F); // decrease
        TEST_CHECK(camera.getMoveSpeed() <= 10.0F * 1.15F);

        // Clamping
        camera.setMoveSpeed(9999.0F);
        TEST_CHECK(camera.getMoveSpeed() == EditorCamera::kMaxMoveSpeed);

        camera.setMoveSpeed(-50.0F);
        TEST_CHECK(camera.getMoveSpeed() == EditorCamera::kMinMoveSpeed);
    }

    std::cout << "[PASS] testEditorCameraNavigationAndFlyMode\n";
}

static void testViewportRayPickingAndEntitySelection() {
    world::World testWorld;
    physics::registerPhysicsComponents(testWorld);

    // Entity 1: Cube at (0, 0, 0) with half extents (1, 1, 1)
    auto eBox = testWorld.createEntity("BoxEntity");
    testWorld.setComponent(eBox, physics::PhysicsTransformComponent{
                                     .transform = Transform{Vec3{0.0F, 0.0F, 0.0F}},
                                 });
    testWorld.setComponent(eBox, physics::ColliderComponent{
                                     .desc = physics::ColliderDesc::makeBox(Vec3{1.0F, 1.0F, 1.0F}),
                                 });

    // Entity 2: Sphere at (6, 0, 0) with radius 1.5
    auto eSphere = testWorld.createEntity("SphereEntity");
    testWorld.setComponent(eSphere, physics::PhysicsTransformComponent{
                                        .transform = Transform{Vec3{6.0F, 0.0F, 0.0F}},
                                    });
    testWorld.setComponent(eSphere, physics::ColliderComponent{
                                        .desc = physics::ColliderDesc::makeSphere(1.5F),
                                    });

    EditorCamera camera;
    // Position camera at (0, 0, 10) looking towards origin (0, 0, 0)
    camera.setPosition(Vec3{0.0F, 0.0F, 10.0F});
    camera.setPivot(Vec3{0.0F, 0.0F, 0.0F});
    camera.updateMatrices(1000.0F, 1000.0F);

    Vec3 canvasOrigin{0.0F, 0.0F, 0.0F};
    Vec3 canvasSize{1000.0F, 1000.0F, 0.0F};

    // 1. Ray through screen center (500, 500) must hit eBox
    Ray centerRay = camera.unproject(Vec3{500.0F, 500.0F, 0.0F}, canvasOrigin, canvasSize);
    auto tBoxHit = centerRay.intersectAABB(Vec3{-1.0F, -1.0F, -1.0F}, Vec3{1.0F, 1.0F, 1.0F});
    TEST_CHECK(tBoxHit.has_value());
    TEST_CHECK(*tBoxHit > 0.0F);

    // 2. Project eSphere (6, 0, 0) to screen and unproject to verify hit
    Vec3 sphereScreen{};
    float sphereDepth = 0.0F;
    bool sphereVis =
        camera.project(Vec3{6.0F, 0.0F, 0.0F}, canvasOrigin, canvasSize, sphereScreen, sphereDepth);
    TEST_CHECK(sphereVis);
    Ray sphereRay = camera.unproject(sphereScreen, canvasOrigin, canvasSize);
    auto tSphereHit = sphereRay.intersectSphere(Vec3{6.0F, 0.0F, 0.0F}, 1.5F);
    TEST_CHECK(tSphereHit.has_value());

    // 3. Ray through top-left corner (10, 10) must miss both entities
    Ray missRay = camera.unproject(Vec3{10.0F, 10.0F, 0.0F}, canvasOrigin, canvasSize);
    auto tMissBox = missRay.intersectAABB(Vec3{-1.0F, -1.0F, -1.0F}, Vec3{1.0F, 1.0F, 1.0F});
    auto tMissSphere = missRay.intersectSphere(Vec3{6.0F, 0.0F, 0.0F}, 1.5F);
    TEST_CHECK(!tMissBox.has_value());
    TEST_CHECK(!tMissSphere.has_value());

    // 4. Stale / destroyed entity handle safety
    testWorld.destroyEntity(eBox);
    TEST_CHECK(!testWorld.isAlive(eBox));

    std::cout << "[PASS] testViewportRayPickingAndEntitySelection\n";
}

static void testViewportTransformGizmoManipulation() {
    world::World testWorld;
    physics::registerPhysicsComponents(testWorld);

    auto entity = testWorld.createEntity("GizmoTarget");
    testWorld.setComponent(entity, physics::PhysicsTransformComponent{
                                       .transform = Transform{Vec3{0.0F, 0.0F, 0.0F}},
                                   });

    SelectionModel selection;
    selection.select(entity);
    TEST_CHECK(selection.isSelected(entity));

    // Simulate Gizmo Translation along X axis by +3.5 meters
    auto* tc = testWorld.getMutableComponent<physics::PhysicsTransformComponent>(entity);
    TEST_CHECK(tc != nullptr);

    tc->transform.position += Vec3{3.5F, 0.0F, 0.0F};
    TEST_CHECK(tc->transform.position.isNearlyEqual(Vec3{3.5F, 0.0F, 0.0F}));

    // Simulate Gizmo Rotation 90 degrees around Y axis
    const float halfPi = std::numbers::pi_v<float> * 0.5F;
    const Quat rotY = Quat::fromAxisAngle(Vec3::unitY(), halfPi);
    tc->transform.rotation = (rotY * tc->transform.rotation).normalized();
    Vec3 rotatedForward = tc->transform.rotation.rotate(Vec3{0.0F, 0.0F, -1.0F});
    TEST_CHECK(rotatedForward.isNearlyEqual(Vec3{-1.0F, 0.0F, 0.0F}, 1e-3F));

    // Simulate Gizmo Scale along Z by +1.5
    tc->transform.scale.z += 1.5F;
    TEST_CHECK(tc->transform.scale.z == 2.5F);

    std::cout << "[PASS] testViewportTransformGizmoManipulation\n";
}

static void testViewportRuntimeWorldPreviewAndSelectionSafety() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto nullDevice = std::move(devRes.value());

    EditorEngine editor;
    EditorConfig config{.autoSaveLayout = false};
    TEST_CHECK(editor.initialize(*nullDevice, config));

    world::World editWorld;
    physics::registerPhysicsComponents(editWorld);
    auto e1 = editWorld.createEntity("AuthoredEntity");
    editWorld.setComponent(e1, physics::PhysicsTransformComponent{
                                   .transform = Transform{Vec3{0.0F, 10.0F, 0.0F}},
                               });

    editor.bindWorld(&editWorld);
    editor.getSelectionModel().select(e1);
    TEST_CHECK(editor.getSelectionModel().isSelected(e1));

    // Start Simulation -> Playing
    auto playRes = editor.getRuntimeSession().play();
    TEST_CHECK(playRes.hasValue());

    auto cmdRes = nullDevice->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto nullCmd = std::move(cmdRes.value());

    // In frame begin, active world should be Runtime World and selection cleared
    editor.beginFrame(Duration::fromMilliseconds(16), 1280, 720);
    TEST_CHECK(editor.getSelectionModel().empty()); // Selection safety across world boundary

    auto* activeWorld = editor.getRuntimeSession().getActiveWorld();
    TEST_CHECK(activeWorld != nullptr);
    TEST_CHECK(activeWorld != &editWorld);

    // Mutate runtime entity
    auto rtHandles = activeWorld->getAllEntities();
    TEST_CHECK(!rtHandles.empty());
    auto* rtTc = activeWorld->getMutableComponent<physics::PhysicsTransformComponent>(rtHandles[0]);
    TEST_CHECK(rtTc != nullptr);
    rtTc->transform.position.y = 1.0F; // Simulating physical drop

    nullCmd->begin();
    editor.drawPanels();
    editor.endFrame(*nullCmd);
    nullCmd->end();

    // Stop Simulation -> Return to Edit
    auto stopRes = editor.getRuntimeSession().stop();
    TEST_CHECK(stopRes.hasValue());

    editor.beginFrame(Duration::fromMilliseconds(16), 1280, 720);
    TEST_CHECK(editor.getRuntimeSession().getActiveWorld() == &editWorld);
    TEST_CHECK(editor.getSelectionModel().empty());

    // Edit World entity must remain completely intact at original (0, 10, 0)
    auto* editTc = editWorld.getComponent<physics::PhysicsTransformComponent>(e1);
    TEST_CHECK(editTc != nullptr);
    TEST_CHECK(editTc->transform.position.y == 10.0F);

    nullCmd->begin();
    editor.drawPanels();
    editor.endFrame(*nullCmd);
    nullCmd->end();

    editor.shutdown();
    std::cout << "[PASS] testViewportRuntimeWorldPreviewAndSelectionSafety\n";
}

static void testEditorMeshAssetPipelineAndRendering() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto& device = *devRes.value();

    auto cmdRes = device.createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto nullCmd = std::move(cmdRes.value());

    world::World editWorld;
    physics::registerPhysicsComponents(editWorld);
    render::registerRenderComponents(editWorld);

    EditorEngine editor;
    TEST_CHECK(editor.initialize(device));
    editor.bindWorld(&editWorld);

    // 1. AssetManager integration in editor
    auto& assetManager = editor.getAssetManager();
    auto loadRes = assetManager.loadMesh("assets/models/cube.obj");
    TEST_CHECK(loadRes.hasValue());
    const render::AssetId cubeId = loadRes.value();
    TEST_CHECK(assetManager.isLoaded(cubeId));

    // 2. Create mesh entity in Edit World
    auto meshEnt = editWorld.createEntity("MeshCubeEntity");
    editWorld.setComponent(meshEnt, physics::PhysicsTransformComponent{
                                        .transform = Transform(Vec3(0.0F, 1.0F, 0.0F)),
                                    });
    editWorld.setComponent(meshEnt, render::MeshComponent{
                                        .meshAsset = cubeId,
                                        .assetPath = "assets/models/cube.obj",
                                        .submeshIndex = 0,
                                        .visible = true,
                                        .castShadows = true,
                                    });

    // 3. Render frame with mesh entity
    editor.beginFrame(Duration::fromMilliseconds(16), 1280, 720);
    editor.getSelectionModel().select(meshEnt);

    nullCmd->begin();
    editor.drawPanels();
    editor.endFrame(*nullCmd);
    nullCmd->end();

    // 4. Test scene serialization and round-trip reloading with MeshComponent
    const char* testSceneFile = "test_mesh_scene.hikari";
    TEST_CHECK(editor.getWorkspaceManager().saveScene(editWorld, testSceneFile));

    world::World reloadedWorld;
    physics::registerPhysicsComponents(reloadedWorld);
    render::registerRenderComponents(reloadedWorld);

    TEST_CHECK(editor.getWorkspaceManager().openScene(reloadedWorld, testSceneFile));
    auto reloadedEntities = reloadedWorld.getAllEntities();
    TEST_CHECK(!reloadedEntities.empty());

    bool foundMeshComponent = false;
    for (auto h : reloadedEntities) {
        if (reloadedWorld.hasComponent<render::MeshComponent>(h)) {
            const auto* mc = reloadedWorld.getComponent<render::MeshComponent>(h);
            TEST_CHECK(mc != nullptr);
            TEST_CHECK(mc->assetPath == "assets/models/cube.obj");
            TEST_CHECK(mc->meshAsset == cubeId);
            TEST_CHECK(mc->visible);
            foundMeshComponent = true;
        }
    }
    TEST_CHECK(foundMeshComponent);

    // Clean up temporary scene file
    std::filesystem::remove(testSceneFile);

    // 5. Test Runtime Session with MeshComponent
    auto playRes = editor.getRuntimeSession().play();
    TEST_CHECK(playRes.hasValue());

    editor.beginFrame(Duration::fromMilliseconds(16), 1280, 720);
    world::World* runtimeWorld = editor.getRuntimeSession().getActiveWorld();
    TEST_CHECK(runtimeWorld != nullptr);
    TEST_CHECK(runtimeWorld != &editWorld);

    auto rtEntities = runtimeWorld->getAllEntities();
    TEST_CHECK(!rtEntities.empty());
    bool rtHasMesh = false;
    for (auto h : rtEntities) {
        if (runtimeWorld->hasComponent<render::MeshComponent>(h)) {
            rtHasMesh = true;
        }
    }
    TEST_CHECK(rtHasMesh);

    nullCmd->begin();
    editor.drawPanels();
    editor.endFrame(*nullCmd);
    nullCmd->end();

    auto stopRes = editor.getRuntimeSession().stop();
    TEST_CHECK(stopRes.hasValue());

    editor.shutdown();
    std::cout << "[PASS] testEditorMeshAssetPipelineAndRendering\n";
}

static void testEditorGpuViewportIntegrationLifecycle() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto nullDevice = std::move(devRes.value());

    EditorEngine editor;
    TEST_CHECK(!editor.isInitialized());

    bool initOk = editor.initialize(*nullDevice);
    TEST_CHECK(initOk);
    TEST_CHECK(editor.isInitialized());

    // 1. SceneRenderer initialization within EditorEngine
    auto& renderer = editor.getSceneRenderer();
    TEST_CHECK(renderer.isInitialized());
    TEST_CHECK(renderer.getColorTarget().isValid());
    TEST_CHECK(renderer.getDepthTarget().isValid());
    TEST_CHECK(renderer.getWidth() == 1280);
    TEST_CHECK(renderer.getHeight() == 720);

    // 2. Viewport zero-size handling and collapse regression (Finding 8B.2-03)
    world::World editWorld;
    editor.bindWorld(&editWorld);

    // Simulate collapsed viewport canvas (0x0)
    editor.beginFrame(Duration::fromMilliseconds(16), 1280, 720);
    editor.drawPanels();

    auto cmdRes = nullDevice->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto nullCmd = std::move(cmdRes.value());

    nullCmd->begin();
    editor.renderScene(*nullCmd);
    editor.endFrame(*nullCmd);
    nullCmd->end();

    // 3. Repeated resize behavior
    auto resizeRes1 = renderer.resize(960, 540);
    TEST_CHECK(resizeRes1.hasValue());
    TEST_CHECK(renderer.getWidth() == 960);
    TEST_CHECK(renderer.getHeight() == 540);

    auto resizeRes2 = renderer.resize(1920, 1080);
    TEST_CHECK(resizeRes2.hasValue());
    TEST_CHECK(renderer.getWidth() == 1920);
    TEST_CHECK(renderer.getHeight() == 1080);

    // 4. Runtime session transition safety (Edit -> Simulating -> Edit)
    auto playRes = editor.getRuntimeSession().play();
    TEST_CHECK(playRes.hasValue());
    TEST_CHECK(editor.getRuntimeSession().isSimulating());

    nullCmd->begin();
    editor.renderScene(*nullCmd);
    editor.endFrame(*nullCmd);
    nullCmd->end();

    auto stopRes = editor.getRuntimeSession().stop();
    TEST_CHECK(stopRes.hasValue());
    TEST_CHECK(!editor.getRuntimeSession().isSimulating());

    editor.shutdown();
    TEST_CHECK(!editor.isInitialized());
    std::cout << "[PASS] testEditorGpuViewportIntegrationLifecycle\n";
}

static void testEditorGpuViewportVulkanHardware() {
    rhi::VulkanDeviceDesc vkDesc{.windowHandle = nullptr, .enableValidation = false};
    auto devRes = rhi::createVulkanDevice(vkDesc);
    if (!devRes.hasValue()) {
        std::cout << "[SKIP] testEditorGpuViewportVulkanHardware (no Vulkan device)\n";
        return;
    }
    auto vkDevice = std::move(devRes.value());

    EditorEngine editor;
    bool initOk = editor.initialize(*vkDevice);
    TEST_CHECK(initOk);
    TEST_CHECK(editor.isInitialized());

    // 1. GPU Viewport active state and texture registration
    TEST_CHECK(editor.isGpuViewportActive());

    auto& renderer = editor.getSceneRenderer();
    TEST_CHECK(renderer.isInitialized());
    TEST_CHECK(renderer.getColorTarget().isValid());
    TEST_CHECK(renderer.getDepthTarget().isValid());

    // Verify native handle resolution for ImGui Vulkan backend
    void* nativeView = vkDevice->getNativeTextureView(renderer.getColorTarget());
    TEST_CHECK(nativeView != nullptr);

    // 2. Viewport resize and descriptor replacement on GPU
    auto resizeRes = renderer.resize(1024, 768);
    TEST_CHECK(resizeRes.hasValue());
    TEST_CHECK(renderer.getWidth() == 1024);
    TEST_CHECK(renderer.getHeight() == 768);

    void* newNativeView = vkDevice->getNativeTextureView(renderer.getColorTarget());
    TEST_CHECK(newNativeView != nullptr);

    // 3. World Mesh rendering and command list execution
    world::World world;
    physics::registerPhysicsComponents(world);
    editor.bindWorld(&world);

    auto loadRes = editor.getAssetManager().loadMesh("assets/models/cube.obj");
    TEST_CHECK(loadRes.hasValue());
    render::AssetId cubeId = loadRes.value();

    auto& ecs = world.getRawWorld();
    auto entity = ecs.entity("ViewportGpuCube");
    entity.set<render::MeshComponent>(render::MeshComponent{
        .meshAsset = cubeId,
        .visible = true,
    });
    entity.set<physics::PhysicsTransformComponent>(physics::PhysicsTransformComponent{
        .transform = Transform(Vec3(0.0F, 0.0F, 0.0F)),
    });

    auto nullDevRes = rhi::createNullDevice();
    TEST_CHECK(nullDevRes.hasValue());
    auto nullDev = std::move(nullDevRes.value());
    auto nullCmdRes = nullDev->createCommandList();
    TEST_CHECK(nullCmdRes.hasValue());
    auto nullCmd = std::move(nullCmdRes.value());

    editor.beginFrame(Duration::fromMilliseconds(16), 1280, 720);
    editor.drawPanels();
    editor.endFrame(*nullCmd);

    auto cmdRes = vkDevice->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto vkCmd = std::move(cmdRes.value());

    vkCmd->begin();
    editor.renderScene(*vkCmd);
    vkCmd->end();

    auto subRes = vkDevice->submit(vkCmd.get());
    TEST_CHECK(subRes.hasValue());
    vkDevice->waitIdle();

    // 4. Runtime / Edit World transition safety on GPU
    auto playRes = editor.getRuntimeSession().play();
    TEST_CHECK(playRes.hasValue());
    TEST_CHECK(editor.isGpuViewportActive());

    vkCmd->begin();
    editor.renderScene(*vkCmd);
    vkCmd->end();

    TEST_CHECK(vkDevice->submit(vkCmd.get()).hasValue());
    vkDevice->waitIdle();

    auto stopRes = editor.getRuntimeSession().stop();
    TEST_CHECK(stopRes.hasValue());
    TEST_CHECK(editor.isGpuViewportActive());

    // 5. Dynamic viewport resizing regression test (first-frame / in-flight safety)
    TEST_CHECK(editor.resizeViewport(960, 540));
    TEST_CHECK(renderer.getWidth() == 960);
    TEST_CHECK(renderer.getHeight() == 540);
    TEST_CHECK(editor.isGpuViewportActive());

    editor.beginFrame(Duration::fromMilliseconds(16), 1280, 720);
    editor.drawPanels();
    editor.endFrame(*nullCmd);

    vkCmd->begin();
    editor.renderScene(*vkCmd);
    vkCmd->end();
    TEST_CHECK(vkDevice->submit(vkCmd.get()).hasValue());
    vkDevice->waitIdle();

    // Consecutive resize to non-standard dimensions
    TEST_CHECK(editor.resizeViewport(765, 364));
    TEST_CHECK(renderer.getWidth() == 765);
    TEST_CHECK(renderer.getHeight() == 364);
    TEST_CHECK(editor.isGpuViewportActive());

    editor.beginFrame(Duration::fromMilliseconds(16), 1280, 720);
    editor.drawPanels();
    editor.endFrame(*nullCmd);

    vkCmd->begin();
    editor.renderScene(*vkCmd);
    vkCmd->end();
    TEST_CHECK(vkDevice->submit(vkCmd.get()).hasValue());
    vkDevice->waitIdle();

    editor.shutdown();
    TEST_CHECK(!editor.isInitialized());
    std::cout << "[PASS] testEditorGpuViewportVulkanHardware\n";
}

static void testObjDragAndDropSpawning() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto nullDev = std::move(devRes.value());

    EditorEngine editor;
    EditorConfig config{.autoSaveLayout = false};
    TEST_CHECK(editor.initialize(*nullDev, config));

    world::World world;
    physics::registerPhysicsComponents(world);
    render::registerRenderComponents(world);
    editor.bindWorld(&world);

    // 1. ContentBrowser discovery & relative path validation
    ContentBrowserController cb("assets");
    TEST_CHECK(cb.navigateTo("assets/models"));
    const auto& items = cb.getItems();
    const ContentBrowserItem* cubeItem = nullptr;
    for (const auto& item : items) {
        if (item.name == "cube.obj") {
            cubeItem = &item;
        }
    }
    TEST_CHECK(cubeItem != nullptr);
    TEST_CHECK(cubeItem->isMesh);
    TEST_CHECK(cubeItem->relativePath == "assets/models/cube.obj");

    // 2. AssetManager loads OBJ vertices, triangles, and bounds
    auto& am = editor.getAssetManager();
    auto loadRes = am.loadMesh(cubeItem->relativePath);
    TEST_CHECK(loadRes.hasValue());
    auto meshId = loadRes.value();
    const auto* md = am.getMeshData(meshId);
    TEST_CHECK(md != nullptr);
    TEST_CHECK(md->getVertexCount() > 0);
    TEST_CHECK(md->getTriangleCount() > 0);
    TEST_CHECK(md->bounds.isValid());

    // 3. ImGui multi-frame drag and drop into Viewport
    // Frame 1: Establish layout and panels
    editor.beginFrame(Duration::fromMilliseconds(16), 1280, 720);
    editor.drawPanels();
    editor.endFrame(*nullDev->createCommandList().value());

    auto simulateMeshDrop = [&](const std::string& path) {
        ImGuiIO& io = ImGui::GetIO();
        for (int f = 0; f < 2; ++f) {
            editor.beginFrame(Duration::fromMilliseconds(16), 1280, 720);
            io.MousePos = ImVec2(400.0F, 200.0F); // Center of Scene Viewport
            io.MouseDown[0] = true;
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern)) {
                ImGui::SetDragDropPayload("CONTENT_BROWSER_MESH", path.c_str(), path.size() + 1);
                ImGui::EndDragDropSource();
            }
            editor.drawPanels();
            editor.endFrame(*nullDev->createCommandList().value());
        }
        editor.beginFrame(Duration::fromMilliseconds(16), 1280, 720);
        io.MousePos = ImVec2(400.0F, 200.0F);
        io.MouseDown[0] = false;
        editor.drawPanels();
        editor.endFrame(*nullDev->createCommandList().value());
    };

    // First drop: cube.obj
    simulateMeshDrop(cubeItem->relativePath);

    // 4. Verify spawned entity in ECS
    auto entities = world.getAllEntities();
    TEST_CHECK(!entities.empty());

    world::EntityHandle firstEntity{};
    for (auto ent : entities) {
        if (world.hasComponent<render::MeshComponent>(ent)) {
            firstEntity = ent;
            break;
        }
    }
    TEST_CHECK(firstEntity.isValid());

    const auto* tc = world.getComponent<physics::PhysicsTransformComponent>(firstEntity);
    TEST_CHECK(tc != nullptr);

    const auto* mc = world.getComponent<render::MeshComponent>(firstEntity);
    TEST_CHECK(mc != nullptr);
    TEST_CHECK(mc->assetPath == "assets/models/cube.obj");
    TEST_CHECK(mc->meshAsset == render::makeAssetIdFromPath("assets/models/cube.obj"));
    TEST_CHECK(mc->visible);
    TEST_CHECK(mc->castShadows);

    const auto* col = world.getComponent<physics::ColliderComponent>(firstEntity);
    TEST_CHECK(col != nullptr);
    TEST_CHECK(col->desc.type == physics::ColliderType::Box);
    TEST_CHECK(col->desc.halfExtents.x > 0.0F);
    TEST_CHECK(col->desc.halfExtents.y > 0.0F);
    TEST_CHECK(col->desc.halfExtents.z > 0.0F);

    // Verify entity was selected
    const auto& sel = editor.getSelectionModel();
    TEST_CHECK(sel.isSelected(firstEntity));

    // 5. Test duplicate drop produces UNIQUE entities (no name collision in Flecs)
    simulateMeshDrop(cubeItem->relativePath);

    auto allEntitiesAfterSecondDrop = world.getAllEntities();
    size_t meshEntityCount = 0;
    for (auto ent : allEntitiesAfterSecondDrop) {
        if (world.hasComponent<render::MeshComponent>(ent)) {
            meshEntityCount++;
        }
    }
    TEST_CHECK(meshEntityCount == 2);

    // 6. Test graceful handling of invalid / missing asset path
    const std::string nonExistentPath = "assets/models/non_existent.obj";
    simulateMeshDrop(nonExistentPath);

    // 7. Verify SceneRenderer executes cleanly with all entities
    auto cmdRes = nullDev->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());
    cmd->begin();
    editor.renderScene(*cmd);
    cmd->end();

    editor.shutdown();
    std::cout << "[PASS] testObjDragAndDropSpawning\n";
}

static void testContentBrowserImportWorkflow() {
    auto devRes = rhi::createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto nullDev = std::move(devRes.value());

    EditorEngine editor;
    EditorConfig config{.autoSaveLayout = false};
    TEST_CHECK(editor.initialize(*nullDev, config));

    auto* panelRaw = editor.getWorkspaceManager().findPanel("content_browser");
    TEST_CHECK(panelRaw != nullptr);
    auto* cbPanel = static_cast<ContentBrowserPanel*>(panelRaw);

    // 1. Create a temporary external OBJ asset
    std::filesystem::path extDir =
        std::filesystem::temp_directory_path() / "hikari_import_test_tmp";
    std::error_code ec;
    std::filesystem::create_directories(extDir, ec);

    std::filesystem::path extObj = extDir / "external_imported_cube.obj";
    {
        std::ofstream out(extObj);
        out << "# Valid Wavefront OBJ\n"
            << "v -0.5 -0.5 0.5\n"
            << "v 0.5 -0.5 0.5\n"
            << "v 0.5 0.5 0.5\n"
            << "f 1 2 3\n";
    }

    // 2. Queue external file for import
    cbPanel->queuePendingImports({extObj});

    // 3. Process frame (which executes processPendingImports)
    editor.beginFrame(Duration::fromMilliseconds(16), 1280, 720);
    editor.drawPanels();
    editor.endFrame(*nullDev->createCommandList().value());

    // 4. Verify imported into asset directory
    std::filesystem::path expectedDest =
        cbPanel->getController().getRootDirectory() / "external_imported_cube.obj";
    TEST_CHECK(std::filesystem::exists(expectedDest));

    // Verify enumerated in items
    bool foundInItems = false;
    for (const auto& it : cbPanel->getController().getItems()) {
        if (it.name == "external_imported_cube.obj") {
            foundInItems = true;
            TEST_CHECK(it.isMesh);
            break;
        }
    }
    TEST_CHECK(foundInItems);

    // 5. Test queueing invalid file reports error gracefully
    std::filesystem::path badFile = extDir / "corrupted_empty.obj";
    {
        std::ofstream out(badFile);
        out << "invalid obj header without geometry\n";
    }
    cbPanel->queuePendingImports({badFile});

    editor.beginFrame(Duration::fromMilliseconds(16), 1280, 720);
    editor.drawPanels();
    editor.endFrame(*nullDev->createCommandList().value());

    // Clean up temporary files
    std::filesystem::remove(expectedDest, ec);
    std::filesystem::remove_all(extDir, ec);

    editor.shutdown();
    std::cout << "[PASS] testContentBrowserImportWorkflow\n";
}

static void testEditorShutdownWithUploadedMeshAssets() {
    // 1. Test explicit shutdown with uploaded GPU mesh assets
    {
        auto devRes = rhi::createNullDevice();
        TEST_CHECK(devRes.hasValue());
        auto nullDev = std::move(devRes.value());

        EditorEngine editor;
        EditorConfig config{.autoSaveLayout = false};
        TEST_CHECK(editor.initialize(*nullDev, config));

        auto loadRes = editor.getAssetManager().loadMesh("assets/models/cube.obj");
        TEST_CHECK(loadRes.hasValue());
        auto uploadRes = editor.getAssetManager().uploadMesh(loadRes.value(), *nullDev);
        TEST_CHECK(uploadRes.hasValue());
        TEST_CHECK(uploadRes.value() != nullptr);

        // Explicit shutdown must unload AssetManager while nullDev is still valid and alive
        editor.shutdown();
        TEST_CHECK(!editor.isInitialized());

        // Now destroy the device
        nullDev.reset();
        // EditorEngine is destroyed at end of scope without crashing
    }

    // 2. Test implicit RAII destructor shutdown with uploaded GPU mesh assets
    {
        auto devRes = rhi::createNullDevice();
        TEST_CHECK(devRes.hasValue());
        auto nullDev = std::move(devRes.value());

        {
            EditorEngine editor;
            EditorConfig config{.autoSaveLayout = false};
            TEST_CHECK(editor.initialize(*nullDev, config));

            auto loadRes = editor.getAssetManager().loadMesh("assets/models/cube.obj");
            TEST_CHECK(loadRes.hasValue());
            auto uploadRes = editor.getAssetManager().uploadMesh(loadRes.value(), *nullDev);
            TEST_CHECK(uploadRes.hasValue());

            // Exiting this inner scope destructs EditorEngine BEFORE nullDev is destroyed
        }

        // nullDev safely destroyed here
        nullDev.reset();
    }

    std::cout << "[PASS] testEditorShutdownWithUploadedMeshAssets\n";
}

int main() {
    std::cout << "Running Editor subsystem unit tests...\n";
    const std::filesystem::path candidates[] = {"assets/models", "../../../assets/models",
                                                "../../assets/models", "../assets/models"};
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c)) {
            std::error_code ec;
            std::filesystem::path targetDir = c.parent_path().parent_path();
            if (!targetDir.empty()) {
                std::filesystem::current_path(targetDir, ec);
            }
            break;
        }
    }
    testEditorShutdownWithUploadedMeshAssets();
    testObjDragAndDropSpawning();
    testContentBrowserImportWorkflow();
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
    testInspectorComponentManagementLifecycle();
    testTransformAndPhysicsPropertyEditing();
    testMultiSelectionPropertyEditingAndBatchOperations();
    testWorkspaceSceneLifecycleAndFileOperations();
    testEditorRuntimeSessionIntegrationAndIsolation();
    testEditorCameraLifecycleAndNavigation();
    testEditorCameraNavigationAndFlyMode();
    testViewportRayPickingAndEntitySelection();
    testViewportTransformGizmoManipulation();
    testViewportRuntimeWorldPreviewAndSelectionSafety();
    testEditorMeshAssetPipelineAndRendering();
    testEditorGpuViewportIntegrationLifecycle();
    testEditorGpuViewportVulkanHardware();
    std::cout << "All Editor subsystem tests PASSED!\n";
    return 0;
}
