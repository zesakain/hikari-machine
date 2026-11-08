/// @file scene_renderer.h
/// @brief Scene mesh rendering system and RHI command recorder.
///
/// Purpose:
/// Traverses ECS entities with MeshComponent and PhysicsTransformComponent, resolves their
/// GPU mesh buffers via AssetManager, and records draw commands into an RhiCommandList.
/// Manages offscreen color and depth render targets, shaders, and graphics pipeline for
/// GPU-based scene viewport rendering.
///
/// Ownership and Lifetime:
/// Owns offscreen render target textures, pipeline layout, and graphics pipeline state object.
/// Destroys all GPU resources upon shutdown() or destruction.
///
/// Thread-safety:
/// Main render thread only. Not re-entrant.
///
/// Performance:
/// Hot-path command recording. Does not allocate heap memory per frame or lock mutexes.

#pragma once

#include "core/math.h"
#include "core/result.h"
#include "render/asset_manager.h"
#include "render/mesh_component.h"
#include "rhi/command_list.h"
#include "rhi/pipeline.h"
#include "rhi/rhi_device.h"
#include "rhi/texture.h"
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
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    SceneRenderer(SceneRenderer&& other) noexcept;
    SceneRenderer& operator=(SceneRenderer&& other) noexcept;

    /// Initializes offscreen render targets, shaders, pipeline layout, and graphics pipeline.
    ///
    /// Preconditions : device must be a valid, initialized RhiDevice; width > 0, height > 0.
    /// Postconditions: color and depth targets allocated; graphics pipeline ready for recording.
    /// Thread-safety : Main thread only.
    ///
    /// @param device The active RHI device for resource allocation.
    /// @param width Render target width in pixels.
    /// @param height Render target height in pixels.
    /// @param colorFormat Texture format for color attachment (default RGBA8_UNORM).
    /// @param depthFormat Texture format for depth attachment (default D32_FLOAT).
    /// @return Result indicating success or resource allocation error.
    [[nodiscard]] Result<void>
    init(rhi::RhiDevice& device, uint32_t width, uint32_t height,
         rhi::TextureFormat colorFormat = rhi::TextureFormat::RGBA8_UNORM,
         rhi::TextureFormat depthFormat = rhi::TextureFormat::D32_FLOAT);

    /// Shuts down all offscreen resources, pipelines, and shaders.
    void shutdown();

    /// Resizes offscreen render targets to match new viewport dimensions.
    /// Synchronizes GPU with waitIdle before destroying previous targets.
    ///
    /// @param width New width in pixels (must be > 0).
    /// @param height New height in pixels (must be > 0).
    /// @return Result indicating success or reallocation error.
    [[nodiscard]] Result<void> resize(uint32_t width, uint32_t height);

    /// Renders all visible mesh entities in the World to the specified command list.
    /// If offscreen render targets are initialized, executes within an offscreen dynamic
    /// rendering pass clearing color and depth.
    ///
    /// @param world The ECS simulation or edit world.
    /// @param assetManager Repository for GPU mesh buffers.
    /// @param device The active RHI device for on-demand GPU uploads.
    /// @param cmdList Target command list for recording vertex/index binds and draw calls.
    /// @param viewProjection Camera view-projection transformation matrix.
    /// @return Render statistics collected for this pass.
    RenderStats renderScene(world::World& world, AssetManager& assetManager, rhi::RhiDevice& device,
                            rhi::RhiCommandList& cmdList,
                            const Mat4& viewProjection = Mat4::identity());

    [[nodiscard]] rhi::TextureHandle getColorTarget() const noexcept { return colorTarget_; }
    [[nodiscard]] rhi::TextureHandle getDepthTarget() const noexcept { return depthTarget_; }
    [[nodiscard]] rhi::PipelineHandle getPipeline() const noexcept { return pipeline_; }
    [[nodiscard]] rhi::PipelineLayoutHandle getPipelineLayout() const noexcept {
        return pipelineLayout_;
    }
    [[nodiscard]] uint32_t getWidth() const noexcept { return width_; }
    [[nodiscard]] uint32_t getHeight() const noexcept { return height_; }
    [[nodiscard]] bool isInitialized() const noexcept {
        return device_ != nullptr && pipeline_.isValid();
    }

private:
    rhi::RhiDevice* device_{nullptr};
    rhi::TextureHandle colorTarget_{};
    rhi::TextureHandle depthTarget_{};
    rhi::ShaderHandle vertexShader_{};
    rhi::ShaderHandle fragmentShader_{};
    rhi::PipelineLayoutHandle pipelineLayout_{};
    rhi::PipelineHandle pipeline_{};
    uint32_t width_{0};
    uint32_t height_{0};
    rhi::TextureFormat colorFormat_{rhi::TextureFormat::RGBA8_UNORM};
    rhi::TextureFormat depthFormat_{rhi::TextureFormat::D32_FLOAT};
};

} // namespace eng::render
