/// @file window.h
/// @brief Abstract platform window interface and creation descriptor.
///
/// Purpose:
/// Defines the window abstraction for managing OS display surface lifecycle, sizing,
/// and presentation targets without exposing underlying windowing subsystem headers.
///
/// Ownership and Lifetime:
/// Concrete window instances are owned via std::unique_ptr by the caller (typically Application
/// or Platform manager). Destroying the window destroys the underlying OS window resource.
///
/// Thread-safety:
/// Window operations must be executed exclusively on the main thread.
///
/// Performance:
/// Methods are non-allocating lightweight property queries or OS system calls.
/// Safe for frame-boundary execution, but should not be called in tight inner loops.

#pragma once

#include "core/result.h"

#include <cstdint>
#include <memory>
#include <string_view>

namespace eng::platform {

/// @struct WindowDesc
/// @brief Configuration descriptor used to instantiate a platform window.
struct WindowDesc {
    /// Window title shown in OS window frame.
    std::string_view title{"Hikari"};

    /// Initial client area width in physical/screen pixels.
    uint32_t width{1280};

    /// Initial client area height in physical/screen pixels.
    uint32_t height{720};

    /// Whether the user can resize the window boundaries.
    bool resizable{true};

    /// Whether the window should launch in exclusive/borderless fullscreen mode.
    bool fullscreen{false};

    /// Whether to force headless mode (offscreen dummy surface for CI/benchmarking).
    bool headless{false};
};

/// @class Window
/// @brief Abstract OS display window representation.
class Window {
public:
    virtual ~Window() = default;

    /// Returns current client area width in pixels.
    [[nodiscard]] virtual uint32_t getWidth() const noexcept = 0;

    /// Returns current client area height in pixels.
    [[nodiscard]] virtual uint32_t getHeight() const noexcept = 0;

    /// Returns true if the window is currently minimized (rendering can be skipped/throttled).
    [[nodiscard]] virtual bool isMinimized() const noexcept = 0;

    /// Returns true if a window close request was initiated (e.g., OS close button or Alt+F4).
    [[nodiscard]] virtual bool shouldClose() const noexcept = 0;

    /// Explicitly flags or unflags the window close request.
    virtual void setShouldClose(bool close) noexcept = 0;

    /// Updates window title bar string.
    virtual void setTitle(std::string_view title) = 0;

    /// Resizes the window client area.
    /// @param width Width in pixels (must be > 0).
    /// @param height Height in pixels (must be > 0).
    virtual void setSize(uint32_t width, uint32_t height) = 0;

    /// Returns non-owning native window handle (e.g. SDL_Window* or OS handle).
    [[nodiscard]] virtual void* getNativeHandle() const noexcept = 0;
};

} // namespace eng::platform
