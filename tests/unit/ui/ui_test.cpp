/// @file ui_test.cpp
/// @brief Comprehensive test suite for the RmlUi runtime UI subsystem.

#include "platform/input_snapshot.h"
#include "render/render_graph.h"
#include "rhi/command_list.h"
#include "rhi/rhi_device.h"
#include "ui/ui_system.h"

#include <cassert>
#include <iostream>
#include <string_view>

using namespace eng;
using namespace eng::ui;

static const std::string_view kSampleRml = R"(
<rml>
    <head>
        <title>HUD Test</title>
        <style>
            body {
                font-family: Arial;
                font-size: 16px;
                width: 100%;
                height: 100%;
            }
            h1 {
                font-size: 24px;
                color: #ffffff;
            }
            .hud-panel {
                width: 300px;
                height: 100px;
                background-color: #202020;
            }
        </style>
    </head>
    <body>
        <div class="hud-panel">
            <h1>HP: 100 / 100</h1>
        </div>
    </body>
</rml>
)";

static void testUiLifecycle() {
    auto devRes = rhi::createNullDevice();
    assert(devRes.hasValue());
    auto nullDevice = std::move(devRes.value());

    UiSystem uiSystem;
    assert(!uiSystem.isInitialized());

    UiConfig config{.width = 1280, .height = 720, .fontScale = 1.0f};

    auto initRes = uiSystem.initialize(config, *nullDevice);
    assert(initRes.hasValue());
    assert(uiSystem.isInitialized());

    uiSystem.shutdown();
    assert(!uiSystem.isInitialized());

    // Re-initialization
    initRes = uiSystem.initialize(config, *nullDevice);
    assert(initRes.hasValue());
    assert(uiSystem.isInitialized());
    uiSystem.shutdown();

    std::cout << "[PASS] testUiLifecycle\n";
}

static void testDocumentLoadingAndVisibility() {
    auto devRes = rhi::createNullDevice();
    assert(devRes.hasValue());
    auto nullDevice = std::move(devRes.value());

    UiSystem uiSystem;
    UiConfig config{.width = 1280, .height = 720};
    assert(uiSystem.initialize(config, *nullDevice).hasValue());

    // Load document
    auto loadRes = uiSystem.loadDocumentFromMemory(kSampleRml, "hud_document");
    assert(loadRes.hasValue());
    UiDocumentHandle docHandle = loadRes.value();
    assert(docHandle.isValid());

    // Document visibility operations
    uiSystem.showDocument(docHandle);
    uiSystem.hideDocument(docHandle);
    uiSystem.showDocument(docHandle);

    // Update with decoupled clock delta
    uiSystem.update(Duration::fromMilliseconds(16));

    // Close document
    uiSystem.closeDocument(docHandle);

    // Loading invalid RML should fail gracefully
    auto badRes = uiSystem.loadDocumentFromMemory("<invalid xml <<", "bad_doc");
    assert(!badRes.hasValue());
    assert(badRes.error() == UiError::DocumentLoadFailed);

    uiSystem.shutdown();
    std::cout << "[PASS] testDocumentLoadingAndVisibility\n";
}

static void testHeadlessRenderingAndStats() {
    auto devRes = rhi::createNullDevice();
    assert(devRes.hasValue());
    auto nullDevice = std::move(devRes.value());

    auto cmdRes = nullDevice->createCommandList();
    assert(cmdRes.hasValue());
    auto nullCmdList = std::move(cmdRes.value());

    UiSystem uiSystem;
    UiConfig config{.width = 1280, .height = 720};
    assert(uiSystem.initialize(config, *nullDevice).hasValue());

    auto loadRes = uiSystem.loadDocumentFromMemory(kSampleRml, "render_test_doc");
    assert(loadRes.hasValue());
    uiSystem.showDocument(loadRes.value());

    // Advance UI layout
    uiSystem.update(Duration::fromMilliseconds(16));

    // Direct render on NullCommandList
    uiSystem.render(*nullCmdList);

    const auto& stats = uiSystem.renderStats();
    assert(stats.activeDocuments == 1);

    // RenderGraph integration
    render::RenderGraph graph;
    uiSystem.render(graph);
    auto compileRes = graph.compile();
    assert(compileRes.hasValue());

    auto execRes = graph.execute(*nullDevice, *nullCmdList);
    assert(execRes.hasValue());

    uiSystem.shutdown();
    std::cout << "[PASS] testHeadlessRenderingAndStats\n";
}

static void testInputProcessingAndResize() {
    auto devRes = rhi::createNullDevice();
    assert(devRes.hasValue());
    auto nullDevice = std::move(devRes.value());

    UiSystem uiSystem;
    UiConfig config{.width = 1280, .height = 720};
    assert(uiSystem.initialize(config, *nullDevice).hasValue());

    auto loadRes = uiSystem.loadDocumentFromMemory(kSampleRml, "input_doc");
    assert(loadRes.hasValue());
    uiSystem.showDocument(loadRes.value());

    platform::InputSnapshot prevInput{};
    platform::InputSnapshot currInput{};

    // Simulate mouse movement
    currInput.mouseX = 150.0f;
    currInput.mouseY = 50.0f;
    uiSystem.processInput(currInput, prevInput);

    // Simulate mouse button click
    prevInput = currInput;
    currInput.mouseButtons.set(static_cast<size_t>(platform::MouseButton::Left), true);
    uiSystem.processInput(currInput, prevInput);

    // Simulate mouse button release
    prevInput = currInput;
    currInput.mouseButtons.set(static_cast<size_t>(platform::MouseButton::Left), false);
    uiSystem.processInput(currInput, prevInput);

    // Viewport resize
    uiSystem.resize(1920, 1080);
    uiSystem.update(Duration::fromMilliseconds(16));

    uiSystem.shutdown();
    std::cout << "[PASS] testInputProcessingAndResize\n";
}

int main() {
    std::cout << "Running UI subsystem unit tests...\n";
    testUiLifecycle();
    testDocumentLoadingAndVisibility();
    testHeadlessRenderingAndStats();
    testInputProcessingAndResize();
    std::cout << "All UI subsystem tests PASSED!\n";
    return 0;
}
