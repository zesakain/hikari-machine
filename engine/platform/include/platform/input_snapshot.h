/// @file input_snapshot.h
/// @brief Deterministic, immutable per-frame snapshot of input device states.
///
/// Purpose:
/// Captures the full state of keyboard, mouse, and gamepads sampled at the start of a frame.
/// Enables deterministic gameplay simulation, input buffering, and replay without querying
/// live OS/platform event loops mid-frame.
///
/// Ownership and Lifetime:
/// Value type with value semantics. Copied or moved across frames and simulation steps.
/// Contains no dynamic heap allocations or raw pointers.
///
/// Thread-safety:
/// Const methods are safe to read concurrently from multiple jobs or threads.
/// Modifying an instance is not thread-safe and must be confined to the main thread / pump.
///
/// Performance:
/// Trivially copyable layout under 512 bytes. Safe for the hot path.

#pragma once

#include "core/assert.h"
#include "platform/input_types.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <string_view>

namespace eng::platform {

/// Maximum number of simultaneously supported gamepads.
inline constexpr uint32_t kMaxGamepads = 4;

/// Maximum number of discrete key events recorded per frame.
inline constexpr size_t kMaxKeyEventsPerFrame = 32;

/// Maximum number of UTF-8 characters/bytes accumulated from text input events per frame.
inline constexpr size_t kMaxTextInputBytes = 64;

/// @struct KeyEvent
/// @brief Discrete key press, release, or repeat event recorded during the frame.
struct KeyEvent {
    KeyCode code{KeyCode::Unknown};
    bool down{false};
    bool repeat{false};
};

/// @struct GamepadState
/// @brief Digital buttons and analog axes state for a single connected controller.
struct GamepadState {
    /// Whether this controller port is currently connected.
    bool connected{false};

    /// Digital button states.
    std::bitset<static_cast<size_t>(GamepadButton::Count)> buttons;

    /// Normalized analog axes. Sticks in [-1.0f, 1.0f], triggers in [0.0f, 1.0f].
    std::array<float, static_cast<size_t>(GamepadAxis::Count)> axes;
};

/// @struct InputSnapshot
/// @brief Fixed-size, deterministic state snapshot of all input peripherals.
struct InputSnapshot {
    /// Keyboard key states.
    std::bitset<static_cast<size_t>(KeyCode::Count)> keys;

    /// Discrete key events that occurred during this frame.
    std::array<KeyEvent, kMaxKeyEventsPerFrame> keyEvents{};
    uint32_t keyEventCount{0};

    /// Null-terminated UTF-8 characters typed during this frame.
    std::array<char, kMaxTextInputBytes> textInput{};
    uint32_t textInputLength{0};

    /// Mouse button states.
    std::bitset<static_cast<size_t>(MouseButton::Count)> mouseButtons;

    /// Absolute mouse cursor position in window coordinates (pixels).
    float mouseX{0.0F};
    float mouseY{0.0F};

    /// Relative mouse cursor displacement during this frame (pixels).
    float mouseDeltaX{0.0F};
    float mouseDeltaY{0.0F};

    /// Mouse wheel displacement (positive = forward/up or right).
    float wheelDeltaX{0.0F};
    float wheelDeltaY{0.0F};

    /// Gamepad peripheral states.
    std::array<GamepadState, kMaxGamepads> gamepads;

    /// Whether the main window currently holds OS input/keyboard focus.
    bool isWindowFocused{true};

    /// Whether window focus state changed in this frame.
    bool focusChanged{false};

    /// Resets all inputs to default unpressed states.
    void reset() noexcept {
        keys.reset();
        keyEvents.fill(KeyEvent{});
        keyEventCount = 0;
        textInput.fill('\0');
        textInputLength = 0;
        mouseButtons.reset();
        mouseX = 0.0F;
        mouseY = 0.0F;
        mouseDeltaX = 0.0F;
        mouseDeltaY = 0.0F;
        wheelDeltaX = 0.0F;
        wheelDeltaY = 0.0F;
        focusChanged = false;
        for (auto& pad : gamepads) {
            pad.connected = false;
            pad.buttons.reset();
            pad.axes.fill(0.0F);
        }
    }

    /// Appends a discrete key event to the per-frame event buffer.
    void addKeyEvent(KeyCode code, bool down, bool repeat = false) noexcept {
        if (keyEventCount < kMaxKeyEventsPerFrame) {
            keyEvents[keyEventCount++] = KeyEvent{.code = code, .down = down, .repeat = repeat};
        }
        const auto idx = static_cast<size_t>(code);
        if (idx < keys.size()) {
            keys.set(idx, down);
        }
    }

    /// Appends UTF-8 text to the per-frame text input buffer.
    void addTextInput(std::string_view text) noexcept {
        if (text.empty() || textInputLength >= kMaxTextInputBytes - 1) {
            return;
        }
        const size_t available = (kMaxTextInputBytes - 1) - textInputLength;
        const size_t toCopy = std::min(text.size(), available);
        for (size_t i = 0; i < toCopy; ++i) {
            textInput[textInputLength + i] = text[i];
        }
        textInputLength += static_cast<uint32_t>(toCopy);
        textInput[textInputLength] = '\0';
    }

    /// Checks if a key is currently held down.
    [[nodiscard]] constexpr bool isKeyDown(KeyCode key) const noexcept {
        const auto idx = static_cast<size_t>(key);
        return idx < keys.size() && keys.test(idx);
    }

    /// Checks if a key was pressed down in this snapshot compared to the previous snapshot.
    [[nodiscard]] constexpr bool wasKeyPressed(KeyCode key,
                                               const InputSnapshot& previous) const noexcept {
        return isKeyDown(key) && !previous.isKeyDown(key);
    }

    /// Checks if a key was released in this snapshot compared to the previous snapshot.
    [[nodiscard]] constexpr bool wasKeyReleased(KeyCode key,
                                                const InputSnapshot& previous) const noexcept {
        return !isKeyDown(key) && previous.isKeyDown(key);
    }

    /// Checks if a mouse button is currently held down.
    [[nodiscard]] constexpr bool isMouseButtonDown(MouseButton button) const noexcept {
        const auto idx = static_cast<size_t>(button);
        return idx < mouseButtons.size() && mouseButtons.test(idx);
    }

    /// Checks if a mouse button was clicked during this snapshot.
    [[nodiscard]] constexpr bool
    wasMouseButtonPressed(MouseButton button, const InputSnapshot& previous) const noexcept {
        return isMouseButtonDown(button) && !previous.isMouseButtonDown(button);
    }

    /// Checks if a mouse button was released during this snapshot.
    [[nodiscard]] constexpr bool
    wasMouseButtonReleased(MouseButton button, const InputSnapshot& previous) const noexcept {
        return !isMouseButtonDown(button) && previous.isMouseButtonDown(button);
    }

    /// Checks if a gamepad is currently connected.
    [[nodiscard]] constexpr bool isGamepadConnected(uint32_t gamepadIndex) const noexcept {
        return gamepadIndex < kMaxGamepads && gamepads[gamepadIndex].connected;
    }

    /// Checks if a gamepad button is held down.
    [[nodiscard]] constexpr bool isGamepadButtonDown(uint32_t gamepadIndex,
                                                     GamepadButton button) const noexcept {
        if (gamepadIndex >= kMaxGamepads || !gamepads[gamepadIndex].connected) {
            return false;
        }
        const auto idx = static_cast<size_t>(button);
        return idx < gamepads[gamepadIndex].buttons.size() &&
               gamepads[gamepadIndex].buttons.test(idx);
    }

    /// Checks if a gamepad button was pressed this frame.
    [[nodiscard]] constexpr bool
    wasGamepadButtonPressed(uint32_t gamepadIndex, GamepadButton button,
                            const InputSnapshot& previous) const noexcept {
        return isGamepadButtonDown(gamepadIndex, button) &&
               !previous.isGamepadButtonDown(gamepadIndex, button);
    }

    /// Checks if a gamepad button was released this frame.
    [[nodiscard]] constexpr bool
    wasGamepadButtonReleased(uint32_t gamepadIndex, GamepadButton button,
                             const InputSnapshot& previous) const noexcept {
        return !isGamepadButtonDown(gamepadIndex, button) &&
               previous.isGamepadButtonDown(gamepadIndex, button);
    }

    /// Returns the normalized value of a gamepad axis.
    /// Sticks range from [-1.0f, 1.0f], triggers range from [0.0f, 1.0f].
    [[nodiscard]] constexpr float getGamepadAxis(uint32_t gamepadIndex,
                                                 GamepadAxis axis) const noexcept {
        if (gamepadIndex >= kMaxGamepads || !gamepads[gamepadIndex].connected) {
            return 0.0F;
        }
        const auto idx = static_cast<size_t>(axis);
        return (idx < gamepads[gamepadIndex].axes.size()) ? gamepads[gamepadIndex].axes[idx] : 0.0F;
    }
};

static_assert(sizeof(InputSnapshot) <= 512, "InputSnapshot must fit in 512 bytes for hot path");

} // namespace eng::platform
