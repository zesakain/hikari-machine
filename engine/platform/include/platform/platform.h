/// @file platform.h
/// @brief Central platform manager interface encapsulating OS events, windowing, and input.
///
/// Purpose:
/// Coordinates OS lifecycle, window creation, and event pumping. Translates native window
/// messages and input events into deterministic Hikari `InputSnapshot` structures.
///
/// Ownership and Lifetime:
/// The platform manager is instantiated at engine boot and owned via std::unique_ptr by the
/// application shell. It must outlive any active windows or graphics contexts.
///
/// Thread-safety:
/// Must be initialized, pumped, and destroyed on the main thread only.
/// `getCurrentInput()` is safe for read-only access once `pollEvents()` has returned.
///
/// Performance:
/// `pollEvents()` drains the OS message queue without dynamic heap allocations.

#pragma once

#include "core/result.h"
#include "platform/input_snapshot.h"
#include "platform/window.h"

#include <memory>

namespace eng::platform {

/// @struct PlatformConfig
/// @brief Initialization configuration for platform subsystem.
struct PlatformConfig {
    /// Forces headless mode (using offscreen dummy drivers), ideal for CI and unit tests.
    bool headless{false};
};

/// @class Platform
/// @brief Abstract platform subsystem manager.
class Platform {
public:
    virtual ~Platform() = default;

    /// Checks if the underlying platform subsystem is successfully initialized.
    [[nodiscard]] virtual bool isInitialized() const noexcept = 0;

    /// Creates and displays a new platform window.
    ///
    /// @param desc Window configuration descriptor.
    /// @return Result containing owning pointer to Window on success, or
    /// ErrorCode::WindowCreationFailed.
    [[nodiscard]] virtual Result<std::unique_ptr<Window>> createWindow(const WindowDesc& desc) = 0;

    /// Drains OS event queue, updates active windows, and transitions input snapshot buffers.
    virtual void pollEvents() = 0;

    /// Accesses the input snapshot recorded for the current frame.
    [[nodiscard]] virtual const InputSnapshot& getCurrentInput() const noexcept = 0;

    /// Accesses the input snapshot from the previous frame for delta calculations.
    [[nodiscard]] virtual const InputSnapshot& getPreviousInput() const noexcept = 0;

    /// Shuts down the platform subsystem, releasing OS drivers.
    virtual void shutdown() = 0;
};

/// Instantiates and initializes the concrete platform manager (SDL3 backend).
///
/// @param config Subsystem initialization settings.
/// @return Owning pointer to Platform instance, or ErrorCode::PlatformInitFailed on failure.
[[nodiscard]] Result<std::unique_ptr<Platform>> createPlatform(const PlatformConfig& config = {});

} // namespace eng::platform
