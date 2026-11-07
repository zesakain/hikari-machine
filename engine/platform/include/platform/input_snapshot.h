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

#include <array>
#include <bitset>
#include <cstdint>

namespace eng::platform {

/// Maximum number of simultaneously supported gamepads.
inline constexpr uint32_t kMaxGamepads = 4;

/// @struct GamepadState
/// @brief Digital buttons and analog axes state for a single connected controller.
struct GamepadState {
    /// Whether this controller port is currently connected.
    bool connected{false};

    /// Digital button states.
    std::bitset<static_cast<size_t>(GamepadButton::Count)> buttons{};

    /// Normalized analog axes. Sticks in [-1.0f, 1.0f], triggers in [0.0f, 1.0f].
    std::array<float, static_cast<size_t>(GamepadAxis::Count)> axes{};
};

/// @struct InputSnapshot
/// @brief Fixed-size, deterministic state snapshot of all input peripherals.
struct InputSnapshot {
    /// Keyboard key states.
    std::bitset<static_cast<size_t>(KeyCode::Count)> keys{};

    /// Mouse button states.
    std::bitset<static_cast<size_t>(MouseButton::Count)> mouseButtons{};

    /// Absolute mouse cursor position in window coordinates (pixels).
    float mouseX{0.0f};
    float mouseY{0.0f};

    /// Relative mouse cursor displacement during this frame (pixels).
    float mouseDeltaX{0.0f};
    float mouseDeltaY{0.0f};

    /// Mouse wheel displacement (positive = forward/up or right).
    float wheelDeltaX{0.0f};
    float wheelDeltaY{0.0f};

    /// Gamepad peripheral states.
    std::array<GamepadState, kMaxGamepads> gamepads{};

    /// Resets all inputs to default unpressed states.
    void reset() noexcept {
        keys.reset();
        mouseButtons.reset();
        mouseX = 0.0f;
        mouseY = 0.0f;
        mouseDeltaX = 0.0f;
        mouseDeltaY = 0.0f;
        wheelDeltaX = 0.0f;
        wheelDeltaY = 0.0f;
        for (auto& pad : gamepads) {
            pad.connected = false;
            pad.buttons.reset();
            pad.axes.fill(0.0f);
        }
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
            return 0.0f;
        }
        const auto idx = static_cast<size_t>(axis);
        return (idx < gamepads[gamepadIndex].axes.size()) ? gamepads[gamepadIndex].axes[idx] : 0.0f;
    }
};

} // namespace eng::platform
