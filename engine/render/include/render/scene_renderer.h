/// @file scene_renderer.h
/// @brief Scene mesh rendering system and RHI command recorder.
///
/// Purpose:
/// Traverses ECS entities with MeshComponent, resolves their GPU mesh buffers via AssetManager,
/// and records draw commands into an RhiCommandList. Transform priority per entity:
///   1. WorldTransformComponent::worldMatrix — set by HierarchyTransformSystem for parented
///   entities.
///   2. TransformComponent — local transform fallback.
///   3. PhysicsTransformComponent — fallback for dynamic rigid bodies at root level.
///   4. Identity matrix — stationary at origin if none of the above components are present.
/// Manages offscreen color and depth render targets, shaders, graphics pipeline, HDR target,
/// and post-processing pipeline (bloom pyramid downsample/upsample and tone mapping).
///
/// Ownership and Lifetime:
/// Owns offscreen render target textures, pipeline layouts, descriptor sets, and pipeline state
/// objects. Destroys all GPU resources upon shutdown() or destruction.
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
#include "render/graphics_settings.h"
#include "render/light_components.h"
#include "render/lighting_data.h"
#include "render/mesh_component.h"
#include "render/post_process_types.h"
#include "render/render_graph.h"
#include "rhi/command_list.h"
#include "rhi/pipeline.h"
#include "rhi/rhi_device.h"
#include "rhi/sampler.h"
#include "rhi/texture.h"
#include "world/world.h"

#include <array>
#include <cstdint>
#include <vector>

namespace eng::render {

/// @struct RenderStats
/// @brief Telemetry metrics collected during scene rendering.
struct RenderStats {
    uint32_t drawCalls{0};             ///< Main forward PBR draw calls
    uint32_t shadowDrawCalls{0};       ///< Directional shadow depth draw calls
    uint32_t depthPrepassDrawCalls{0}; ///< Early depth prepass draw calls
    uint32_t triangles{0};             ///< Triangles rasterized in main PBR pass
    uint32_t shadowTriangles{0};       ///< Triangles rasterized in shadow pass
    uint32_t vertices{0};              ///< Vertices evaluated in main PBR pass
    uint32_t meshesRendered{0};        ///< Mesh entities evaluated and rendered
    uint32_t culledPrimitives{0};      ///< Mesh primitives culled or skipped
};

/// @struct GpuPassTimings
/// @brief Hardware GPU execution duration for individual scene rendering and post-processing
/// passes.
struct GpuPassTimings {
    /// Total duration spanning scene rendering from shadow pass through tone mapping
    /// (milliseconds).
    float totalSceneMs{0.0F};
    /// Duration of directional shadow map generation (milliseconds).
    float shadowPassMs{0.0F};
    /// Duration of early depth prepass (milliseconds).
    float depthPrepassMs{0.0F};
    /// Duration of forward PBR mesh rasterization and material shading (milliseconds).
    float mainPbrPassMs{0.0F};
    /// Duration of bloom downsampling pyramid passes (milliseconds).
    float bloomDownsampleMs{0.0F};
    /// Duration of bloom upsampling and accumulation passes (milliseconds).
    float bloomUpsampleMs{0.0F};
    /// Duration of tone mapping and presentation conversion pass (milliseconds).
    float toneMappingMs{0.0F};
    /// Whether these timings reflect valid GPU query results from the preceding frame.
    bool valid{false};
};

/// @class SceneRenderer
/// @brief Traverses the ECS World and records mesh draw calls to the RHI command list.
class SceneRenderer {
public:
    static constexpr size_t kBloomPyramidLevels = 4;

    SceneRenderer() = default;
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    SceneRenderer(SceneRenderer&& other) noexcept;
    SceneRenderer& operator=(SceneRenderer&& other) noexcept;

    /// Initializes offscreen render targets, shaders, pipeline layout, and graphics pipeline.
    ///
    /// Preconditions : device must be a valid, initialized RhiDevice; width > 0, height > 0.
    /// Postconditions: color, depth, and HDR targets allocated; graphics pipelines ready for
    /// recording. Thread-safety : Main thread only.
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

    /// Resizes offscreen render targets and bloom pyramid to match new viewport dimensions.
    /// Synchronizes GPU with waitIdle before destroying previous targets.
    ///
    /// @param width New width in pixels (must be > 0).
    /// @param height New height in pixels (must be > 0).
    /// @return Result indicating success or reallocation error.
    [[nodiscard]] Result<void> resize(uint32_t width, uint32_t height);

    /// Renders all visible mesh entities in the World to the specified command list.
    /// If post-processing is enabled, renders into the floating-point HDR scene target.
    /// If post-processing is disabled, renders directly into the LDR color target.
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

    /// Executes post-processing passes (bloom pyramid downsample/upsample and tone mapping)
    /// transitioning radiance from the HDR scene target into the display color target.
    ///
    /// Preconditions : cmdList must be recording outside an active render pass.
    /// Thread-safety : Main thread only.
    /// Cost          : Zero heap allocation in hot path.
    ///
    /// @param cmdList Command list recording post-processing passes.
    void renderPostProcess(rhi::RhiCommandList& cmdList);

    /// Asynchronously polls and updates GPU pass duration telemetry from the preceding frame.
    ///
    /// Cost : Zero heap allocation, non-blocking check. Allowed in hot path.
    ///
    /// @param device The active RHI device owning query pool resources.
    void updateGpuTimings(rhi::RhiDevice& device);

    /// Returns the telemetry metrics collected during the most recent scene rendering pass.
    [[nodiscard]] const RenderStats& getLastRenderStats() const noexcept {
        return lastRenderStats_;
    }

    /// Returns the latest measured GPU execution durations for all rendering passes.
    [[nodiscard]] const GpuPassTimings& getGpuPassTimings() const noexcept {
        return gpuPassTimings_;
    }

    [[nodiscard]] rhi::TextureHandle getColorTarget() const noexcept { return colorTarget_; }
    [[nodiscard]] rhi::TextureHandle getDepthTarget() const noexcept { return depthTarget_; }
    [[nodiscard]] rhi::TextureHandle getHdrTarget() const noexcept { return hdrTarget_; }
    [[nodiscard]] rhi::TextureFormat getHdrFormat() const noexcept { return hdrFormat_; }
    /// Returns true if genuine floating-point HDR rendering is active (RGBA16_FLOAT or
    /// RGBA32_FLOAT). Returns false if post-processing is disabled or operating in SDR fallback
    /// mode (RGBA8_UNORM).
    [[nodiscard]] bool isHdrActive() const noexcept {
        return postProcessSettings_.enabled && hdrTarget_.isValid() &&
               (hdrFormat_ == rhi::TextureFormat::RGBA16_FLOAT ||
                hdrFormat_ == rhi::TextureFormat::RGBA32_FLOAT);
    }

    /// Returns true if post-processing is enabled but falling back to an SDR format (e.g.
    /// RGBA8_UNORM).
    [[nodiscard]] bool isSdrFallbackActive() const noexcept {
        return postProcessSettings_.enabled && !isHdrActive();
    }

    /// Returns the central graphics and scalability configuration.
    [[nodiscard]] const GraphicsSettings& getGraphicsSettings() const noexcept { return settings_; }

    /// Validates and applies a modified graphics configuration across renderer subsystems.
    [[nodiscard]] Result<void> applyGraphicsSettings(const GraphicsSettings& settings);

    [[nodiscard]] const PostProcessSettings& getPostProcessSettings() const noexcept {
        return postProcessSettings_;
    }
    void setPostProcessSettings(const PostProcessSettings& settings) noexcept {
        postProcessSettings_ = settings;
        buildPostProcessGraph();
        buildRenderGraph();
    }
    [[nodiscard]] bool isPostProcessingEnabled() const noexcept {
        return postProcessSettings_.enabled;
    }
    void setPostProcessingEnabled(bool enabled) noexcept {
        postProcessSettings_.enabled = enabled;
        buildPostProcessGraph();
        buildRenderGraph();
    }

    [[nodiscard]] rhi::PipelineHandle getPipeline() const noexcept { return pipeline_; }
    [[nodiscard]] rhi::PipelineLayoutHandle getPipelineLayout() const noexcept {
        return pipelineLayout_;
    }
    /// Returns the cached descriptor set layout for glTF materials (Set 0).
    ///
    /// Preconditions : SceneRenderer must be initialized.
    /// Thread-safety : Safe to call concurrently from any thread after init().
    /// Cost          : Free (no heap allocation, returns value handle).
    [[nodiscard]] rhi::DescriptorSetLayoutHandle getMaterialDescriptorSetLayout() const noexcept {
        return materialDescriptorSetLayout_;
    }
    [[nodiscard]] rhi::DescriptorSetLayoutHandle getSceneDescriptorSetLayout() const noexcept {
        return sceneDescriptorSetLayout_;
    }
    [[nodiscard]] rhi::TextureHandle getShadowDepthTarget() const noexcept {
        return shadowDepthTarget_;
    }
    [[nodiscard]] rhi::SamplerHandle getShadowSampler() const noexcept { return shadowSampler_; }
    [[nodiscard]] rhi::PipelineHandle getShadowPipeline() const noexcept { return shadowPipeline_; }
    [[nodiscard]] rhi::BufferHandle getLightingUniformBuffer() const noexcept {
        return lightingUniformBuffer_;
    }
    [[nodiscard]] uint32_t getShadowResolution() const noexcept { return shadowResolution_; }
    void setShadowResolution(uint32_t resolution) noexcept {
        shadowResolution_ = resolution;
        shadowDirty_ = true;
    }
    void markShadowDirty() noexcept { shadowDirty_ = true; }
    [[nodiscard]] bool isShadowDirty() const noexcept { return shadowDirty_; }
    [[nodiscard]] uint32_t getWidth() const noexcept { return width_; }
    [[nodiscard]] uint32_t getHeight() const noexcept { return height_; }
    [[nodiscard]] bool isInitialized() const noexcept {
        return device_ != nullptr && pipeline_.isValid();
    }

    /// Returns the live scene DAG render graph.
    [[nodiscard]] const RenderGraph& getRenderGraph() const noexcept { return renderGraph_; }

    /// Returns the live post-processing DAG render graph.
    [[nodiscard]] const RenderGraph& getPostProcessRenderGraph() const noexcept {
        return postProcessGraph_;
    }

private:
    [[nodiscard]] Result<void> initPostProcessResources();
    [[nodiscard]] Result<void> createPostProcessTargets(uint32_t width, uint32_t height);
    void destroyPostProcessTargets();
    void updatePostProcessDescriptors();

    void buildRenderGraph();
    void buildPostProcessGraph();
    void recordShadowPass(rhi::RhiCommandList& cmdList);
    void recordDepthPrepass(rhi::RhiCommandList& cmdList);
    void recordMainPbrPass(rhi::RhiCommandList& cmdList);
    void recordBloomDownsamplePass(rhi::RhiCommandList& cmdList, size_t level);
    void recordBloomUpsamplePass(rhi::RhiCommandList& cmdList, size_t step, size_t targetLevel);
    void recordToneMappingPass(rhi::RhiCommandList& cmdList);

    rhi::RhiDevice* device_{nullptr};
    rhi::TextureHandle colorTarget_{};
    rhi::TextureHandle depthTarget_{};
    rhi::ShaderHandle vertexShader_{};
    rhi::ShaderHandle fragmentShader_{};
    rhi::ShaderHandle fragmentShaderOpaque_{};
    rhi::DescriptorSetLayoutHandle materialDescriptorSetLayout_{};
    rhi::DescriptorSetLayoutHandle sceneDescriptorSetLayout_{};
    rhi::DescriptorPoolHandle sceneDescriptorPool_{};
    rhi::DescriptorSetHandle sceneDescriptorSet_{};
    rhi::BufferHandle lightingUniformBuffer_{};
    rhi::TextureHandle shadowDepthTarget_{};
    rhi::SamplerHandle shadowSampler_{};
    rhi::ShaderHandle shadowVertexShader_{};
    rhi::PipelineLayoutHandle shadowPipelineLayout_{};
    rhi::PipelineHandle shadowPipeline_{};
    rhi::PipelineHandle depthPrepassPipeline_{};
    rhi::PipelineLayoutHandle pipelineLayout_{};
    rhi::PipelineHandle pipeline_{};
    rhi::PipelineHandle pipelineOpaque_{};
    rhi::PipelineHandle pipelineDepthPrepass_{};
    rhi::PipelineHandle pipelineDepthPrepassOpaque_{};
    rhi::PipelineHandle hdrPipelineDepthPrepass_{};
    rhi::PipelineHandle hdrPipelineDepthPrepassOpaque_{};

    // HDR and Post-Processing Resources
    rhi::TextureHandle hdrTarget_{};
    rhi::TextureFormat hdrFormat_{rhi::TextureFormat::RGBA16_FLOAT};
    PostProcessSettings postProcessSettings_{};
    rhi::TextureHandle bloomDownTarget_[kBloomPyramidLevels]{};
    rhi::TextureHandle bloomUpTarget_[kBloomPyramidLevels - 1]{};
    rhi::SamplerHandle postProcessSampler_{};
    rhi::ShaderHandle fullscreenVertexShader_{};
    rhi::ShaderHandle bloomDownFragmentShader_{};
    rhi::ShaderHandle bloomUpFragmentShader_{};
    rhi::ShaderHandle toneMappingFragmentShader_{};
    rhi::DescriptorSetLayoutHandle singleTextureSetLayout_{};
    rhi::DescriptorSetLayoutHandle dualTextureSetLayout_{};
    rhi::DescriptorPoolHandle postProcessDescriptorPool_{};
    rhi::DescriptorSetHandle bloomDownSets_[kBloomPyramidLevels]{};
    rhi::DescriptorSetHandle bloomUpSets_[kBloomPyramidLevels - 1]{};
    rhi::DescriptorSetHandle toneMappingSet_{};
    rhi::PipelineLayoutHandle bloomDownPipelineLayout_{};
    rhi::PipelineLayoutHandle bloomUpPipelineLayout_{};
    rhi::PipelineLayoutHandle toneMappingPipelineLayout_{};
    rhi::PipelineHandle bloomDownPipeline_{};
    rhi::PipelineHandle bloomUpPipeline_{};
    rhi::PipelineHandle toneMappingPipeline_{};
    rhi::PipelineHandle hdrPipeline_{};
    rhi::PipelineHandle hdrPipelineOpaque_{};

    uint32_t width_{0};
    uint32_t height_{0};
    uint32_t shadowResolution_{2048};
    rhi::TextureFormat colorFormat_{rhi::TextureFormat::RGBA8_UNORM};
    rhi::TextureFormat depthFormat_{rhi::TextureFormat::D32_FLOAT};
    GraphicsSettings settings_{};
    rhi::PipelineHandle wireframePipeline_{};
    rhi::PipelineHandle wireframeLdrPipeline_{};
    RenderStats lastRenderStats_{};
    GpuPassTimings gpuPassTimings_{};
    uint32_t frameSlotIndex_{0};

    // Live RenderGraph DAGs and per-frame recording state
    RenderGraph renderGraph_{};
    RenderGraph postProcessGraph_{};
    rhi::RhiDevice* currentDevice_{nullptr};
    world::World* currentWorld_{nullptr};
    AssetManager* currentAssetManager_{nullptr};
    Mat4 currentViewProj_{Mat4::identity()};
    Vec3 currentCameraPos_{0.0F, 0.0F, 0.0F};
    Mat4 currentLightSpaceMatrix_{Mat4::identity()};
    Vec3 currentLightDir_{Vec3::unitY()};
    uint32_t currentBaseQuery_{0};
    bool currentHasTimestamps_{false};

    struct VisibleMeshItem {
        GpuMesh* gpuMesh{nullptr};
        rhi::DescriptorSetHandle materialSet{};
        Mat4 model{Mat4::identity()};
        uint32_t submeshIndex{0};
        float depthSq{0.0F};
        bool isAlphaMasked{false};
    };

    struct ShadowCasterItem {
        GpuMesh* gpuMesh{nullptr};
        Mat4 model{Mat4::identity()};
        uint32_t submeshIndex{0};
        float distanceToLight{0.0F};
    };

    std::vector<VisibleMeshItem> visibleItems_{};
    std::vector<ShadowCasterItem> shadowCasters_{};
    bool shadowDirty_{true};
    Vec3 lastLightDir_{Vec3::zero()};
    Mat4 lastLightSpaceMatrix_{Mat4::identity()};
};

} // namespace eng::render
