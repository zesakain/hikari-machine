/// @file scene_renderer.h
/// @brief Scene mesh rendering system and RHI command recorder.
///
/// Purpose:
/// Traverses ECS entities with MeshComponent and PhysicsTransformComponent, resolves their
/// GPU mesh buffers via AssetManager, and records draw commands into an RhiCommandList.
///
/// Ownership and Lifetime:
/// Lightweight rendering executor. Typically called per frame from render passes or viewport.
///
/// Thread-safety:
/// Called from main/render thread.

#pragma once

#include "core/result.h"
#include "render/asset_manager.h"
#include "render/mesh_component.h"
#include "rhi/command_list.h"
#include "rhi/rhi_device.h"
#include "world/world.h"

#include <cstdint>

namespace eng::render {

/// @struct RenderStats
/// @brief Telemetry metrics collected during scene rendering.
struct RenderStats {
    uint32_t drawCalls{0};
    uint32_t triangles{0};
    uint32_t meshesRendered{0};
};

/// @class SceneRenderer
/// @brief Traverses the ECS World and records mesh draw calls to the RHI command list.
class SceneRenderer {
public:
    SceneRenderer() = default;

    /// @brief Renders all visible mesh entities in the World to the specified command list.
    ///
    /// @param world The ECS simulation or edit world.
    /// @param assetManager Repository for GPU mesh buffers.
    /// @param device The active RHI device for on-demand GPU uploads.
    /// @param cmdList Target command list for recording vertex/index binds and draw calls.
    /// @return Render statistics collected for this pass.
    RenderStats renderScene(world::World& world, AssetManager& assetManager, rhi::RhiDevice& device,
                            rhi::RhiCommandList& cmdList);
};

} // namespace eng::render
