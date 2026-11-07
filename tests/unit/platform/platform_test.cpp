/// @file platform_test.cpp
/// @brief Unit tests for Platform layer, Windowing, and deterministic InputSnapshot.

#include "core/assert.h"
#include "platform/input_snapshot.h"
#include "platform/input_types.h"
#include "platform/platform.h"
#include "platform/window.h"

#include <cassert>
#include <iostream>

using namespace eng::platform;

static void testInputSnapshotKeyboard() {
    InputSnapshot s1{};
    InputSnapshot s2{};

    assert(!s1.isKeyDown(KeyCode::A));
    assert(!s1.isKeyDown(KeyCode::Space));

    s2.keys.set(static_cast<size_t>(KeyCode::A), true);
    assert(s2.isKeyDown(KeyCode::A));
    assert(s2.wasKeyPressed(KeyCode::A, s1));
    assert(!s2.wasKeyReleased(KeyCode::A, s1));

    // Next frame: A held down
    s1 = s2;
    assert(s2.isKeyDown(KeyCode::A));
    assert(!s2.wasKeyPressed(KeyCode::A, s1));
    assert(!s2.wasKeyReleased(KeyCode::A, s1));

    // Next frame: A released
    s2.keys.set(static_cast<size_t>(KeyCode::A), false);
    assert(!s2.isKeyDown(KeyCode::A));
    assert(!s2.wasKeyPressed(KeyCode::A, s1));
    assert(s2.wasKeyReleased(KeyCode::A, s1));

    std::cout << "testInputSnapshotKeyboard passed\n";
}

static void testInputSnapshotMouseAndGamepad() {
    InputSnapshot s1{};
    InputSnapshot s2{};

    // Mouse buttons
    s2.mouseButtons.set(static_cast<size_t>(MouseButton::Left), true);
    assert(s2.isMouseButtonDown(MouseButton::Left));
    assert(s2.wasMouseButtonPressed(MouseButton::Left, s1));

    // Gamepad
    assert(!s1.isGamepadConnected(0));
    assert(!s1.isGamepadButtonDown(0, GamepadButton::South));
    assert(s1.getGamepadAxis(0, GamepadAxis::LeftX) == 0.0f);

    s2.gamepads[0].connected = true;
    s2.gamepads[0].buttons.set(static_cast<size_t>(GamepadButton::South), true);
    s2.gamepads[0].axes[static_cast<size_t>(GamepadAxis::LeftX)] = 0.75f;

    assert(s2.isGamepadConnected(0));
    assert(s2.isGamepadButtonDown(0, GamepadButton::South));
    assert(s2.wasGamepadButtonPressed(0, GamepadButton::South, s1));
    assert(s2.getGamepadAxis(0, GamepadAxis::LeftX) == 0.75f);

    // Reset
    s2.reset();
    assert(!s2.isMouseButtonDown(MouseButton::Left));
    assert(!s2.isGamepadConnected(0));
    assert(s2.getGamepadAxis(0, GamepadAxis::LeftX) == 0.0f);

    std::cout << "testInputSnapshotMouseAndGamepad passed\n";
}

static void testPlatformAndWindowLifecycle() {
    PlatformConfig config{.headless = true};
    auto platformRes = createPlatform(config);
    assert(platformRes.hasValue());
    auto platform = std::move(platformRes.value());
    assert(platform->isInitialized());

    WindowDesc desc{.title = "PlatformTestWindow",
                    .width = 800,
                    .height = 600,
                    .resizable = true,
                    .fullscreen = false,
                    .headless = true};

    auto winRes = platform->createWindow(desc);
    assert(winRes.hasValue());
    auto window = std::move(winRes.value());
    assert(window != nullptr);

    assert(window->getWidth() == 800);
    assert(window->getHeight() == 600);
    assert(!window->shouldClose());
    assert(window->getNativeHandle() != nullptr);

    window->setTitle("UpdatedTestTitle");
    window->setSize(1024, 768);
    assert(window->getWidth() == 1024);
    assert(window->getHeight() == 768);

    window->setShouldClose(true);
    assert(window->shouldClose());

    platform->pollEvents();
    const auto& curr = platform->getCurrentInput();
    const auto& prev = platform->getPreviousInput();
    assert(!curr.isKeyDown(KeyCode::A));
    assert(!prev.isKeyDown(KeyCode::A));

    platform->shutdown();
    assert(!platform->isInitialized());

    std::cout << "testPlatformAndWindowLifecycle passed\n";
}

int main() {
    testInputSnapshotKeyboard();
    testInputSnapshotMouseAndGamepad();
    testPlatformAndWindowLifecycle();
    std::cout << "All Platform unit tests passed!\n";
    return 0;
}
