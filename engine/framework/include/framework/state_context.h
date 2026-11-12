/// @file state_context.h
/// @brief State-specific execution context separated from shared World ownership.
///
/// Purpose:
/// Stores mode-specific parameters, active camera references, targeting selections,
/// UI layouts, input contexts, and speculative preparation caches.
/// In accordance with Hikari-Machine's Two-State architecture, this state-specific
/// context is maintained separately from the single shared World, ensuring shared
/// entities and continuous simulation components are never duplicated across modes.
///
/// Ownership and Lifetime:
/// Owned by the Conductor or Mode instances. Outlives individual transitions.
/// Does not own ECS entities or resources; references them via generational handles.
///
/// Thread-safety:
/// Main thread access during mode enter, update, prepare, and commit.
///
/// Performance:
/// Pre-allocated small vector buffers; zero heap allocation during hot-path execution.

#pragma once

#include "core/time.h"
#include "world/entity.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eng::framework {

/// @struct CameraStateContext
/// @brief Camera parameters and active viewpoint entity for an execution mode.
struct CameraStateContext {
    world::EntityHandle activeCamera{};
    float fovDegrees{60.0F};
    float nearPlane{0.1F};
    float farPlane{1000.0F};
    float aspectRatio{16.0F / 9.0F};
};

/// @struct TargetSelectionContext
/// @brief Target focus and query candidates evaluated during standby or active state.
struct TargetSelectionContext {
    world::EntityHandle primaryTarget{};
    std::vector<world::EntityHandle> candidateTargets{};
};

/// @struct UiStateContext
/// @brief UI presentation document paths and layer masks for a mode.
struct UiStateContext {
    std::string documentPath{};
    uint32_t activeLayerMask{0xFFFFFFFF};
    bool showHud{true};
};

/// @struct InputStateContext
/// @brief Input mapping context tags and action filtering masks.
struct InputStateContext {
    std::string contextTag{};
    uint32_t actionMask{0xFFFFFFFF};
    bool consumeNavigation{false};
};

/// @struct ExecutionStateConfig
/// @brief Timing and budgeting configuration for a mode's standby preparation.
struct ExecutionStateConfig {
    Duration prepareBudget{Duration::fromMicroseconds(500)};
    float slowMotionScale{0.2F};
    Duration slowMotionDuration{Duration::fromMilliseconds(300)};
};

/// @struct SpeculativePreparationCache
/// @brief Prepared resource handles and pre-validated entity references cached during standby.
struct SpeculativePreparationCache {
    std::vector<uint64_t> preparedAssetIds{};
    std::vector<world::EntityHandle> preparedEntities{};
    bool isReady{false};

    /// Clears cached speculative references.
    void clear() noexcept {
        preparedAssetIds.clear();
        preparedEntities.clear();
        isReady = false;
    }
};

/// @struct StateContext
/// @brief Comprehensive state-specific execution context for a Conductor mode slot.
struct StateContext {
    CameraStateContext camera{};
    TargetSelectionContext targeting{};
    UiStateContext ui{};
    InputStateContext input{};
    ExecutionStateConfig execution{};
    SpeculativePreparationCache cache{};

    /// Resets speculative caches while preserving mode configuration.
    void resetCache() noexcept { cache.clear(); }
};

} // namespace eng::framework
