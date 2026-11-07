/// @file physics_system.cpp
/// @brief Global Jolt Physics factory and allocator initialization.

#include "physics/physics_system.h"

#include "jolt_bridge.h"

#include <atomic>

namespace eng::physics {

namespace {
std::atomic<bool> gIsInitialized{false};
} // namespace

Result<void> PhysicsSystem::initialize() {
    if (gIsInitialized.load(std::memory_order_relaxed)) {
        return Result<void>::success();
    }

    // Register Jolt default memory allocator hooks
    JPH::RegisterDefaultAllocator();

    // Create Jolt factory instance
    JPH::Factory::sInstance = new JPH::Factory();

    // Register all standard Jolt physics types
    JPH::RegisterTypes();

    gIsInitialized.store(true, std::memory_order_release);
    return Result<void>::success();
}

void PhysicsSystem::shutdown() noexcept {
    if (!gIsInitialized.load(std::memory_order_relaxed)) {
        return;
    }

    JPH::UnregisterTypes();

    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    gIsInitialized.store(false, std::memory_order_release);
}

bool PhysicsSystem::isInitialized() noexcept {
    return gIsInitialized.load(std::memory_order_acquire);
}

} // namespace eng::physics
