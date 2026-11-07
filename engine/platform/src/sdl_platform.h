/// @file sdl_platform.h
/// @brief SDL3 concrete implementation of eng::platform::Platform interface.
///
/// Purpose:
/// Manages SDL3 subsystem lifecycle, window instantiation, and event pumping into InputSnapshot.
///
/// Ownership and Lifetime:
/// Created by createPlatform() factory. Releases SDL3 and all associated controllers on shutdown.
///
/// Thread-safety:
/// Main thread only.
///
/// Performance:
/// Drains OS event queue with zero allocations in the hot path.

#pragma once

#include "platform/platform.h"

#include <SDL3/SDL.h>
#include <array>

namespace eng::platform {

/// @class SdlPlatform
/// @brief Concrete SDL3 platform subsystem implementation.
class SdlPlatform final : public Platform {
public:
    explicit SdlPlatform(const PlatformConfig& config) noexcept;
    ~SdlPlatform() noexcept override;

    SdlPlatform(const SdlPlatform&) = delete;
    SdlPlatform& operator=(const SdlPlatform&) = delete;
    SdlPlatform(SdlPlatform&&) noexcept = delete;
    SdlPlatform& operator=(SdlPlatform&&) noexcept = delete;

    [[nodiscard]] bool isInitialized() const noexcept override;
    [[nodiscard]] Result<std::unique_ptr<Window>> createWindow(const WindowDesc& desc) override;
    void pollEvents() override;
    [[nodiscard]] const InputSnapshot& getCurrentInput() const noexcept override;
    [[nodiscard]] const InputSnapshot& getPreviousInput() const noexcept override;
    void shutdown() override;

private:
    void handleGamepadAdded(SDL_JoystickID instanceId) noexcept;
    void handleGamepadRemoved(SDL_JoystickID instanceId) noexcept;
    void handleGamepadButton(SDL_JoystickID instanceId, uint8_t button, bool down) noexcept;
    void handleGamepadAxis(SDL_JoystickID instanceId, uint8_t axis, int16_t value) noexcept;

    PlatformConfig config_{};
    bool initialized_{false};
    InputSnapshot currentInput_{};
    InputSnapshot previousInput_{};

    struct GamepadSlot {
        SDL_Gamepad* pad{nullptr};
        SDL_JoystickID id{0};
    };
    std::array<GamepadSlot, kMaxGamepads> gamepadSlots_{};
};

} // namespace eng::platform
