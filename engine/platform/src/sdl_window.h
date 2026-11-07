/// @file sdl_window.h
/// @brief SDL3 concrete implementation of eng::platform::Window interface.
///
/// Purpose:
/// Encapsulates native SDL_Window lifecycle and properties behind the engine's Window interface.
///
/// Ownership and Lifetime:
/// Created by SdlPlatform and returned as std::unique_ptr<Window> to caller.
/// On destruction, releases the underlying SDL_Window handle.
///
/// Thread-safety:
/// Main thread only.
///
/// Performance:
/// Negligible overhead over native SDL window calls.

#pragma once

#include "platform/window.h"

#include <SDL3/SDL.h>

namespace eng::platform {

/// @class SdlWindow
/// @brief Concrete SDL3 implementation of Window interface.
class SdlWindow final : public Window {
public:
    /// Constructs SdlWindow taking ownership of an existing SDL_Window pointer.
    ///
    /// @param window Pointer to valid SDL_Window. Must not be null.
    /// @param width Initial client width in pixels.
    /// @param height Initial client height in pixels.
    explicit SdlWindow(SDL_Window* window, uint32_t width, uint32_t height) noexcept;

    ~SdlWindow() noexcept override;

    SdlWindow(const SdlWindow&) = delete;
    SdlWindow& operator=(const SdlWindow&) = delete;
    SdlWindow(SdlWindow&&) noexcept = delete;
    SdlWindow& operator=(SdlWindow&&) noexcept = delete;

    [[nodiscard]] uint32_t getWidth() const noexcept override;
    [[nodiscard]] uint32_t getHeight() const noexcept override;
    [[nodiscard]] bool isMinimized() const noexcept override;
    [[nodiscard]] bool shouldClose() const noexcept override;
    void setShouldClose(bool close) noexcept override;
    void setTitle(std::string_view title) override;
    void setSize(uint32_t width, uint32_t height) override;
    [[nodiscard]] void* getNativeHandle() const noexcept override;

private:
    SDL_Window* window_{nullptr};
    uint32_t width_{0};
    uint32_t height_{0};
    bool shouldClose_{false};
};

} // namespace eng::platform
