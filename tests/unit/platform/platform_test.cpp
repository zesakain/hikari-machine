/// @file platform_test.cpp
/// @brief Unit tests for Platform layer, Windowing, and deterministic InputSnapshot.

#include "core/assert.h"
#include "platform/input_snapshot.h"
#include "platform/input_types.h"
#include "platform/platform.h"
#include "platform/window.h"

#include <iostream>

using namespace eng::platform;

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__          \
                      << std::endl;                                                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

static int testInputSnapshotKeyboard() {
    InputSnapshot s1{};
    InputSnapshot s2{};

    TEST_CHECK(!s1.isKeyDown(KeyCode::A));
    TEST_CHECK(!s1.isKeyDown(KeyCode::Space));

    s2.keys.set(static_cast<size_t>(KeyCode::A), true);
    TEST_CHECK(s2.isKeyDown(KeyCode::A));
    TEST_CHECK(s2.wasKeyPressed(KeyCode::A, s1));
    TEST_CHECK(!s2.wasKeyReleased(KeyCode::A, s1));

    // Next frame: A held down
    s1 = s2;
    TEST_CHECK(s2.isKeyDown(KeyCode::A));
    TEST_CHECK(!s2.wasKeyPressed(KeyCode::A, s1));
    TEST_CHECK(!s2.wasKeyReleased(KeyCode::A, s1));

    // Next frame: A released
    s2.keys.set(static_cast<size_t>(KeyCode::A), false);
    TEST_CHECK(!s2.isKeyDown(KeyCode::A));
    TEST_CHECK(!s2.wasKeyPressed(KeyCode::A, s1));
    TEST_CHECK(s2.wasKeyReleased(KeyCode::A, s1));

    return 0;
}

static int testInputSnapshotMouseAndGamepad() {
    InputSnapshot s1{};
    InputSnapshot s2{};

    // Mouse buttons
    s2.mouseButtons.set(static_cast<size_t>(MouseButton::Left), true);
    TEST_CHECK(s2.isMouseButtonDown(MouseButton::Left));
    TEST_CHECK(s2.wasMouseButtonPressed(MouseButton::Left, s1));

    // Gamepad
    TEST_CHECK(!s1.isGamepadConnected(0));
    TEST_CHECK(!s1.isGamepadButtonDown(0, GamepadButton::South));
    TEST_CHECK(s1.getGamepadAxis(0, GamepadAxis::LeftX) == 0.0f);

    s2.gamepads[0].connected = true;
    s2.gamepads[0].buttons.set(static_cast<size_t>(GamepadButton::South), true);
    s2.gamepads[0].axes[static_cast<size_t>(GamepadAxis::LeftX)] = 0.75f;

    TEST_CHECK(s2.isGamepadConnected(0));
    TEST_CHECK(s2.isGamepadButtonDown(0, GamepadButton::South));
    TEST_CHECK(s2.wasGamepadButtonPressed(0, GamepadButton::South, s1));
    TEST_CHECK(s2.getGamepadAxis(0, GamepadAxis::LeftX) == 0.75f);

    // Reset
    s2.reset();
    TEST_CHECK(!s2.isMouseButtonDown(MouseButton::Left));
    TEST_CHECK(!s2.isGamepadConnected(0));
    TEST_CHECK(s2.getGamepadAxis(0, GamepadAxis::LeftX) == 0.0f);

    return 0;
}

static int testPlatformAndWindowLifecycle() {
    PlatformConfig config{.headless = true};
    auto platformRes = createPlatform(config);
    TEST_CHECK(platformRes.hasValue());
    auto platform = std::move(platformRes.value());
    TEST_CHECK(platform->isInitialized());

    WindowDesc desc{.title = "PlatformTestWindow",
                    .width = 800,
                    .height = 600,
                    .resizable = true,
                    .fullscreen = false,
                    .headless = true};

    auto winRes = platform->createWindow(desc);
    TEST_CHECK(winRes.hasValue());
    auto window = std::move(winRes.value());
    TEST_CHECK(window != nullptr);

    TEST_CHECK(window->getWidth() == 800);
    TEST_CHECK(window->getHeight() == 600);
    TEST_CHECK(!window->shouldClose());
    TEST_CHECK(window->getNativeHandle() != nullptr);

    window->setTitle("UpdatedTestTitle");
    window->setSize(1024, 768);
    TEST_CHECK(window->getWidth() == 1024);
    TEST_CHECK(window->getHeight() == 768);

    window->setShouldClose(true);
    TEST_CHECK(window->shouldClose());

    platform->pollEvents();
    const auto& curr = platform->getCurrentInput();
    const auto& prev = platform->getPreviousInput();
    TEST_CHECK(!curr.isKeyDown(KeyCode::A));
    TEST_CHECK(!prev.isKeyDown(KeyCode::A));

    platform->shutdown();
    TEST_CHECK(!platform->isInitialized());

    return 0;
}

int main() {
    if (testInputSnapshotKeyboard() != 0) {
        return 1;
    }
    if (testInputSnapshotMouseAndGamepad() != 0) {
        return 1;
    }
    if (testPlatformAndWindowLifecycle() != 0) {
        return 1;
    }
    std::cout << "All Platform unit tests passed!\n";
    return 0;
}
