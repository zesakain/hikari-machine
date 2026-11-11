/// @file scene_renderer.cpp
/// @brief Scene mesh rendering traversal and command recording implementation.

#include "render/scene_renderer.h"

#include "core/assert.h"
#include "physics/physics_components.h"
#include "render/gpu_material.h"
#include "render/mesh_shader_spirv.h"
#include "world/components.h"

#include <cmath>
#include <cstring>

namespace eng::render {

SceneRenderer::~SceneRenderer() {
    shutdown();
}

SceneRenderer::SceneRenderer(SceneRenderer&& other) noexcept
    : device_(other.device_), colorTarget_(other.colorTarget_), depthTarget_(other.depthTarget_),
      vertexShader_(other.vertexShader_), fragmentShader_(other.fragmentShader_),
      materialDescriptorSetLayout_(other.materialDescriptorSetLayout_),
      sceneDescriptorSetLayout_(other.sceneDescriptorSetLayout_),
      sceneDescriptorPool_(other.sceneDescriptorPool_),
      sceneDescriptorSet_(other.sceneDescriptorSet_),
      lightingUniformBuffer_(other.lightingUniformBuffer_),
      shadowDepthTarget_(other.shadowDepthTarget_), shadowSampler_(other.shadowSampler_),
      shadowVertexShader_(other.shadowVertexShader_),
      shadowPipelineLayout_(other.shadowPipelineLayout_), shadowPipeline_(other.shadowPipeline_),
      pipelineLayout_(other.pipelineLayout_), pipeline_(other.pipeline_), width_(other.width_),
      height_(other.height_), shadowResolution_(other.shadowResolution_),
      colorFormat_(other.colorFormat_), depthFormat_(other.depthFormat_) {
    other.device_ = nullptr;
    other.colorTarget_ = {};
    other.depthTarget_ = {};
    other.vertexShader_ = {};
    other.fragmentShader_ = {};
    other.materialDescriptorSetLayout_ = {};
    other.sceneDescriptorSetLayout_ = {};
    other.sceneDescriptorPool_ = {};
    other.sceneDescriptorSet_ = {};
    other.lightingUniformBuffer_ = {};
    other.shadowDepthTarget_ = {};
    other.shadowSampler_ = {};
    other.shadowVertexShader_ = {};
    other.shadowPipelineLayout_ = {};
    other.shadowPipeline_ = {};
    other.pipelineLayout_ = {};
    other.pipeline_ = {};
    other.width_ = 0;
    other.height_ = 0;
    other.shadowResolution_ = 2048;
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
        sceneDescriptorSetLayout_ = other.sceneDescriptorSetLayout_;
        sceneDescriptorPool_ = other.sceneDescriptorPool_;
        sceneDescriptorSet_ = other.sceneDescriptorSet_;
        lightingUniformBuffer_ = other.lightingUniformBuffer_;
        shadowDepthTarget_ = other.shadowDepthTarget_;
        shadowSampler_ = other.shadowSampler_;
        shadowVertexShader_ = other.shadowVertexShader_;
        shadowPipelineLayout_ = other.shadowPipelineLayout_;
        shadowPipeline_ = other.shadowPipeline_;
        pipelineLayout_ = other.pipelineLayout_;
        pipeline_ = other.pipeline_;
        width_ = other.width_;
        height_ = other.height_;
        shadowResolution_ = other.shadowResolution_;
        colorFormat_ = other.colorFormat_;
        depthFormat_ = other.depthFormat_;

        other.device_ = nullptr;
        other.colorTarget_ = {};
        other.depthTarget_ = {};
        other.vertexShader_ = {};
        other.fragmentShader_ = {};
        other.materialDescriptorSetLayout_ = {};
        other.sceneDescriptorSetLayout_ = {};
        other.sceneDescriptorPool_ = {};
        other.sceneDescriptorSet_ = {};
        other.lightingUniformBuffer_ = {};
        other.shadowDepthTarget_ = {};
        other.shadowSampler_ = {};
        other.shadowVertexShader_ = {};
        other.shadowPipelineLayout_ = {};
        other.shadowPipeline_ = {};
        other.pipelineLayout_ = {};
        other.pipeline_ = {};
        other.width_ = 0;
        other.height_ = 0;
        other.shadowResolution_ = 2048;
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

    // 2b. Allocate directional shadow map depth texture
    rhi::TextureDesc shadowDesc{
        .type = rhi::TextureType::Texture2D,
        .format = rhi::TextureFormat::D32_FLOAT,
        .width = shadowResolution_,
        .height = shadowResolution_,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = rhi::TextureUsageFlags::DepthStencilAttachment | rhi::TextureUsageFlags::Sampled,
        .debugName = "DirectionalShadowMap",
    };
    auto shadowRes = device_->createTexture(shadowDesc);
    if (!shadowRes.hasValue()) {
        shutdown();
        return shadowRes.error();
    }
    shadowDepthTarget_ = shadowRes.value();

    // 2c. Allocate shadow comparison sampler
    rhi::SamplerDesc shadowSampDesc{
        .minFilter = rhi::Filter::Linear,
        .magFilter = rhi::Filter::Linear,
        .mipmapMode = rhi::Filter::Linear,
        .addressU = rhi::SamplerAddressMode::ClampToEdge,
        .addressV = rhi::SamplerAddressMode::ClampToEdge,
        .addressW = rhi::SamplerAddressMode::ClampToEdge,
        .compareEnable = true,
        .compareOp = rhi::CompareOp::LessOrEqual,
        .debugName = "ShadowComparisonSampler",
    };
    auto sampRes = device_->createSampler(shadowSampDesc);
    if (!sampRes.hasValue()) {
        shutdown();
        return sampRes.error();
    }
    shadowSampler_ = sampRes.value();

    // 2d. Allocate lighting uniform buffer
    rhi::BufferDesc uboDesc{
        .sizeInBytes = sizeof(GpuLightingData),
        .usage = rhi::BufferUsageFlags::Uniform,
        .memoryUsage = rhi::MemoryUsage::CpuToGpu,
        .debugName = "SceneLightingUniformBuffer",
    };
    auto uboRes = device_->createBuffer(uboDesc);
    if (!uboRes.hasValue()) {
        shutdown();
        return uboRes.error();
    }
    lightingUniformBuffer_ = uboRes.value();

    GpuLightingData initialLighting{};
    std::ignore =
        device_->writeBuffer(lightingUniformBuffer_, 0, &initialLighting, sizeof(GpuLightingData));

    // 3. Create material descriptor set layout (Set 0)
    auto matLayoutDesc = createMaterialDescriptorSetLayoutDesc();
    auto matLayoutRes = device_->createDescriptorSetLayout(matLayoutDesc);
    if (!matLayoutRes.hasValue()) {
        shutdown();
        return matLayoutRes.error();
    }
    materialDescriptorSetLayout_ = matLayoutRes.value();

    // 3b. Create scene lighting descriptor set layout (Set 1)
    auto sceneLayoutDesc = createSceneDescriptorSetLayoutDesc();
    auto sceneLayoutRes = device_->createDescriptorSetLayout(sceneLayoutDesc);
    if (!sceneLayoutRes.hasValue()) {
        shutdown();
        return sceneLayoutRes.error();
    }
    sceneDescriptorSetLayout_ = sceneLayoutRes.value();

    // 3c. Create descriptor pool & allocate Set 1
    rhi::DescriptorPoolDesc poolDesc{
        .maxSets = 1,
        .poolSizes =
            {
                {.type = rhi::DescriptorType::UniformBuffer, .count = 1},
                {.type = rhi::DescriptorType::CombinedImageSampler, .count = 1},
            },
        .debugName = "SceneLightingDescriptorPool",
    };
    auto poolRes = device_->createDescriptorPool(poolDesc);
    if (!poolRes.hasValue()) {
        shutdown();
        return poolRes.error();
    }
    sceneDescriptorPool_ = poolRes.value();

    auto setRes = device_->allocateDescriptorSet(sceneDescriptorPool_, sceneDescriptorSetLayout_);
    if (!setRes.hasValue()) {
        shutdown();
        return setRes.error();
    }
    sceneDescriptorSet_ = setRes.value();

    std::array<rhi::DescriptorWrite, 2> sceneWrites{
        rhi::DescriptorWrite::makeBuffer(0, rhi::DescriptorType::UniformBuffer,
                                         lightingUniformBuffer_, 0, sizeof(GpuLightingData)),
        rhi::DescriptorWrite::makeCombinedImageSampler(1, shadowDepthTarget_, shadowSampler_),
    };
    std::ignore = device_->updateDescriptorSet(sceneDescriptorSet_, sceneWrites);

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
        .descriptorSetLayoutHandles = {materialDescriptorSetLayout_, sceneDescriptorSetLayout_},
        .descriptorSetLayouts = {},
        .debugName = "MeshPipelineLayout",
    };
    auto layoutRes = device_->createPipelineLayout(layoutDesc);
    if (!layoutRes.hasValue()) {
        shutdown();
        return layoutRes.error();
    }
    pipelineLayout_ = layoutRes.value();

    // 6. Build Graphics Pipeline for offscreen dynamic rendering
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

    // 7. Compile Shadow Depth Vertex Shader and create Shadow Pipeline
    rhi::ShaderDesc shadowVertDesc{
        .stage = rhi::ShaderStageFlags::Vertex,
        .spirvCode = kShadowDepthVertexShaderSpirv,
        .entryPoint = "main",
        .debugName = "ShadowDepthVertexShader",
    };
    auto shadowVertRes = device_->createShader(shadowVertDesc);
    if (!shadowVertRes.hasValue()) {
        shutdown();
        return shadowVertRes.error();
    }
    shadowVertexShader_ = shadowVertRes.value();

    rhi::PipelineLayoutDesc shadowLayoutDesc{
        .pushConstants = {{
            .stageFlags = rhi::ShaderStageFlags::Vertex,
            .offset = 0,
            .size = 128,
        }},
        .descriptorSetLayoutHandles = {},
        .descriptorSetLayouts = {},
        .debugName = "ShadowPipelineLayout",
    };
    auto shadowLayoutRes = device_->createPipelineLayout(shadowLayoutDesc);
    if (!shadowLayoutRes.hasValue()) {
        shutdown();
        return shadowLayoutRes.error();
    }
    shadowPipelineLayout_ = shadowLayoutRes.value();

    rhi::GraphicsPipelineDesc shadowPipeDesc{
        .vertexShader = shadowVertexShader_,
        .fragmentShader = {},
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
                .depthCompareOp = rhi::CompareOp::LessOrEqual,
            },
        .colorBlendAttachments = {},
        .layout = shadowPipelineLayout_,
        .colorAttachmentFormats = {},
        .depthAttachmentFormat = rhi::TextureFormat::D32_FLOAT,
        .debugName = "ShadowGraphicsPipeline",
    };
    auto shadowPipeRes = device_->createGraphicsPipeline(shadowPipeDesc);
    if (!shadowPipeRes.hasValue()) {
        shutdown();
        return shadowPipeRes.error();
    }
    shadowPipeline_ = shadowPipeRes.value();

    return Result<void>::success();
}

void SceneRenderer::shutdown() {
    if (device_ != nullptr) {
        if (shadowPipeline_.isValid()) {
            device_->destroyGraphicsPipeline(shadowPipeline_);
            shadowPipeline_ = {};
        }
        if (shadowPipelineLayout_.isValid()) {
            device_->destroyPipelineLayout(shadowPipelineLayout_);
            shadowPipelineLayout_ = {};
        }
        if (shadowVertexShader_.isValid()) {
            device_->destroyShader(shadowVertexShader_);
            shadowVertexShader_ = {};
        }
        if (shadowSampler_.isValid()) {
            device_->destroySampler(shadowSampler_);
            shadowSampler_ = {};
        }
        if (shadowDepthTarget_.isValid()) {
            device_->destroyTexture(shadowDepthTarget_);
            shadowDepthTarget_ = {};
        }
        if (sceneDescriptorPool_.isValid()) {
            device_->destroyDescriptorPool(sceneDescriptorPool_);
            sceneDescriptorPool_ = {};
            sceneDescriptorSet_ = {};
        }
        if (sceneDescriptorSetLayout_.isValid()) {
            device_->destroyDescriptorSetLayout(sceneDescriptorSetLayout_);
            sceneDescriptorSetLayout_ = {};
        }
        if (lightingUniformBuffer_.isValid()) {
            device_->destroyBuffer(lightingUniformBuffer_);
            lightingUniformBuffer_ = {};
        }
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
    auto& ecs = world.getRawWorld();

    // 1. Gather Light Sources & Build GpuLightingData
    GpuLightingData lightingData{};
    lightingData.cameraPosAndAmbientIntensity = Vec4(0.0F, 0.0F, 5.0F, 0.15F);
    lightingData.ambientColorAndShadowBias = Vec4(1.0F, 1.0F, 1.0F, 0.005F);

    // Camera extraction from viewProjection
    auto invVPOpt = viewProjection.inverse();
    if (invVPOpt.has_value()) {
        const auto& ivp = invVPOpt.value();
        float x = -ivp.m[8];
        float y = -ivp.m[9];
        float z = -ivp.m[10];
        float w = -ivp.m[11];
        if (std::abs(w) > 1e-6F) {
            lightingData.cameraPosAndAmbientIntensity.x = x / w;
            lightingData.cameraPosAndAmbientIntensity.y = y / w;
            lightingData.cameraPosAndAmbientIntensity.z = z / w;
        }
    }

    bool hasDirLight = false;
    auto dirQuery = ecs.query<const DirectionalLightComponent>();
    dirQuery.each([&](flecs::entity e, const DirectionalLightComponent& dirComp) {
        static_cast<void>(e);
        if (!dirComp.enabled || hasDirLight) {
            return;
        }
        hasDirLight = true;
        Vec3 dir = dirComp.direction;
        if (dir.lengthSq() > 1e-6F) {
            dir = dir.normalized();
        } else {
            dir = Vec3(0.5F, 0.8F, 0.5F).normalized();
        }
        lightingData.dirLight.directionAndCastShadow =
            Vec4(dir.x, dir.y, dir.z, dirComp.castShadows ? 1.0F : 0.0F);
        lightingData.dirLight.colorAndIntensity =
            Vec4(dirComp.color.x, dirComp.color.y, dirComp.color.z, dirComp.intensity);
        lightingData.ambientColorAndShadowBias.w = dirComp.shadowBias;
        lightingData.dirLightActive = 1;
    });

    if (!hasDirLight) {
        // Default sunlight baseline preserving Milestone 10D behavior
        Vec3 dir = Vec3(0.5F, 0.8F, 0.5F).normalized();
        lightingData.dirLight.directionAndCastShadow = Vec4(dir.x, dir.y, dir.z, 0.0F);
        lightingData.dirLight.colorAndIntensity = Vec4(1.0F, 1.0F, 1.0F, 2.5F);
        lightingData.dirLightActive = 1;
    }

    // Point lights
    uint32_t pointCount = 0;
    auto pointQuery = ecs.query<const PointLightComponent>();
    pointQuery.each([&](flecs::entity e, const PointLightComponent& pComp) {
        if (!pComp.enabled || pointCount >= kMaxPointLights) {
            return;
        }
        Vec3 pos = pComp.position;
        if (e.has<world::WorldTransformComponent>()) {
            const auto* wt = e.get<world::WorldTransformComponent>();
            if (wt != nullptr) {
                pos = Vec3(wt->worldMatrix.m[12], wt->worldMatrix.m[13], wt->worldMatrix.m[14]);
            }
        } else if (e.has<world::TransformComponent>()) {
            const auto* tc = e.get<world::TransformComponent>();
            if (tc != nullptr) {
                pos = tc->transform.position;
            }
        }
        lightingData.pointLights[pointCount].positionAndRange =
            Vec4(pos.x, pos.y, pos.z, std::max(0.1F, pComp.range));
        lightingData.pointLights[pointCount].colorAndIntensity =
            Vec4(pComp.color.x, pComp.color.y, pComp.color.z, pComp.intensity);
        ++pointCount;
    });
    lightingData.numPointLights = pointCount;

    // Spot lights
    uint32_t spotCount = 0;
    auto spotQuery = ecs.query<const SpotLightComponent>();
    spotQuery.each([&](flecs::entity e, const SpotLightComponent& sComp) {
        if (!sComp.enabled || spotCount >= kMaxSpotLights) {
            return;
        }
        Vec3 pos = sComp.position;
        Vec3 dir = sComp.direction;
        if (e.has<world::WorldTransformComponent>()) {
            const auto* wt = e.get<world::WorldTransformComponent>();
            if (wt != nullptr) {
                pos = Vec3(wt->worldMatrix.m[12], wt->worldMatrix.m[13], wt->worldMatrix.m[14]);
                dir = wt->worldMatrix.transformVector(dir).normalized();
            }
        } else if (e.has<world::TransformComponent>()) {
            const auto* tc = e.get<world::TransformComponent>();
            if (tc != nullptr) {
                pos = tc->transform.position;
                dir = tc->transform.rotation.rotate(dir).normalized();
            }
        }
        if (dir.lengthSq() > 1e-6F) {
            dir = dir.normalized();
        } else {
            dir = Vec3(0.0F, -1.0F, 0.0F);
        }
        lightingData.spotLights[spotCount].positionAndRange =
            Vec4(pos.x, pos.y, pos.z, std::max(0.1F, sComp.range));
        lightingData.spotLights[spotCount].directionAndIntensity =
            Vec4(dir.x, dir.y, dir.z, sComp.intensity);
        lightingData.spotLights[spotCount].colorAndCastShadow =
            Vec4(sComp.color.x, sComp.color.y, sComp.color.z, sComp.castShadows ? 1.0F : 0.0F);
        lightingData.spotLights[spotCount].coneAngles =
            Vec4(std::cos(sComp.innerConeAngle), std::cos(sComp.outerConeAngle), 0.0F, 0.0F);
        ++spotCount;
    });
    lightingData.numSpotLights = spotCount;

    // Compute Directional Light-Space View-Projection Matrix
    Vec3 dir = Vec3(lightingData.dirLight.directionAndCastShadow.x,
                    lightingData.dirLight.directionAndCastShadow.y,
                    lightingData.dirLight.directionAndCastShadow.z);
    Vec3 target(0.0F, 0.0F, 0.0F);
    Vec3 lightPos = target + dir * 25.0F;
    Vec3 up = std::abs(dir.y) > 0.99F ? Vec3::unitZ() : Vec3::unitY();
    Mat4 lightView = Mat4::lookAt(lightPos, target, up);
    Mat4 lightProj = Mat4::orthographic(-15.0F, 15.0F, -15.0F, 15.0F, 0.1F, 60.0F);
    Mat4 lightSpaceMatrix = lightProj * lightView;
    lightingData.lightSpaceMatrix = lightSpaceMatrix;

    // Upload lighting data to GPU uniform buffer
    if (lightingUniformBuffer_.isValid() && device_ != nullptr) {
        std::ignore =
            device_->writeBuffer(lightingUniformBuffer_, 0, &lightingData, sizeof(GpuLightingData));
    }

    // 2. DIRECTIONAL SHADOW PASS
    const bool doShadowPass = (lightingData.dirLight.directionAndCastShadow.w > 0.5F) &&
                              shadowDepthTarget_.isValid() && shadowPipeline_.isValid();
    if (doShadowPass) {
        rhi::RenderPassBeginInfo shadowPassInfo{
            .colorTarget = {},
            .depthTarget = shadowDepthTarget_,
            .clearColor = {0.0F, 0.0F, 0.0F, 1.0F},
            .clearDepth = 1.0F,
            .clearTarget = false,
            .clearDepthTarget = true,
            .renderWidth = shadowResolution_,
            .renderHeight = shadowResolution_,
        };
        cmdList.beginRenderPass(shadowPassInfo);
        cmdList.setViewport(0.0F, 0.0F, static_cast<float>(shadowResolution_),
                            static_cast<float>(shadowResolution_));
        cmdList.setScissor(0, 0, shadowResolution_, shadowResolution_);
        cmdList.bindPipeline(shadowPipeline_);

        auto shadowMeshQuery = ecs.query<const MeshComponent>();
        shadowMeshQuery.each([&](flecs::entity e, const MeshComponent& meshComp) {
            if (!meshComp.visible || !meshComp.castShadows) {
                return;
            }
            AssetId meshId = meshComp.meshAsset;
            if ((!meshId.isValid() || !assetManager.isLoaded(meshId)) &&
                !meshComp.assetPath.empty()) {
                meshId = assetManager.findAssetByPath(meshComp.assetPath);
            }
            if (!meshId.isValid() || !assetManager.isLoaded(meshId)) {
                return;
            }
            GpuMesh* gpuMesh = assetManager.getGpuMesh(meshId);
            if (gpuMesh == nullptr || !gpuMesh->isValid()) {
                return;
            }

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

            if (shadowPipelineLayout_.isValid()) {
                alignas(16) float pushData[32];
                std::memcpy(&pushData[0], lightSpaceMatrix.m, 64);
                std::memcpy(&pushData[16], model.m, 64);
                cmdList.pushConstants(shadowPipelineLayout_, rhi::ShaderStageFlags::Vertex, 0,
                                      sizeof(pushData), pushData);
            }

            cmdList.bindVertexBuffer(0, gpuMesh->getVertexBuffer(), 0);
            if (gpuMesh->getIndexBuffer().isValid() && gpuMesh->getIndexCount() > 0) {
                cmdList.bindIndexBuffer(gpuMesh->getIndexBuffer(), 0, true);
                const auto& submeshes = gpuMesh->getSubmeshes();
                if (meshComp.submeshIndex < submeshes.size()) {
                    const auto& sub = submeshes[meshComp.submeshIndex];
                    cmdList.drawIndexed(sub.indexCount, 1, sub.firstIndex, sub.vertexOffset, 0);
                } else if (submeshes.empty()) {
                    cmdList.drawIndexed(gpuMesh->getIndexCount(), 1, 0, 0, 0);
                } else {
                    for (const auto& sub : submeshes) {
                        cmdList.drawIndexed(sub.indexCount, 1, sub.firstIndex, sub.vertexOffset, 0);
                    }
                }
            } else if (gpuMesh->getVertexCount() > 0) {
                cmdList.draw(gpuMesh->getVertexCount(), 1, 0, 0);
            }
        });

        cmdList.endRenderPass();
    }

    // 3. MAIN PBR SCENE PASS
    const bool managePass = colorTarget_.isValid();

    if (managePass) {
        rhi::RenderPassBeginInfo passInfo{
            .colorTarget = colorTarget_,
            .depthTarget = depthTarget_,
            .clearColor = {0.08F, 0.12F, 0.20F, 1.0F},
            .clearDepth = 1.0F,
            .clearTarget = true,
            .clearDepthTarget = true,
        };
        cmdList.beginRenderPass(passInfo);
        cmdList.setViewport(0.0F, 0.0F, static_cast<float>(width_), static_cast<float>(height_));
        cmdList.setScissor(0, 0, width_, height_);
    }

    if (pipeline_.isValid()) {
        cmdList.bindPipeline(pipeline_);
    }

    // Bind Set 1: Scene Lighting & Shadow Map once for the main pass
    if (pipelineLayout_.isValid() && sceneDescriptorSet_.isValid()) {
        const std::array<rhi::DescriptorSetHandle, 1> sceneSets = {sceneDescriptorSet_};
        cmdList.bindDescriptorSets(pipelineLayout_, 1, sceneSets);
    }

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
