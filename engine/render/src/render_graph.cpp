/// @file render_graph.cpp
/// @brief Render Graph DAG compilation, cycle detection, and execution implementation.

#include "render/render_graph.h"

#include "core/assert.h"

#include <algorithm>
#include <queue>
#include <utility>

namespace eng::render {

// RenderPassBuilder definitions
void RenderPassBuilder::readBuffer(GraphBufferId id, rhi::BufferUsageFlags usage) {
    graph_.addResourceAccess(passIndex_, RenderGraph::ResourceAccess{
                                             .resourceIndex = id.value(),
                                             .isTexture = false,
                                             .accessType = ResourceAccessType::Read,
                                             .usageFlags = static_cast<uint32_t>(usage),
                                         });
}

void RenderPassBuilder::writeBuffer(GraphBufferId id, rhi::BufferUsageFlags usage) {
    graph_.addResourceAccess(passIndex_, RenderGraph::ResourceAccess{
                                             .resourceIndex = id.value(),
                                             .isTexture = false,
                                             .accessType = ResourceAccessType::Write,
                                             .usageFlags = static_cast<uint32_t>(usage),
                                         });
}

void RenderPassBuilder::readTexture(GraphTextureId id, rhi::TextureUsageFlags usage) {
    graph_.addResourceAccess(passIndex_, RenderGraph::ResourceAccess{
                                             .resourceIndex = id.value(),
                                             .isTexture = true,
                                             .accessType = ResourceAccessType::Read,
                                             .usageFlags = static_cast<uint32_t>(usage),
                                         });
}

void RenderPassBuilder::writeTexture(GraphTextureId id, rhi::TextureUsageFlags usage) {
    graph_.addResourceAccess(passIndex_, RenderGraph::ResourceAccess{
                                             .resourceIndex = id.value(),
                                             .isTexture = true,
                                             .accessType = ResourceAccessType::Write,
                                             .usageFlags = static_cast<uint32_t>(usage),
                                         });
}

// RenderGraph implementation
RenderGraph::RenderGraph() = default;
RenderGraph::~RenderGraph() = default;

RenderGraph::RenderGraph(RenderGraph&&) noexcept = default;
RenderGraph& RenderGraph::operator=(RenderGraph&&) noexcept = default;

GraphBufferId RenderGraph::importBuffer(std::string_view name, rhi::BufferHandle handle) {
    const auto id = static_cast<uint32_t>(buffers_.size());
    buffers_.push_back(BufferNode{
        .name = std::string(name),
        .desc = {},
        .handle = handle,
        .isTransient = false,
        .lifetime = {},
        .currentState = rhi::BufferUsageFlags::None,
    });
    return GraphBufferId(id);
}

GraphBufferId RenderGraph::createTransientBuffer(std::string_view name,
                                                 const rhi::BufferDesc& desc) {
    const auto id = static_cast<uint32_t>(buffers_.size());
    buffers_.push_back(BufferNode{
        .name = std::string(name),
        .desc = desc,
        .handle = {},
        .isTransient = true,
        .lifetime = {},
        .currentState = rhi::BufferUsageFlags::None,
    });
    return GraphBufferId(id);
}

GraphTextureId RenderGraph::importTexture(std::string_view name, rhi::TextureHandle handle) {
    const auto id = static_cast<uint32_t>(textures_.size());
    textures_.push_back(TextureNode{
        .name = std::string(name),
        .desc = {},
        .handle = handle,
        .isTransient = false,
        .lifetime = {},
        .currentState = rhi::TextureUsageFlags::None,
    });
    return GraphTextureId(id);
}

GraphTextureId RenderGraph::createTransientTexture(std::string_view name,
                                                   const rhi::TextureDesc& desc) {
    const auto id = static_cast<uint32_t>(textures_.size());
    textures_.push_back(TextureNode{
        .name = std::string(name),
        .desc = desc,
        .handle = {},
        .isTransient = true,
        .lifetime = {},
        .currentState = rhi::TextureUsageFlags::None,
    });
    return GraphTextureId(id);
}

void RenderGraph::addResourceAccess(uint32_t passIndex, ResourceAccess access) {
    ENG_ASSERT(passIndex < passes_.size(), "Invalid pass index for resource access declaration");
    passes_[passIndex].accesses.push_back(access);
}

Result<void> RenderGraph::compile() {
    isCompiled_ = false;
    executionOrder_.clear();

    const size_t passCount = passes_.size();
    if (passCount == 0) {
        isCompiled_ = true;
        return Result<void>::success();
    }

    // 1. Construct pass dependency adjacency matrix
    // adj[u] contains all passes v that depend directly on pass u (edge u -> v)
    std::vector<std::vector<uint32_t>> adj(passCount);
    std::vector<uint32_t> inDegree(passCount, 0);

    auto addEdge = [&](uint32_t u, uint32_t v) {
        if (u == v || u == UINT32_MAX || v == UINT32_MAX) {
            return;
        }
        // Avoid duplicate edges
        if (std::find(adj[u].begin(), adj[u].end(), v) == adj[u].end()) {
            adj[u].push_back(v);
            ++inDegree[v];
        }
    };

    // For each resource, collect writers and readers across all passes
    struct ResourcePassAccess {
        std::vector<uint32_t> writers;
        std::vector<uint32_t> readers;
    };

    std::vector<ResourcePassAccess> bufferAccesses(buffers_.size());
    std::vector<ResourcePassAccess> textureAccesses(textures_.size());

    for (uint32_t passIdx = 0; passIdx < passCount; ++passIdx) {
        const auto& pass = passes_[passIdx];
        for (const auto& access : pass.accesses) {
            auto& resAccess = access.isTexture ? textureAccesses[access.resourceIndex]
                                               : bufferAccesses[access.resourceIndex];
            if (access.accessType == ResourceAccessType::Read) {
                resAccess.readers.push_back(passIdx);
            } else {
                resAccess.writers.push_back(passIdx);
            }
        }
    }

    // Build edges: Readers depend on writers (Writer -> Reader)
    auto buildResourceEdges = [&](const std::vector<ResourcePassAccess>& resList) {
        for (const auto& res : resList) {
            if (res.writers.empty()) {
                continue;
            }

            if (res.writers.size() == 1) {
                const uint32_t writer = res.writers[0];
                for (uint32_t reader : res.readers) {
                    addEdge(writer, reader);
                }
            } else {
                // Multiple writers: sequence writers in declaration order
                for (size_t i = 0; i < res.writers.size() - 1; ++i) {
                    addEdge(res.writers[i], res.writers[i + 1]);
                }
                // Readers depend on the closest preceding writer
                for (uint32_t reader : res.readers) {
                    uint32_t precedingWriter = res.writers[0];
                    for (uint32_t writer : res.writers) {
                        if (writer < reader) {
                            precedingWriter = writer;
                        }
                    }
                    addEdge(precedingWriter, reader);
                }
            }
        }
    };

    buildResourceEdges(bufferAccesses);
    buildResourceEdges(textureAccesses);

    // 2. Cycle Detection via DFS
    // Colors: 0 = Unvisited, 1 = Visiting (in recursion stack), 2 = Visited
    std::vector<uint8_t> visitState(passCount, 0);
    auto dfsCycle = [&](auto& self, uint32_t u) -> bool {
        visitState[u] = 1;
        for (uint32_t v : adj[u]) {
            if (visitState[v] == 1) {
                return true; // Back-edge detected -> cycle!
            }
            if (visitState[v] == 0 && self(self, v)) {
                return true;
            }
        }
        visitState[u] = 2;
        return false;
    };

    for (uint32_t i = 0; i < passCount; ++i) {
        if (visitState[i] == 0) {
            if (dfsCycle(dfsCycle, i)) {
                return Error{.code = ErrorCode::RenderGraphCompilationFailed,
                             .message = "Cycle detected in render graph pass dependencies"};
            }
        }
    }

    // 3. Topological Sort (Kahn's Algorithm)
    std::queue<uint32_t> zeroInDegreeQueue;
    for (uint32_t i = 0; i < passCount; ++i) {
        if (inDegree[i] == 0) {
            zeroInDegreeQueue.push(i);
        }
    }

    executionOrder_.reserve(passCount);
    while (!zeroInDegreeQueue.empty()) {
        uint32_t u = zeroInDegreeQueue.front();
        zeroInDegreeQueue.pop();
        executionOrder_.push_back(u);

        for (uint32_t v : adj[u]) {
            --inDegree[v];
            if (inDegree[v] == 0) {
                zeroInDegreeQueue.push(v);
            }
        }
    }

    if (executionOrder_.size() != passCount) {
        return Error{.code = ErrorCode::RenderGraphCompilationFailed,
                     .message = "Failed to sort render graph passes topologically"};
    }

    // 4. Resource Lifetime Analysis
    for (auto& buf : buffers_) {
        buf.lifetime = ResourceLifetime{};
    }
    for (auto& tex : textures_) {
        tex.lifetime = ResourceLifetime{};
    }

    for (uint32_t step = 0; step < passCount; ++step) {
        const uint32_t passIdx = executionOrder_[step];
        const auto& pass = passes_[passIdx];

        for (const auto& access : pass.accesses) {
            ResourceLifetime& lifetime = access.isTexture ? textures_[access.resourceIndex].lifetime
                                                          : buffers_[access.resourceIndex].lifetime;

            if (lifetime.firstPass == UINT32_MAX) {
                lifetime.firstPass = step;
            }
            lifetime.lastPass = step;
        }
    }

    // 5. State Transition & Barrier Planning
    for (uint32_t passIdx : executionOrder_) {
        auto& pass = passes_[passIdx];
        pass.textureTransitions.clear();
        pass.bufferTransitions.clear();

        for (const auto& access : pass.accesses) {
            if (access.isTexture) {
                auto& tex = textures_[access.resourceIndex];
                const auto targetUsage = static_cast<rhi::TextureUsageFlags>(access.usageFlags);
                if (tex.currentState != targetUsage) {
                    pass.textureTransitions.push_back(TextureTransition{
                        .textureId = GraphTextureId(access.resourceIndex),
                        .handle = tex.handle,
                        .beforeState = tex.currentState,
                        .afterState = targetUsage,
                    });
                    tex.currentState = targetUsage;
                }
            } else {
                auto& buf = buffers_[access.resourceIndex];
                const auto targetUsage = static_cast<rhi::BufferUsageFlags>(access.usageFlags);
                if (buf.currentState != targetUsage) {
                    pass.bufferTransitions.push_back(BufferTransition{
                        .bufferId = GraphBufferId(access.resourceIndex),
                        .handle = buf.handle,
                        .beforeState = buf.currentState,
                        .afterState = targetUsage,
                    });
                    buf.currentState = targetUsage;
                }
            }
        }
    }

    isCompiled_ = true;
    return Result<void>::success();
}

Result<void> RenderGraph::execute(rhi::RhiDevice& device, rhi::RhiCommandList& cmdList) {
    if (!isCompiled_) {
        return Error{.code = ErrorCode::InvalidState,
                     .message = "Cannot execute uncompiled render graph"};
    }

    // 1. Allocate transient resources
    for (auto& buf : buffers_) {
        if (buf.isTransient && !buf.handle.isValid()) {
            auto createRes = device.createBuffer(buf.desc);
            if (!createRes.hasValue()) {
                return createRes.error();
            }
            buf.handle = createRes.value();
        }
    }

    for (auto& tex : textures_) {
        if (tex.isTransient && !tex.handle.isValid()) {
            auto createRes = device.createTexture(tex.desc);
            if (!createRes.hasValue()) {
                return createRes.error();
            }
            tex.handle = createRes.value();
        }
    }

    // Update transition handles with resolved transient handles
    for (uint32_t passIdx : executionOrder_) {
        auto& pass = passes_[passIdx];
        for (auto& trans : pass.textureTransitions) {
            trans.handle = textures_[trans.textureId.value()].handle;
        }
        for (auto& trans : pass.bufferTransitions) {
            trans.handle = buffers_[trans.bufferId.value()].handle;
        }
    }

    // 2. Execute passes in topological order
    for (uint32_t passIdx : executionOrder_) {
        const auto& pass = passes_[passIdx];
        if (pass.executor != nullptr) {
            pass.executor->execute(cmdList);
        }
    }

    // 3. Clean up transient resources
    for (auto& buf : buffers_) {
        if (buf.isTransient && buf.handle.isValid()) {
            device.destroyBuffer(buf.handle);
            buf.handle = {};
        }
    }

    for (auto& tex : textures_) {
        if (tex.isTransient && tex.handle.isValid()) {
            device.destroyTexture(tex.handle);
            tex.handle = {};
        }
    }

    return Result<void>::success();
}

void RenderGraph::reset() {
    buffers_.clear();
    textures_.clear();
    passes_.clear();
    executionOrder_.clear();
    isCompiled_ = false;
}

rhi::BufferHandle RenderGraph::getBufferHandle(GraphBufferId id) const noexcept {
    if (id.value() < buffers_.size()) {
        return buffers_[id.value()].handle;
    }
    return {};
}

rhi::TextureHandle RenderGraph::getTextureHandle(GraphTextureId id) const noexcept {
    if (id.value() < textures_.size()) {
        return textures_[id.value()].handle;
    }
    return {};
}

ResourceLifetime RenderGraph::getTextureLifetime(GraphTextureId id) const noexcept {
    if (id.value() < textures_.size()) {
        return textures_[id.value()].lifetime;
    }
    return {};
}

ResourceLifetime RenderGraph::getBufferLifetime(GraphBufferId id) const noexcept {
    if (id.value() < buffers_.size()) {
        return buffers_[id.value()].lifetime;
    }
    return {};
}

} // namespace eng::render
