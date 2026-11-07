/// @file render_graph.h
/// @brief Declarative, backend-independent Render Graph DAG scheduler.
///
/// Purpose:
/// Coordinates multi-pass graphics rendering via an acyclic dependency graph. Automatically
/// resolves pass dependencies, topological execution ordering, cycle detection, resource
/// lifetimes, and automatic pipeline barrier transitions.
///
/// Ownership and Lifetime:
/// Owned by the application or high-level renderer. Outlives per-frame execution iterations.
///
/// Thread-safety:
/// Graph declaration and compilation occur on a single thread (render thread / job).
///
/// Performance:
/// Pre-allocated vectors. Zero heap allocation during execution after steady-state capacity is
/// reached.

#pragma once

#include "core/result.h"
#include "render/render_graph_types.h"
#include "rhi/command_list.h"
#include "rhi/rhi_device.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eng::render {

class RenderGraph;

/// @class RenderPassBuilder
/// @brief Interface provided to pass setup callbacks to declare resource read/write access.
class RenderPassBuilder {
public:
    explicit RenderPassBuilder(RenderGraph& graph, uint32_t passIndex) noexcept
        : graph_(graph), passIndex_(passIndex) {}

    /// Declares read access on a graph buffer resource.
    void readBuffer(GraphBufferId id, rhi::BufferUsageFlags usage);

    /// Declares write access on a graph buffer resource.
    void writeBuffer(GraphBufferId id, rhi::BufferUsageFlags usage);

    /// Declares read access on a graph texture resource.
    void readTexture(GraphTextureId id, rhi::TextureUsageFlags usage);

    /// Declares write access on a graph texture resource.
    void writeTexture(GraphTextureId id, rhi::TextureUsageFlags usage);

private:
    RenderGraph& graph_;
    uint32_t passIndex_{0};
};

/// @struct PassExecutor
/// @brief Internal abstract interface for polymorphic pass command recording.
struct PassExecutor {
    virtual ~PassExecutor() = default;
    virtual void execute(rhi::RhiCommandList& cmd) = 0;
};

/// @class RenderGraph
/// @brief DAG-based render pass and resource dependency manager.
class RenderGraph {
public:
    struct BufferNode {
        std::string name;
        rhi::BufferDesc desc{};
        rhi::BufferHandle handle;
        bool isTransient{false};
        ResourceLifetime lifetime{};
        rhi::BufferUsageFlags currentState{rhi::BufferUsageFlags::None};
    };

    struct TextureNode {
        std::string name;
        rhi::TextureDesc desc{};
        rhi::TextureHandle handle;
        bool isTransient{false};
        ResourceLifetime lifetime{};
        rhi::TextureUsageFlags currentState{rhi::TextureUsageFlags::None};
    };

    struct ResourceAccess {
        uint32_t resourceIndex{0};
        bool isTexture{false};
        ResourceAccessType accessType{ResourceAccessType::Read};
        uint32_t usageFlags{0};
    };

    struct PassNode {
        std::string name;
        uint32_t index{0};
        std::vector<ResourceAccess> accesses;
        std::unique_ptr<PassExecutor> executor;
        std::vector<TextureTransition> textureTransitions;
        std::vector<BufferTransition> bufferTransitions;
    };

    RenderGraph();
    ~RenderGraph();

    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;
    RenderGraph(RenderGraph&&) noexcept;
    RenderGraph& operator=(RenderGraph&&) noexcept;

    /// Registers an externally allocated GPU buffer with the graph.
    [[nodiscard]] GraphBufferId importBuffer(std::string_view name, rhi::BufferHandle handle);

    /// Declares a transient buffer to be created and managed by the graph.
    [[nodiscard]] GraphBufferId createTransientBuffer(std::string_view name,
                                                      const rhi::BufferDesc& desc);

    /// Registers an externally allocated GPU texture with the graph.
    [[nodiscard]] GraphTextureId importTexture(std::string_view name, rhi::TextureHandle handle);

    /// Declares a transient texture to be created and managed by the graph.
    [[nodiscard]] GraphTextureId createTransientTexture(std::string_view name,
                                                        const rhi::TextureDesc& desc);

    /// Adds a render pass to the graph with explicit setup and execution callbacks.
    template <typename SetupFn, typename ExecFn>
    void addPass(std::string_view name, SetupFn&& setup, ExecFn&& exec) {
        const auto passIdx = static_cast<uint32_t>(passes_.size());

        struct ConcreteExecutor final : public PassExecutor {
            std::decay_t<ExecFn> callable;
            explicit ConcreteExecutor(ExecFn&& fn) : callable(std::forward<ExecFn>(fn)) {}
            void execute(rhi::RhiCommandList& cmd) override { callable(cmd); }
        };

        auto executor = std::make_unique<ConcreteExecutor>(std::forward<ExecFn>(exec));
        passes_.push_back(PassNode{
            .name = std::string(name),
            .index = passIdx,
            .accesses = {},
            .executor = std::move(executor),
            .textureTransitions = {},
            .bufferTransitions = {},
        });

        RenderPassBuilder builder(*this, passIdx);
        setup(builder);
    }

    /// Compiles graph dependencies, checks for cycles, and calculates topological ordering.
    [[nodiscard]] Result<void> compile();

    /// Executes compiled passes in topological order using the provided RHI device and command
    /// list.
    [[nodiscard]] Result<void> execute(rhi::RhiDevice& device, rhi::RhiCommandList& cmdList);

    /// Resets the graph structure for the next frame while retaining memory capacities.
    void reset();

    /// Resolves actual RHI BufferHandle for a graph buffer ID.
    [[nodiscard]] rhi::BufferHandle getBufferHandle(GraphBufferId id) const noexcept;

    /// Resolves actual RHI TextureHandle for a graph texture ID.
    [[nodiscard]] rhi::TextureHandle getTextureHandle(GraphTextureId id) const noexcept;

    /// Returns whether the graph has been successfully compiled.
    [[nodiscard]] bool isCompiled() const noexcept { return isCompiled_; }

    /// Returns the number of declared passes.
    [[nodiscard]] size_t getPassCount() const noexcept { return passes_.size(); }

    /// Returns list of pass indices in topological execution order.
    [[nodiscard]] const std::vector<uint32_t>& getExecutionOrder() const noexcept {
        return executionOrder_;
    }

    /// Returns lifetime metadata for a texture.
    [[nodiscard]] ResourceLifetime getTextureLifetime(GraphTextureId id) const noexcept;

    /// Returns lifetime metadata for a buffer.
    [[nodiscard]] ResourceLifetime getBufferLifetime(GraphBufferId id) const noexcept;

private:
    friend class RenderPassBuilder;
    void addResourceAccess(uint32_t passIndex, ResourceAccess access);

    std::vector<BufferNode> buffers_;
    std::vector<TextureNode> textures_;
    std::vector<PassNode> passes_;
    std::vector<uint32_t> executionOrder_;
    bool isCompiled_{false};
};

} // namespace eng::render
