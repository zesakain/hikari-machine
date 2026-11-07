/// @file sdl_platform.cpp
/// @brief SDL3 platform implementation.

#include "sdl_platform.h"

#include "core/assert.h"
#include "sdl_window.h"

#include <cstdlib>

namespace eng::platform {

namespace {
constexpr const char* kWindowPtrProp = "Hikari_WindowPtr";
} // namespace

SdlPlatform::SdlPlatform(const PlatformConfig& config) noexcept
    : config_(config), initialized_(true) {}

SdlPlatform::~SdlPlatform() noexcept {
    shutdown();
}

bool SdlPlatform::isInitialized() const noexcept {
    return initialized_;
}

Result<std::unique_ptr<Window>> SdlPlatform::createWindow(const WindowDesc& desc) {
    if (!initialized_) {
        return Error{.code = ErrorCode::InvalidState, .message = "SdlPlatform is not initialized"};
    }

    SDL_WindowFlags flags = 0;
    if (desc.vulkan && !desc.headless && !config_.headless) {
        flags |= SDL_WINDOW_VULKAN;
    }
    if (desc.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (desc.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    if (config_.headless || desc.headless) {
        flags |= SDL_WINDOW_HIDDEN;
    }

    std::array<char, 256> titleBuf{};
    const size_t copyLen = std::min(desc.title.size(), titleBuf.size() - 1);
    for (size_t i = 0; i < copyLen; ++i) {
        titleBuf[i] = desc.title[i];
    }
    titleBuf[copyLen] = '\0';

    SDL_Window* sdlWin = SDL_CreateWindow(titleBuf.data(), static_cast<int>(desc.width),
                                          static_cast<int>(desc.height), flags);
    if (sdlWin == nullptr) {
        return Error{.code = ErrorCode::WindowCreationFailed,
                     .message = "Failed to create SDL_Window"};
    }

    return std::unique_ptr<Window>(std::make_unique<SdlWindow>(sdlWin, desc.width, desc.height));
}

void SdlPlatform::pollEvents() {
    if (!initialized_) {
        return;
    }

    previousInput_ = currentInput_;
    currentInput_.mouseDeltaX = 0.0F;
    currentInput_.mouseDeltaY = 0.0F;
    currentInput_.wheelDeltaX = 0.0F;
    currentInput_.wheelDeltaY = 0.0F;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT: {
            // Signal close on all known windows if possible
            break;
        }
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
            SDL_Window* sdlWin = SDL_GetWindowFromID(event.window.windowID);
            if (sdlWin != nullptr) {
                const SDL_PropertiesID props = SDL_GetWindowProperties(sdlWin);
                if (props != 0) {
                    auto* win = static_cast<SdlWindow*>(
                        SDL_GetPointerProperty(props, kWindowPtrProp, nullptr));
                    if (win != nullptr) {
                        win->setShouldClose(true);
                    }
                }
            }
            break;
        }
        case SDL_EVENT_KEY_DOWN: {
            const auto sc = static_cast<size_t>(event.key.scancode);
            if (sc < static_cast<size_t>(KeyCode::Count)) {
                currentInput_.keys.set(sc, true);
            }
            break;
        }
        case SDL_EVENT_KEY_UP: {
            const auto sc = static_cast<size_t>(event.key.scancode);
            if (sc < static_cast<size_t>(KeyCode::Count)) {
                currentInput_.keys.set(sc, false);
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            const auto btn = static_cast<size_t>(event.button.button);
            if (btn < static_cast<size_t>(MouseButton::Count)) {
                currentInput_.mouseButtons.set(btn, true);
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const auto btn = static_cast<size_t>(event.button.button);
            if (btn < static_cast<size_t>(MouseButton::Count)) {
                currentInput_.mouseButtons.set(btn, false);
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            currentInput_.mouseX = event.motion.x;
            currentInput_.mouseY = event.motion.y;
            currentInput_.mouseDeltaX += event.motion.xrel;
            currentInput_.mouseDeltaY += event.motion.yrel;
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            currentInput_.wheelDeltaX += event.wheel.x;
            currentInput_.wheelDeltaY += event.wheel.y;
            break;
        }
        case SDL_EVENT_GAMEPAD_ADDED: {
            handleGamepadAdded(event.gdevice.which);
            break;
        }
        case SDL_EVENT_GAMEPAD_REMOVED: {
            handleGamepadRemoved(event.gdevice.which);
            break;
        }
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
            handleGamepadButton(event.gbutton.which, event.gbutton.button, true);
            break;
        }
        case SDL_EVENT_GAMEPAD_BUTTON_UP: {
            handleGamepadButton(event.gbutton.which, event.gbutton.button, false);
            break;
        }
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            handleGamepadAxis(event.gaxis.which, event.gaxis.axis, event.gaxis.value);
            break;
        }
        default:
            break;
        }
    }
}

const InputSnapshot& SdlPlatform::getCurrentInput() const noexcept {
    return currentInput_;
}

const InputSnapshot& SdlPlatform::getPreviousInput() const noexcept {
    return previousInput_;
}

void SdlPlatform::handleGamepadAdded(SDL_JoystickID instanceId) noexcept {
    for (size_t i = 0; i < kMaxGamepads; ++i) {
        if (gamepadSlots_[i].pad == nullptr) {
            SDL_Gamepad* pad = SDL_OpenGamepad(instanceId);
            if (pad != nullptr) {
                gamepadSlots_[i].pad = pad;
                gamepadSlots_[i].id = instanceId;
                currentInput_.gamepads[i].connected = true;
                currentInput_.gamepads[i].buttons.reset();
                currentInput_.gamepads[i].axes.fill(0.0F);
            }
            break;
        }
    }
}

void SdlPlatform::handleGamepadRemoved(SDL_JoystickID instanceId) noexcept {
    for (size_t i = 0; i < kMaxGamepads; ++i) {
        if (gamepadSlots_[i].id == instanceId && gamepadSlots_[i].pad != nullptr) {
            SDL_CloseGamepad(gamepadSlots_[i].pad);
            gamepadSlots_[i].pad = nullptr;
            gamepadSlots_[i].id = 0;
            currentInput_.gamepads[i].connected = false;
            currentInput_.gamepads[i].buttons.reset();
            currentInput_.gamepads[i].axes.fill(0.0F);
            break;
        }
    }
}

void SdlPlatform::handleGamepadButton(SDL_JoystickID instanceId, uint8_t button,
                                      bool down) noexcept {
    for (size_t i = 0; i < kMaxGamepads; ++i) {
        if (gamepadSlots_[i].id == instanceId && gamepadSlots_[i].pad != nullptr) {
            if (button < static_cast<uint8_t>(GamepadButton::Count)) {
                currentInput_.gamepads[i].buttons.set(button, down);
            }
            break;
        }
    }
}

void SdlPlatform::handleGamepadAxis(SDL_JoystickID instanceId, uint8_t axis,
                                    int16_t value) noexcept {
    for (size_t i = 0; i < kMaxGamepads; ++i) {
        if (gamepadSlots_[i].id == instanceId && gamepadSlots_[i].pad != nullptr) {
            if (axis < static_cast<uint8_t>(GamepadAxis::Count)) {
                float normalized = 0.0F;
                if (axis == static_cast<uint8_t>(GamepadAxis::TriggerLeft) ||
                    axis == static_cast<uint8_t>(GamepadAxis::TriggerRight)) {
                    normalized = (value < 0) ? 0.0F : (static_cast<float>(value) / 32767.0F);
                } else {
                    normalized = (value < 0) ? (static_cast<float>(value) / 32768.0F)
                                             : (static_cast<float>(value) / 32767.0F);
                }
                currentInput_.gamepads[i].axes[axis] = normalized;
            }
            break;
        }
    }
}

void SdlPlatform::shutdown() {
    if (!initialized_) {
        return;
    }

    for (auto& slot : gamepadSlots_) {
        if (slot.pad != nullptr) {
            SDL_CloseGamepad(slot.pad);
            slot.pad = nullptr;
            slot.id = 0;
        }
    }

    SDL_Quit();
    initialized_ = false;
    currentInput_.reset();
    previousInput_.reset();
}

Result<std::unique_ptr<Platform>> createPlatform(const PlatformConfig& config) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): std::getenv executed during single-threaded startup
    const char* displayEnv = std::getenv("DISPLAY");
    // NOLINTNEXTLINE(concurrency-mt-unsafe): std::getenv executed during single-threaded startup
    const char* waylandEnv = std::getenv("WAYLAND_DISPLAY");
    const bool isHeadlessEnv = (displayEnv == nullptr && waylandEnv == nullptr);
    if (config.headless || isHeadlessEnv) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
#ifdef __linux__
        // In headless mode (CI / headless simulation tests), disable desktop session D-Bus
        // connection to avoid third-party libdbus lock-order-inversion false positives.
        // NOLINTNEXTLINE(concurrency-mt-unsafe): setenv executed during single-threaded startup
        ::setenv("DBUS_SESSION_BUS_ADDRESS", "disabled:", 1);
#endif
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        return Error{.code = ErrorCode::PlatformInitFailed,
                     .message = "SDL_Init failed for video or gamepad"};
    }

    return std::unique_ptr<Platform>(std::make_unique<SdlPlatform>(config));
}

} // namespace eng::platform
