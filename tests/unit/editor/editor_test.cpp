/// @file editor_test.cpp
/// @brief Comprehensive test suite for the Dear ImGui editor tooling subsystem.

#include "editor/editor_engine.h"
#include "framework/conductor.h"
#include "framework/mode.h"
#include "platform/input_snapshot.h"
#include "rhi/command_list.h"
#include "rhi/rhi_device.h"
#include "world/world.h"

#include <cassert>
#include <iostream>

using namespace eng;
using namespace eng::editor;

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
    assert(devRes.hasValue());
    auto nullDevice = std::move(devRes.value());

    EditorEngine editor;
    assert(!editor.isInitialized());

    bool initOk = editor.initialize(*nullDevice);
    assert(initOk);
    assert(editor.isInitialized());

    editor.shutdown();
    assert(!editor.isInitialized());

    // Re-initialization
    initOk = editor.initialize(*nullDevice);
    assert(initOk);
    assert(editor.isInitialized());
    editor.shutdown();

    std::cout << "[PASS] testEditorLifecycle\n";
}

static void testEditorFrameAndPanels() {
    auto devRes = rhi::createNullDevice();
    assert(devRes.hasValue());
    auto nullDevice = std::move(devRes.value());

    auto cmdRes = nullDevice->createCommandList();
    assert(cmdRes.hasValue());
    auto nullCmd = std::move(cmdRes.value());

    world::World world;
    auto modeA = std::make_unique<DummyEditorMode>("ModeA");
    auto modeB = std::make_unique<DummyEditorMode>("ModeB");
    framework::Conductor conductor(world, std::move(modeA), std::move(modeB));
    assert(conductor.initialize().hasValue());

    EditorEngine editor;
    assert(editor.initialize(*nullDevice));

    editor.bindWorld(&world);
    editor.bindConductor(&conductor);

    // Run 3 editor frames
    for (int frame = 0; frame < 3; ++frame) {
        nullCmd->begin();
        editor.beginFrame(Duration::fromMilliseconds(16));
        editor.drawPanels();
        editor.endFrame(*nullCmd);
        nullCmd->end();
        assert(editor.lastDrawCallCount() > 0);
        assert(editor.lastVertexCount() > 0);
    }

    editor.shutdown();
    conductor.shutdown();

    std::cout << "[PASS] testEditorFrameAndPanels\n";
}

static void testEditorInputProcessing() {
    auto devRes = rhi::createNullDevice();
    assert(devRes.hasValue());
    auto nullDevice = std::move(devRes.value());

    auto cmdRes = nullDevice->createCommandList();
    assert(cmdRes.hasValue());
    auto nullCmd = std::move(cmdRes.value());

    EditorEngine editor;
    assert(editor.initialize(*nullDevice));

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
