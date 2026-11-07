/// @file physics_system.h
/// @brief Global physics subsystem lifecycle coordinator and allocator registration.
///
/// Purpose:
/// Coordinates one-time process initialization and shutdown for the physics backend (Jolt).
/// Ensures factory registrations and memory allocator hooks are set up before any PhysicsWorld
/// is instantiated.
///
/// Ownership and Lifetime:
/// Global singleton lifecycle managed explicitly at engine startup and shutdown.
///
/// Thread-safety:
/// initialize() and shutdown() must be called on the main thread during engine startup/shutdown.

#pragma once

#include "core/result.h"

namespace eng::physics {

/// @class PhysicsSystem
/// @brief Global physics backend lifecycle and allocator coordinator.
class PhysicsSystem {
public:
    /// Initializes global Jolt physics allocators, type registrations, and factory instances.
    [[nodiscard]] static Result<void> initialize();

    /// Shuts down global Jolt physics factory instances and tears down registrations.
    static void shutdown() noexcept;

    /// Checks whether the global physics backend is currently initialized.
    [[nodiscard]] static bool isInitialized() noexcept;
};

} // namespace eng::physics
