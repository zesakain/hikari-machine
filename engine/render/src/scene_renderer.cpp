/// @file scene_renderer.cpp
/// @brief Scene mesh rendering traversal and command recording implementation.

#include "render/scene_renderer.h"

#include "core/assert.h"
#include "physics/physics_components.h"
#include "render/gpu_material.h"
#include "render/mesh_shader_spirv.h"
#include "world/components.h"

#include <cstring>

namespace eng::render {

SceneRenderer::~SceneRenderer() {
    shutdown();
}

SceneRenderer::SceneRenderer(SceneRenderer&& other) noexcept
    : device_(other.device_), colorTarget_(other.colorTarget_), depthTarget_(other.depthTarget_),
      vertexShader_(other.vertexShader_), fragmentShader_(other.fragmentShader_),
      materialDescriptorSetLayout_(other.materialDescriptorSetLayout_),
      pipelineLayout_(other.pipelineLayout_), pipeline_(other.pipeline_), width_(other.width_),
      height_(other.height_), colorFormat_(other.colorFormat_), depthFormat_(other.depthFormat_) {
    other.device_ = nullptr;
    other.colorTarget_ = {};
    other.depthTarget_ = {};
    other.vertexShader_ = {};
    other.fragmentShader_ = {};
    other.materialDescriptorSetLayout_ = {};
    other.pipelineLayout_ = {};
    other.pipeline_ = {};
    other.width_ = 0;
    other.height_ = 0;
}

SceneRenderer& SceneRenderer::operator=(SceneRenderer&& other) noexcept {
    if (this != &other) {
        shutdown();
        device_ = other.device_;
        colorTarget_ = other.colorTarget_;
        depthTarget_ = other.depthTarget_;
        vertexShader_ = other.vertexShader_;
        fragmentShader_ = other.fragmentShader_;
        materialDescriptorSetLayout_ = other.materialDescriptorSetLayout_;
        pipelineLayout_ = other.pipelineLayout_;
        pipeline_ = other.pipeline_;
        width_ = other.width_;
        height_ = other.height_;
        colorFormat_ = other.colorFormat_;
        depthFormat_ = other.depthFormat_;

        other.device_ = nullptr;
        other.colorTarget_ = {};
        other.depthTarget_ = {};
        other.vertexShader_ = {};
        other.fragmentShader_ = {};
        other.materialDescriptorSetLayout_ = {};
        other.pipelineLayout_ = {};
        other.pipeline_ = {};
        other.width_ = 0;
        other.height_ = 0;
    }
    return *this;
}

Result<void> SceneRenderer::init(rhi::RhiDevice& device, uint32_t width, uint32_t height,
                                 rhi::TextureFormat colorFormat, rhi::TextureFormat depthFormat) {
    if (width == 0 || height == 0) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "SceneRenderer width and height must be non-zero"};
    }

    shutdown();

    device_ = &device;
    width_ = width;
    height_ = height;
    colorFormat_ = colorFormat;
    depthFormat_ = depthFormat;

    // 1. Allocate offscreen color attachment texture
    rhi::TextureDesc colorDesc{
        .type = rhi::TextureType::Texture2D,
        .format = colorFormat_,
        .width = width_,
        .height = height_,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = rhi::TextureUsageFlags::ColorAttachment | rhi::TextureUsageFlags::Sampled |
                 rhi::TextureUsageFlags::TransferSrc,
        .debugName = "SceneRendererColorTarget",
    };
    auto colorRes = device_->createTexture(colorDesc);
    if (!colorRes.hasValue()) {
        shutdown();
        return colorRes.error();
    }
    colorTarget_ = colorRes.value();

    // 2. Allocate offscreen depth attachment texture
    rhi::TextureDesc depthDesc{
        .type = rhi::TextureType::Texture2D,
        .format = depthFormat_,
        .width = width_,
        .height = height_,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = rhi::TextureUsageFlags::DepthStencilAttachment,
        .debugName = "SceneRendererDepthTarget",
    };
    auto depthRes = device_->createTexture(depthDesc);
    if (!depthRes.hasValue()) {
        shutdown();
        return depthRes.error();
    }
    depthTarget_ = depthRes.value();

    // 3. Create material descriptor set layout (Set 0)
    auto matLayoutDesc = createMaterialDescriptorSetLayoutDesc();
    auto matLayoutRes = device_->createDescriptorSetLayout(matLayoutDesc);
    if (!matLayoutRes.hasValue()) {
        shutdown();
        return matLayoutRes.error();
    }
    materialDescriptorSetLayout_ = matLayoutRes.value();

    // 4. Compile vertex and fragment shaders from precompiled SPIR-V bytecodes
    rhi::ShaderDesc vertDesc{
        .stage = rhi::ShaderStageFlags::Vertex,
        .spirvCode = kPbrMeshVertexShaderSpirv,
        .entryPoint = "main",
        .debugName = "PbrMeshVertexShader",
    };
    auto vertRes = device_->createShader(vertDesc);
    if (!vertRes.hasValue()) {
        shutdown();
        return vertRes.error();
    }
    vertexShader_ = vertRes.value();

    rhi::ShaderDesc fragDesc{
        .stage = rhi::ShaderStageFlags::Fragment,
        .spirvCode = kGltfMaterialFragmentShaderSpirv,
        .entryPoint = "main",
        .debugName = "PbrMaterialFragmentShader",
    };
    auto fragRes = device_->createShader(fragDesc);
    if (!fragRes.hasValue()) {
        shutdown();
        return fragRes.error();
    }
    fragmentShader_ = fragRes.value();

    // 5. Create pipeline layout specifying 128 bytes of push constants for vertex stage
    rhi::PipelineLayoutDesc layoutDesc{
        .pushConstants = {{
            .stageFlags = rhi::ShaderStageFlags::Vertex,
            .offset = 0,
            .size = 128,
        }},
        .descriptorSetLayoutHandles = {materialDescriptorSetLayout_},
        .descriptorSetLayouts = {},
        .debugName = "MeshPipelineLayout",
    };
    auto layoutRes = device_->createPipelineLayout(layoutDesc);
    if (!layoutRes.hasValue()) {
        shutdown();
        return layoutRes.error();
    }
    pipelineLayout_ = layoutRes.value();

    // 5. Build Graphics Pipeline for offscreen dynamic rendering
    rhi::GraphicsPipelineDesc pipeDesc{
        .vertexShader = vertexShader_,
        .fragmentShader = fragmentShader_,
        .vertexInput =
            {
                .bindings = {{
                    .binding = 0,
                    .stride = sizeof(Vertex),
                    .inputRate = rhi::VertexInputRate::Vertex,
                }},
                .attributes =
                    {
                        {
                            .location = 0,
                            .binding = 0,
                            .format = rhi::VertexFormat::Float3,
                            .offset = static_cast<uint32_t>(offsetof(Vertex, position)),
                        },
                        {
                            .location = 1,
                            .binding = 0,
                            .format = rhi::VertexFormat::Float3,
                            .offset = static_cast<uint32_t>(offsetof(Vertex, normal)),
                        },
                        {
                            .location = 2,
                            .binding = 0,
                            .format = rhi::VertexFormat::Float2,
                            .offset = static_cast<uint32_t>(offsetof(Vertex, uv)),
                        },
                        {
                            .location = 3,
                            .binding = 0,
                            .format = rhi::VertexFormat::Float4,
                            .offset = static_cast<uint32_t>(offsetof(Vertex, color)),
                        },
                        {
                            .location = 4,
                            .binding = 0,
                            .format = rhi::VertexFormat::Float4,
                            .offset = static_cast<uint32_t>(offsetof(Vertex, tangent)),
                        },
                    },
            },
        .topology = rhi::PrimitiveTopology::TriangleList,
        .rasterization =
            {
                .polygonMode = rhi::PolygonMode::Fill,
                .cullMode = rhi::CullMode::Back,
                .frontFace = rhi::FrontFace::CounterClockwise,
            },
        .depthStencil =
            {
                .depthTestEnable = true,
                .depthWriteEnable = true,
                .depthCompareOp = rhi::CompareOp::Less,
            },
        .colorBlendAttachments = {{
            .blendEnable = false,
            .colorWriteMask = rhi::ColorComponentFlags::All,
        }},
        .layout = pipelineLayout_,
        .colorAttachmentFormats = {colorFormat_},
        .depthAttachmentFormat = depthFormat_,
        .debugName = "MeshGraphicsPipeline",
    };
    auto pipeRes = device_->createGraphicsPipeline(pipeDesc);
    if (!pipeRes.hasValue()) {
        shutdown();
        return pipeRes.error();
    }
    pipeline_ = pipeRes.value();

    return Result<void>::success();
}

void SceneRenderer::shutdown() {
    if (device_ != nullptr) {
        if (pipeline_.isValid()) {
            device_->destroyGraphicsPipeline(pipeline_);
            pipeline_ = {};
        }
        if (pipelineLayout_.isValid()) {
            device_->destroyPipelineLayout(pipelineLayout_);
            pipelineLayout_ = {};
        }
        if (materialDescriptorSetLayout_.isValid()) {
            device_->destroyDescriptorSetLayout(materialDescriptorSetLayout_);
            materialDescriptorSetLayout_ = {};
        }
        if (fragmentShader_.isValid()) {
            device_->destroyShader(fragmentShader_);
            fragmentShader_ = {};
        }
        if (vertexShader_.isValid()) {
            device_->destroyShader(vertexShader_);
            vertexShader_ = {};
        }
        if (depthTarget_.isValid()) {
            device_->destroyTexture(depthTarget_);
            depthTarget_ = {};
        }
        if (colorTarget_.isValid()) {
            device_->destroyTexture(colorTarget_);
            colorTarget_ = {};
        }
        device_ = nullptr;
    }
    width_ = 0;
    height_ = 0;
}

Result<void> SceneRenderer::resize(uint32_t width, uint32_t height) {
    if (device_ == nullptr) {
        return Error{.code = ErrorCode::InvalidState,
                     .message = "SceneRenderer not initialized for resize"};
    }
    if (width == 0 || height == 0) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Resize dimensions must be non-zero"};
    }
    if (width == width_ && height == height_) {
        return Result<void>::success();
    }

    // Synchronize GPU to prevent destroying textures in use by pending command buffers (Finding
    // F-04)
    device_->waitIdle();

    if (colorTarget_.isValid()) {
        device_->destroyTexture(colorTarget_);
        colorTarget_ = {};
    }
    if (depthTarget_.isValid()) {
        device_->destroyTexture(depthTarget_);
        depthTarget_ = {};
    }

    width_ = width;
    height_ = height;

    rhi::TextureDesc colorDesc{
        .type = rhi::TextureType::Texture2D,
        .format = colorFormat_,
        .width = width_,
        .height = height_,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = rhi::TextureUsageFlags::ColorAttachment | rhi::TextureUsageFlags::Sampled |
                 rhi::TextureUsageFlags::TransferSrc,
        .debugName = "SceneRendererColorTarget",
    };
    auto colorRes = device_->createTexture(colorDesc);
    if (!colorRes.hasValue()) {
        return colorRes.error();
    }
    colorTarget_ = colorRes.value();

    rhi::TextureDesc depthDesc{
        .type = rhi::TextureType::Texture2D,
        .format = depthFormat_,
        .width = width_,
        .height = height_,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = rhi::TextureUsageFlags::DepthStencilAttachment,
        .debugName = "SceneRendererDepthTarget",
    };
    auto depthRes = device_->createTexture(depthDesc);
    if (!depthRes.hasValue()) {
        return depthRes.error();
    }
    depthTarget_ = depthRes.value();

    return Result<void>::success();
}

RenderStats SceneRenderer::renderScene(world::World& world, AssetManager& assetManager,
                                       rhi::RhiDevice& device, rhi::RhiCommandList& cmdList,
                                       const Mat4& viewProjection) {
    RenderStats stats{};
    const bool managePass = colorTarget_.isValid();

    if (managePass) {
        rhi::RenderPassBeginInfo passInfo{
            .colorTarget = colorTarget_,
            .depthTarget = depthTarget_,
            .clearColor = {0.08F, 0.12F, 0.20F, 1.0F},
            .clearDepth = 1.0F,
            .clearTarget = true,
        };
        cmdList.beginRenderPass(passInfo);
        cmdList.setViewport(0.0F, 0.0F, static_cast<float>(width_), static_cast<float>(height_));
        cmdList.setScissor(0, 0, width_, height_);
    }

    if (pipeline_.isValid()) {
        cmdList.bindPipeline(pipeline_);
    }

    auto& ecs = world.getRawWorld();
    auto query = ecs.query<const MeshComponent>();
    query.each([&](flecs::entity e, const MeshComponent& meshComp) {
        if (!meshComp.visible) {
            return;
        }

        AssetId meshId = meshComp.meshAsset;
        if ((!meshId.isValid() || !assetManager.isLoaded(meshId)) && !meshComp.assetPath.empty()) {
            meshId = assetManager.findAssetByPath(meshComp.assetPath);
            if (!meshId.isValid() || !assetManager.isLoaded(meshId)) {
                auto loadRes = assetManager.loadMesh(meshComp.assetPath);
                if (loadRes.hasValue()) {
                    meshId = loadRes.value();
                }
            }
        }

        if (!meshId.isValid() || !assetManager.isLoaded(meshId)) {
            return;
        }

        GpuMesh* gpuMesh = assetManager.getGpuMesh(meshId);
        if (gpuMesh == nullptr) {
            auto uploadRes = assetManager.uploadMesh(meshId, device);
            if (uploadRes.hasValue()) {
                gpuMesh = uploadRes.value();
            }
        }

        if (gpuMesh == nullptr || !gpuMesh->isValid()) {
            return;
        }

        // Compute model matrix from entity transform.
        // Priority: WorldTransformComponent (hierarchy-aware world matrix) >
        //           TransformComponent (local transform fallback) >
        //           PhysicsTransformComponent (physics simulation output) >
        //           identity (stationary at world origin).
        //
        // WorldTransformComponent is preferred because it accumulates parent chain transforms
        // and is updated by HierarchyTransformSystem every frame before rendering. Falling back
        // to PhysicsTransformComponent preserves correct rendering of dynamic rigid bodies that
        // are root-level entities (not parented) updated by PhysicsPostSync.
        Mat4 model = Mat4::identity();
        if (e.has<world::WorldTransformComponent>()) {
            const auto* wt = e.get<world::WorldTransformComponent>();
            if (wt != nullptr) {
                model = wt->worldMatrix;
            }
        } else if (e.has<world::TransformComponent>()) {
            const auto* tc = e.get<world::TransformComponent>();
            if (tc != nullptr) {
                model = tc->transform.toMatrix();
            }
        } else if (e.has<physics::PhysicsTransformComponent>()) {
            const auto* tc = e.get<physics::PhysicsTransformComponent>();
            if (tc != nullptr) {
                model = tc->transform.toMatrix();
            }
        }

        // Pack 128-byte push constants (64B viewProj + 64B model)
        if (pipelineLayout_.isValid()) {
            alignas(16) float pushData[32];
            std::memcpy(&pushData[0], viewProjection.m, 64);
            std::memcpy(&pushData[16], model.m, 64);
            cmdList.pushConstants(pipelineLayout_, rhi::ShaderStageFlags::Vertex, 0,
                                  sizeof(pushData), pushData);
        }

        // Bind vertex buffer
        cmdList.bindVertexBuffer(0, gpuMesh->getVertexBuffer(), 0);

        auto bindMaterial = [&](uint32_t matIndex) {
            if (pipelineLayout_.isValid() && materialDescriptorSetLayout_.isValid()) {
                GpuMaterial* mat = assetManager.getGpuMaterial(meshId, matIndex);
                if (mat == nullptr) {
                    auto upMat = assetManager.uploadMaterial(meshId, matIndex, device,
                                                             materialDescriptorSetLayout_);
                    if (upMat.hasValue()) {
                        mat = upMat.value();
                    } else {
                        auto defMat = assetManager.getOrCreateDefaultMaterial(
                            device, materialDescriptorSetLayout_);
                        if (defMat.hasValue()) {
                            mat = defMat.value();
                        }
                    }
                }
                if (mat != nullptr && mat->isValid()) {
                    const std::array<rhi::DescriptorSetHandle, 1> sets = {mat->getDescriptorSet()};
                    cmdList.bindDescriptorSets(pipelineLayout_, 0, sets);
                }
            }
        };

        // Bind index buffer if present
        if (gpuMesh->getIndexBuffer().isValid() && gpuMesh->getIndexCount() > 0) {
            cmdList.bindIndexBuffer(gpuMesh->getIndexBuffer(), 0, true);

            const auto& submeshes = gpuMesh->getSubmeshes();
            if (meshComp.submeshIndex < submeshes.size()) {
                const auto& sub = submeshes[meshComp.submeshIndex];
                bindMaterial(sub.materialIndex);
                cmdList.drawIndexed(sub.indexCount, 1, sub.firstIndex, sub.vertexOffset, 0);
                stats.drawCalls++;
                stats.triangles += sub.indexCount / 3;
            } else if (submeshes.empty()) {
                bindMaterial(0);
                cmdList.drawIndexed(gpuMesh->getIndexCount(), 1, 0, 0, 0);
                stats.drawCalls++;
                stats.triangles += gpuMesh->getIndexCount() / 3;
            } else {
                for (const auto& sub : submeshes) {
                    bindMaterial(sub.materialIndex);
                    cmdList.drawIndexed(sub.indexCount, 1, sub.firstIndex, sub.vertexOffset, 0);
                    stats.drawCalls++;
                    stats.triangles += sub.indexCount / 3;
                }
            }
        } else if (gpuMesh->getVertexCount() > 0) {
            bindMaterial(0);
            cmdList.draw(gpuMesh->getVertexCount(), 1, 0, 0);
            stats.drawCalls++;
            stats.triangles += gpuMesh->getVertexCount() / 3;
        }

        stats.meshesRendered++;
    });

    if (managePass) {
        cmdList.endRenderPass();
    }

    return stats;
}

} // namespace eng::render
