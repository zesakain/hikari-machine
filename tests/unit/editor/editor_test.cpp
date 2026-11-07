/// @file editor_test.cpp
/// @brief Comprehensive test suite for the Dear ImGui editor tooling subsystem.

#include "editor/editor_engine.h"
#include "framework/conductor.h"
#include "framework/mode.h"
#include "platform/input_snapshot.h"
#include "rhi/command_list.h"
#include "rhi/rhi_device.h"
#include "world/world.h"

#include <cstdlib>
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

int main() {
    std::cout << "Running Editor subsystem unit tests...\n";
    testEditorLifecycle();
    testEditorFrameAndPanels();
    testEditorInputProcessing();
    std::cout << "All Editor subsystem tests PASSED!\n";
    return 0;
}
