/// @file scene_renderer.cpp
/// @brief Scene mesh rendering traversal and command recording implementation.

#include "render/scene_renderer.h"

#include "core/assert.h"
#include "physics/physics_components.h"
#include "render/gpu_material.h"
#include "render/mesh_shader_spirv.h"
#include "world/components.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace eng::render {

namespace {

struct BloomDownsamplePushConstants {
    Vec2 texelSize;
    float threshold;
    float knee;
    int32_t isFirstPass;
    float padding[3];
};

struct BloomUpsamplePushConstants {
    Vec2 texelSize;
    float filterRadius;
    float blendFactor;
};

struct ToneMappingPushConstants {
    float exposure;
    int32_t toneMapOp;
    float bloomIntensity;
    int32_t bloomEnabled;
    float whitePoint;
    float padding[3];
};

rhi::TextureFormat selectHdrFormat(const rhi::RhiDevice& device) {
    const auto requiredUsage =
        rhi::TextureUsageFlags::ColorAttachment | rhi::TextureUsageFlags::Sampled;
    if (device.isTextureFormatSupported(rhi::TextureFormat::RGBA16_FLOAT, requiredUsage) &&
        device.isTextureFormatFilterable(rhi::TextureFormat::RGBA16_FLOAT)) {
        return rhi::TextureFormat::RGBA16_FLOAT;
    }
    if (device.isTextureFormatSupported(rhi::TextureFormat::RGBA32_FLOAT, requiredUsage) &&
        device.isTextureFormatFilterable(rhi::TextureFormat::RGBA32_FLOAT)) {
        return rhi::TextureFormat::RGBA32_FLOAT;
    }
    return rhi::TextureFormat::RGBA8_UNORM;
}

} // namespace

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
      pipelineLayout_(other.pipelineLayout_), pipeline_(other.pipeline_),
      hdrTarget_(other.hdrTarget_), hdrFormat_(other.hdrFormat_),
      postProcessSettings_(other.postProcessSettings_),
      postProcessSampler_(other.postProcessSampler_),
      fullscreenVertexShader_(other.fullscreenVertexShader_),
      bloomDownFragmentShader_(other.bloomDownFragmentShader_),
      bloomUpFragmentShader_(other.bloomUpFragmentShader_),
      toneMappingFragmentShader_(other.toneMappingFragmentShader_),
      singleTextureSetLayout_(other.singleTextureSetLayout_),
      dualTextureSetLayout_(other.dualTextureSetLayout_),
      postProcessDescriptorPool_(other.postProcessDescriptorPool_),
      toneMappingSet_(other.toneMappingSet_),
      bloomDownPipelineLayout_(other.bloomDownPipelineLayout_),
      bloomUpPipelineLayout_(other.bloomUpPipelineLayout_),
      toneMappingPipelineLayout_(other.toneMappingPipelineLayout_),
      bloomDownPipeline_(other.bloomDownPipeline_), bloomUpPipeline_(other.bloomUpPipeline_),
      toneMappingPipeline_(other.toneMappingPipeline_), hdrPipeline_(other.hdrPipeline_),
      width_(other.width_), height_(other.height_), shadowResolution_(other.shadowResolution_),
      colorFormat_(other.colorFormat_), depthFormat_(other.depthFormat_) {
    for (size_t i = 0; i < kBloomPyramidLevels; ++i) {
        bloomDownTarget_[i] = other.bloomDownTarget_[i];
        bloomDownSets_[i] = other.bloomDownSets_[i];
        other.bloomDownTarget_[i] = {};
        other.bloomDownSets_[i] = {};
    }
    for (size_t i = 0; i < kBloomPyramidLevels - 1; ++i) {
        bloomUpTarget_[i] = other.bloomUpTarget_[i];
        bloomUpSets_[i] = other.bloomUpSets_[i];
        other.bloomUpTarget_[i] = {};
        other.bloomUpSets_[i] = {};
    }

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
    other.hdrTarget_ = {};
    other.postProcessSampler_ = {};
    other.fullscreenVertexShader_ = {};
    other.bloomDownFragmentShader_ = {};
    other.bloomUpFragmentShader_ = {};
    other.toneMappingFragmentShader_ = {};
    other.singleTextureSetLayout_ = {};
    other.dualTextureSetLayout_ = {};
    other.postProcessDescriptorPool_ = {};
    other.toneMappingSet_ = {};
    other.bloomDownPipelineLayout_ = {};
    other.bloomUpPipelineLayout_ = {};
    other.toneMappingPipelineLayout_ = {};
    other.bloomDownPipeline_ = {};
    other.bloomUpPipeline_ = {};
    other.toneMappingPipeline_ = {};
    other.hdrPipeline_ = {};
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

        hdrTarget_ = other.hdrTarget_;
        hdrFormat_ = other.hdrFormat_;
        postProcessSettings_ = other.postProcessSettings_;
        for (size_t i = 0; i < kBloomPyramidLevels; ++i) {
            bloomDownTarget_[i] = other.bloomDownTarget_[i];
            bloomDownSets_[i] = other.bloomDownSets_[i];
            other.bloomDownTarget_[i] = {};
            other.bloomDownSets_[i] = {};
        }
        for (size_t i = 0; i < kBloomPyramidLevels - 1; ++i) {
            bloomUpTarget_[i] = other.bloomUpTarget_[i];
            bloomUpSets_[i] = other.bloomUpSets_[i];
            other.bloomUpTarget_[i] = {};
            other.bloomUpSets_[i] = {};
        }

        postProcessSampler_ = other.postProcessSampler_;
        fullscreenVertexShader_ = other.fullscreenVertexShader_;
        bloomDownFragmentShader_ = other.bloomDownFragmentShader_;
        bloomUpFragmentShader_ = other.bloomUpFragmentShader_;
        toneMappingFragmentShader_ = other.toneMappingFragmentShader_;
        singleTextureSetLayout_ = other.singleTextureSetLayout_;
        dualTextureSetLayout_ = other.dualTextureSetLayout_;
        postProcessDescriptorPool_ = other.postProcessDescriptorPool_;
        toneMappingSet_ = other.toneMappingSet_;
        bloomDownPipelineLayout_ = other.bloomDownPipelineLayout_;
        bloomUpPipelineLayout_ = other.bloomUpPipelineLayout_;
        toneMappingPipelineLayout_ = other.toneMappingPipelineLayout_;
        bloomDownPipeline_ = other.bloomDownPipeline_;
        bloomUpPipeline_ = other.bloomUpPipeline_;
        toneMappingPipeline_ = other.toneMappingPipeline_;
        hdrPipeline_ = other.hdrPipeline_;

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
        other.hdrTarget_ = {};
        other.postProcessSampler_ = {};
        other.fullscreenVertexShader_ = {};
        other.bloomDownFragmentShader_ = {};
        other.bloomUpFragmentShader_ = {};
        other.toneMappingFragmentShader_ = {};
        other.singleTextureSetLayout_ = {};
        other.dualTextureSetLayout_ = {};
        other.postProcessDescriptorPool_ = {};
        other.toneMappingSet_ = {};
        other.bloomDownPipelineLayout_ = {};
        other.bloomUpPipelineLayout_ = {};
        other.toneMappingPipelineLayout_ = {};
        other.bloomDownPipeline_ = {};
        other.bloomUpPipeline_ = {};
        other.toneMappingPipeline_ = {};
        other.hdrPipeline_ = {};
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
    hdrFormat_ = selectHdrFormat(device);

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

    if (hdrFormat_ != colorFormat_) {
        pipeDesc.colorAttachmentFormats = {hdrFormat_};
        pipeDesc.debugName = "MeshHdrGraphicsPipeline";
        auto hdrPipeRes = device_->createGraphicsPipeline(pipeDesc);
        if (!hdrPipeRes.hasValue()) {
            shutdown();
            return hdrPipeRes.error();
        }
        hdrPipeline_ = hdrPipeRes.value();
    } else {
        hdrPipeline_ = pipeline_;
    }

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

    // 8. Initialize Post-Processing & HDR Resources
    auto ppRes = initPostProcessResources();
    if (!ppRes.hasValue()) {
        shutdown();
        return ppRes.error();
    }

    auto ppTargetsRes = createPostProcessTargets(width_, height_);
    if (!ppTargetsRes.hasValue()) {
        shutdown();
        return ppTargetsRes.error();
    }

    return Result<void>::success();
}

void SceneRenderer::shutdown() {
    if (device_ != nullptr) {
        destroyPostProcessTargets();

        if (toneMappingPipeline_.isValid()) {
            device_->destroyGraphicsPipeline(toneMappingPipeline_);
            toneMappingPipeline_ = {};
        }
        if (bloomUpPipeline_.isValid()) {
            device_->destroyGraphicsPipeline(bloomUpPipeline_);
            bloomUpPipeline_ = {};
        }
        if (bloomDownPipeline_.isValid()) {
            device_->destroyGraphicsPipeline(bloomDownPipeline_);
            bloomDownPipeline_ = {};
        }
        if (toneMappingPipelineLayout_.isValid()) {
            device_->destroyPipelineLayout(toneMappingPipelineLayout_);
            toneMappingPipelineLayout_ = {};
        }
        if (bloomUpPipelineLayout_.isValid()) {
            device_->destroyPipelineLayout(bloomUpPipelineLayout_);
            bloomUpPipelineLayout_ = {};
        }
        if (bloomDownPipelineLayout_.isValid()) {
            device_->destroyPipelineLayout(bloomDownPipelineLayout_);
            bloomDownPipelineLayout_ = {};
        }
        if (postProcessDescriptorPool_.isValid()) {
            device_->destroyDescriptorPool(postProcessDescriptorPool_);
            postProcessDescriptorPool_ = {};
            for (auto& s : bloomDownSets_) {
                s = {};
            }
            for (auto& s : bloomUpSets_) {
                s = {};
            }
            toneMappingSet_ = {};
        }
        if (dualTextureSetLayout_.isValid()) {
            device_->destroyDescriptorSetLayout(dualTextureSetLayout_);
            dualTextureSetLayout_ = {};
        }
        if (singleTextureSetLayout_.isValid()) {
            device_->destroyDescriptorSetLayout(singleTextureSetLayout_);
            singleTextureSetLayout_ = {};
        }
        if (toneMappingFragmentShader_.isValid()) {
            device_->destroyShader(toneMappingFragmentShader_);
            toneMappingFragmentShader_ = {};
        }
        if (bloomUpFragmentShader_.isValid()) {
            device_->destroyShader(bloomUpFragmentShader_);
            bloomUpFragmentShader_ = {};
        }
        if (bloomDownFragmentShader_.isValid()) {
            device_->destroyShader(bloomDownFragmentShader_);
            bloomDownFragmentShader_ = {};
        }
        if (fullscreenVertexShader_.isValid()) {
            device_->destroyShader(fullscreenVertexShader_);
            fullscreenVertexShader_ = {};
        }
        if (postProcessSampler_.isValid()) {
            device_->destroySampler(postProcessSampler_);
            postProcessSampler_ = {};
        }
        if (hdrPipeline_.isValid() && hdrPipeline_ != pipeline_) {
            device_->destroyGraphicsPipeline(hdrPipeline_);
        }
        hdrPipeline_ = {};

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

    destroyPostProcessTargets();

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

    auto ppRes = createPostProcessTargets(width_, height_);
    if (!ppRes.hasValue()) {
        return ppRes.error();
    }

    return Result<void>::success();
}

RenderStats SceneRenderer::renderScene(world::World& world, AssetManager& assetManager,
                                       rhi::RhiDevice& device, rhi::RhiCommandList& cmdList,
                                       const Mat4& viewProjection) {
    RenderStats stats{};
    auto& ecs = world.getRawWorld();

    const bool hasTimestamps = device.getCaps().features.timestampQueries;
    const uint32_t baseQuery = (frameSlotIndex_ % 2) * 16;
    if (hasTimestamps) {
        cmdList.resetQueryPool(baseQuery, 16);
        cmdList.writeTimestamp(baseQuery + 0, rhi::GpuTimestampStage::TopOfPipe);
    }

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
        if (hasTimestamps) {
            cmdList.writeTimestamp(baseQuery + 2, rhi::GpuTimestampStage::TopOfPipe);
        }
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
                stats.culledPrimitives++;
                return;
            }
            AssetId meshId = meshComp.meshAsset;
            if ((!meshId.isValid() || !assetManager.isLoaded(meshId)) &&
                !meshComp.assetPath.empty()) {
                meshId = assetManager.findAssetByPath(meshComp.assetPath);
            }
            if (!meshId.isValid() || !assetManager.isLoaded(meshId)) {
                stats.culledPrimitives++;
                return;
            }
            GpuMesh* gpuMesh = assetManager.getGpuMesh(meshId);
            if (gpuMesh == nullptr || !gpuMesh->isValid()) {
                stats.culledPrimitives++;
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
                    stats.shadowDrawCalls++;
                    stats.shadowTriangles += sub.indexCount / 3;
                } else if (submeshes.empty()) {
                    cmdList.drawIndexed(gpuMesh->getIndexCount(), 1, 0, 0, 0);
                    stats.shadowDrawCalls++;
                    stats.shadowTriangles += gpuMesh->getIndexCount() / 3;
                } else {
                    for (const auto& sub : submeshes) {
                        cmdList.drawIndexed(sub.indexCount, 1, sub.firstIndex, sub.vertexOffset, 0);
                        stats.shadowDrawCalls++;
                        stats.shadowTriangles += sub.indexCount / 3;
                    }
                }
            } else if (gpuMesh->getVertexCount() > 0) {
                cmdList.draw(gpuMesh->getVertexCount(), 1, 0, 0);
                stats.shadowDrawCalls++;
                stats.shadowTriangles += gpuMesh->getVertexCount() / 3;
            }
        });

        cmdList.endRenderPass();
        if (hasTimestamps) {
            cmdList.writeTimestamp(baseQuery + 3, rhi::GpuTimestampStage::BottomOfPipe);
        }
    }

    // 3. MAIN PBR SCENE PASS
    const bool managePass = colorTarget_.isValid();
    const bool useHdr = postProcessSettings_.enabled && hdrTarget_.isValid();
    const rhi::TextureHandle sceneColorTarget = useHdr ? hdrTarget_ : colorTarget_;
    const rhi::PipelineHandle scenePipeline =
        (useHdr && hdrPipeline_.isValid()) ? hdrPipeline_ : pipeline_;

    if (managePass) {
        if (hasTimestamps) {
            cmdList.writeTimestamp(baseQuery + 4, rhi::GpuTimestampStage::TopOfPipe);
        }
        rhi::RenderPassBeginInfo passInfo{
            .colorTarget = sceneColorTarget,
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

    if (scenePipeline.isValid()) {
        cmdList.bindPipeline(scenePipeline);
    }

    // Bind Set 1: Scene Lighting & Shadow Map once for the main pass
    if (pipelineLayout_.isValid() && sceneDescriptorSet_.isValid()) {
        const std::array<rhi::DescriptorSetHandle, 1> sceneSets = {sceneDescriptorSet_};
        cmdList.bindDescriptorSets(pipelineLayout_, 1, sceneSets);
    }

    auto query = ecs.query<const MeshComponent>();
    query.each([&](flecs::entity e, const MeshComponent& meshComp) {
        if (!meshComp.visible) {
            stats.culledPrimitives++;
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
            stats.culledPrimitives++;
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
            stats.culledPrimitives++;
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
                stats.vertices += sub.indexCount;
            } else if (submeshes.empty()) {
                bindMaterial(0);
                cmdList.drawIndexed(gpuMesh->getIndexCount(), 1, 0, 0, 0);
                stats.drawCalls++;
                stats.triangles += gpuMesh->getIndexCount() / 3;
                stats.vertices += gpuMesh->getVertexCount();
            } else {
                for (const auto& sub : submeshes) {
                    bindMaterial(sub.materialIndex);
                    cmdList.drawIndexed(sub.indexCount, 1, sub.firstIndex, sub.vertexOffset, 0);
                    stats.drawCalls++;
                    stats.triangles += sub.indexCount / 3;
                    stats.vertices += sub.indexCount;
                }
            }
        } else if (gpuMesh->getVertexCount() > 0) {
            bindMaterial(0);
            cmdList.draw(gpuMesh->getVertexCount(), 1, 0, 0);
            stats.drawCalls++;
            stats.triangles += gpuMesh->getVertexCount() / 3;
            stats.vertices += gpuMesh->getVertexCount();
        }

        stats.meshesRendered++;
    });

    if (managePass) {
        cmdList.endRenderPass();
        if (hasTimestamps) {
            cmdList.writeTimestamp(baseQuery + 5, rhi::GpuTimestampStage::BottomOfPipe);
            if (!postProcessSettings_.enabled) {
                cmdList.writeTimestamp(baseQuery + 1, rhi::GpuTimestampStage::BottomOfPipe);
            }
        }
    }

    lastRenderStats_ = stats;
    return stats;
}

Result<void> SceneRenderer::initPostProcessResources() {
    // 1. Post-process bilinear sampler with clamp-to-edge
    rhi::SamplerDesc sampDesc{
        .minFilter = rhi::Filter::Linear,
        .magFilter = rhi::Filter::Linear,
        .mipmapMode = rhi::Filter::Linear,
        .addressU = rhi::SamplerAddressMode::ClampToEdge,
        .addressV = rhi::SamplerAddressMode::ClampToEdge,
        .addressW = rhi::SamplerAddressMode::ClampToEdge,
        .debugName = "PostProcessLinearSampler",
    };
    auto sampRes = device_->createSampler(sampDesc);
    if (!sampRes.hasValue()) {
        return sampRes.error();
    }
    postProcessSampler_ = sampRes.value();

    // 2. Descriptor set layouts
    rhi::DescriptorSetLayoutDesc singleDesc{
        .bindings = {{
            .binding = 0,
            .descriptorType = rhi::DescriptorType::CombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = rhi::ShaderStageFlags::Fragment,
        }},
        .debugName = "PostProcessSingleTextureLayout",
    };
    auto singleRes = device_->createDescriptorSetLayout(singleDesc);
    if (!singleRes.hasValue()) {
        return singleRes.error();
    }
    singleTextureSetLayout_ = singleRes.value();

    rhi::DescriptorSetLayoutDesc dualDesc{
        .bindings =
            {
                {
                    .binding = 0,
                    .descriptorType = rhi::DescriptorType::CombinedImageSampler,
                    .descriptorCount = 1,
                    .stageFlags = rhi::ShaderStageFlags::Fragment,
                },
                {
                    .binding = 1,
                    .descriptorType = rhi::DescriptorType::CombinedImageSampler,
                    .descriptorCount = 1,
                    .stageFlags = rhi::ShaderStageFlags::Fragment,
                },
            },
        .debugName = "PostProcessDualTextureLayout",
    };
    auto dualRes = device_->createDescriptorSetLayout(dualDesc);
    if (!dualRes.hasValue()) {
        return dualRes.error();
    }
    dualTextureSetLayout_ = dualRes.value();

    // 3. Descriptor pool for 8 post-processing descriptor sets
    rhi::DescriptorPoolDesc poolDesc{
        .maxSets = 8,
        .poolSizes = {{
            .type = rhi::DescriptorType::CombinedImageSampler,
            .count = 12,
        }},
        .debugName = "PostProcessDescriptorPool",
    };
    auto poolRes = device_->createDescriptorPool(poolDesc);
    if (!poolRes.hasValue()) {
        return poolRes.error();
    }
    postProcessDescriptorPool_ = poolRes.value();

    for (size_t i = 0; i < kBloomPyramidLevels; ++i) {
        auto setRes =
            device_->allocateDescriptorSet(postProcessDescriptorPool_, singleTextureSetLayout_);
        if (!setRes.hasValue()) {
            return setRes.error();
        }
        bloomDownSets_[i] = setRes.value();
    }

    for (size_t i = 0; i < kBloomPyramidLevels - 1; ++i) {
        auto setRes =
            device_->allocateDescriptorSet(postProcessDescriptorPool_, dualTextureSetLayout_);
        if (!setRes.hasValue()) {
            return setRes.error();
        }
        bloomUpSets_[i] = setRes.value();
    }

    auto tmSetRes =
        device_->allocateDescriptorSet(postProcessDescriptorPool_, dualTextureSetLayout_);
    if (!tmSetRes.hasValue()) {
        return tmSetRes.error();
    }
    toneMappingSet_ = tmSetRes.value();

    // 4. Shaders
    rhi::ShaderDesc fsVertDesc{
        .stage = rhi::ShaderStageFlags::Vertex,
        .spirvCode = kFullscreenVertexShaderSpirv,
        .entryPoint = "main",
        .debugName = "FullscreenVertexShader",
    };
    auto fsVertRes = device_->createShader(fsVertDesc);
    if (!fsVertRes.hasValue()) {
        return fsVertRes.error();
    }
    fullscreenVertexShader_ = fsVertRes.value();

    rhi::ShaderDesc bloomDownDesc{
        .stage = rhi::ShaderStageFlags::Fragment,
        .spirvCode = kBloomDownsampleFragmentShaderSpirv,
        .entryPoint = "main",
        .debugName = "BloomDownFragmentShader",
    };
    auto bloomDownRes = device_->createShader(bloomDownDesc);
    if (!bloomDownRes.hasValue()) {
        return bloomDownRes.error();
    }
    bloomDownFragmentShader_ = bloomDownRes.value();

    rhi::ShaderDesc bloomUpDesc{
        .stage = rhi::ShaderStageFlags::Fragment,
        .spirvCode = kBloomUpsampleFragmentShaderSpirv,
        .entryPoint = "main",
        .debugName = "BloomUpFragmentShader",
    };
    auto bloomUpRes = device_->createShader(bloomUpDesc);
    if (!bloomUpRes.hasValue()) {
        return bloomUpRes.error();
    }
    bloomUpFragmentShader_ = bloomUpRes.value();

    rhi::ShaderDesc tmDesc{
        .stage = rhi::ShaderStageFlags::Fragment,
        .spirvCode = kToneMappingFragmentShaderSpirv,
        .entryPoint = "main",
        .debugName = "ToneMappingFragmentShader",
    };
    auto tmRes = device_->createShader(tmDesc);
    if (!tmRes.hasValue()) {
        return tmRes.error();
    }
    toneMappingFragmentShader_ = tmRes.value();

    // 5. Pipeline layouts
    rhi::PipelineLayoutDesc downLayoutDesc{
        .pushConstants = {{
            .stageFlags = rhi::ShaderStageFlags::Fragment,
            .offset = 0,
            .size = sizeof(BloomDownsamplePushConstants),
        }},
        .descriptorSetLayoutHandles = {singleTextureSetLayout_},
        .descriptorSetLayouts = {},
        .debugName = "BloomDownPipelineLayout",
    };
    auto downLayoutRes = device_->createPipelineLayout(downLayoutDesc);
    if (!downLayoutRes.hasValue()) {
        return downLayoutRes.error();
    }
    bloomDownPipelineLayout_ = downLayoutRes.value();

    rhi::PipelineLayoutDesc upLayoutDesc{
        .pushConstants = {{
            .stageFlags = rhi::ShaderStageFlags::Fragment,
            .offset = 0,
            .size = sizeof(BloomUpsamplePushConstants),
        }},
        .descriptorSetLayoutHandles = {dualTextureSetLayout_},
        .descriptorSetLayouts = {},
        .debugName = "BloomUpPipelineLayout",
    };
    auto upLayoutRes = device_->createPipelineLayout(upLayoutDesc);
    if (!upLayoutRes.hasValue()) {
        return upLayoutRes.error();
    }
    bloomUpPipelineLayout_ = upLayoutRes.value();

    rhi::PipelineLayoutDesc tmLayoutDesc{
        .pushConstants = {{
            .stageFlags = rhi::ShaderStageFlags::Fragment,
            .offset = 0,
            .size = sizeof(ToneMappingPushConstants),
        }},
        .descriptorSetLayoutHandles = {dualTextureSetLayout_},
        .descriptorSetLayouts = {},
        .debugName = "ToneMappingPipelineLayout",
    };
    auto tmLayoutRes = device_->createPipelineLayout(tmLayoutDesc);
    if (!tmLayoutRes.hasValue()) {
        return tmLayoutRes.error();
    }
    toneMappingPipelineLayout_ = tmLayoutRes.value();

    // 6. Graphics pipelines for fullscreen passes
    rhi::GraphicsPipelineDesc downPipeDesc{
        .vertexShader = fullscreenVertexShader_,
        .fragmentShader = bloomDownFragmentShader_,
        .vertexInput = {},
        .topology = rhi::PrimitiveTopology::TriangleList,
        .rasterization =
            {
                .polygonMode = rhi::PolygonMode::Fill,
                .cullMode = rhi::CullMode::None,
                .frontFace = rhi::FrontFace::CounterClockwise,
            },
        .depthStencil =
            {
                .depthTestEnable = false,
                .depthWriteEnable = false,
                .depthCompareOp = rhi::CompareOp::Always,
            },
        .colorBlendAttachments = {{
            .blendEnable = false,
            .colorWriteMask = rhi::ColorComponentFlags::All,
        }},
        .layout = bloomDownPipelineLayout_,
        .colorAttachmentFormats = {hdrFormat_},
        .debugName = "BloomDownGraphicsPipeline",
    };
    auto downPipeRes = device_->createGraphicsPipeline(downPipeDesc);
    if (!downPipeRes.hasValue()) {
        return downPipeRes.error();
    }
    bloomDownPipeline_ = downPipeRes.value();

    rhi::GraphicsPipelineDesc upPipeDesc{
        .vertexShader = fullscreenVertexShader_,
        .fragmentShader = bloomUpFragmentShader_,
        .vertexInput = {},
        .topology = rhi::PrimitiveTopology::TriangleList,
        .rasterization =
            {
                .polygonMode = rhi::PolygonMode::Fill,
                .cullMode = rhi::CullMode::None,
                .frontFace = rhi::FrontFace::CounterClockwise,
            },
        .depthStencil =
            {
                .depthTestEnable = false,
                .depthWriteEnable = false,
                .depthCompareOp = rhi::CompareOp::Always,
            },
        .colorBlendAttachments = {{
            .blendEnable = false,
            .colorWriteMask = rhi::ColorComponentFlags::All,
        }},
        .layout = bloomUpPipelineLayout_,
        .colorAttachmentFormats = {hdrFormat_},
        .debugName = "BloomUpGraphicsPipeline",
    };
    auto upPipeRes = device_->createGraphicsPipeline(upPipeDesc);
    if (!upPipeRes.hasValue()) {
        return upPipeRes.error();
    }
    bloomUpPipeline_ = upPipeRes.value();

    rhi::GraphicsPipelineDesc tmPipeDesc{
        .vertexShader = fullscreenVertexShader_,
        .fragmentShader = toneMappingFragmentShader_,
        .vertexInput = {},
        .topology = rhi::PrimitiveTopology::TriangleList,
        .rasterization =
            {
                .polygonMode = rhi::PolygonMode::Fill,
                .cullMode = rhi::CullMode::None,
                .frontFace = rhi::FrontFace::CounterClockwise,
            },
        .depthStencil =
            {
                .depthTestEnable = false,
                .depthWriteEnable = false,
                .depthCompareOp = rhi::CompareOp::Always,
            },
        .colorBlendAttachments = {{
            .blendEnable = false,
            .colorWriteMask = rhi::ColorComponentFlags::All,
        }},
        .layout = toneMappingPipelineLayout_,
        .colorAttachmentFormats = {colorFormat_},
        .debugName = "ToneMappingGraphicsPipeline",
    };
    auto tmPipeRes = device_->createGraphicsPipeline(tmPipeDesc);
    if (!tmPipeRes.hasValue()) {
        return tmPipeRes.error();
    }
    toneMappingPipeline_ = tmPipeRes.value();

    return Result<void>::success();
}

Result<void> SceneRenderer::createPostProcessTargets(uint32_t width, uint32_t height) {
    if (device_ == nullptr || width == 0 || height == 0) {
        return Result<void>::success();
    }

    // 1. Fullscreen HDR Scene Target
    rhi::TextureDesc hdrDesc{
        .type = rhi::TextureType::Texture2D,
        .format = hdrFormat_,
        .width = width,
        .height = height,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = rhi::TextureUsageFlags::ColorAttachment | rhi::TextureUsageFlags::Sampled |
                 rhi::TextureUsageFlags::TransferSrc,
        .debugName = "SceneRendererHdrTarget",
    };
    auto hdrRes = device_->createTexture(hdrDesc);
    if (!hdrRes.hasValue()) {
        return hdrRes.error();
    }
    hdrTarget_ = hdrRes.value();

    // 2. Bloom Downsample Pyramid Targets
    for (size_t i = 0; i < kBloomPyramidLevels; ++i) {
        uint32_t downW = std::max(1u, width >> (i + 1));
        uint32_t downH = std::max(1u, height >> (i + 1));
        rhi::TextureDesc downDesc{
            .type = rhi::TextureType::Texture2D,
            .format = hdrFormat_,
            .width = downW,
            .height = downH,
            .depth = 1,
            .mipLevels = 1,
            .arrayLayers = 1,
            .usage = rhi::TextureUsageFlags::ColorAttachment | rhi::TextureUsageFlags::Sampled,
            .debugName = "BloomDownTarget",
        };
        auto downRes = device_->createTexture(downDesc);
        if (!downRes.hasValue()) {
            return downRes.error();
        }
        bloomDownTarget_[i] = downRes.value();
    }

    // 3. Bloom Upsample Targets
    for (size_t i = 0; i < kBloomPyramidLevels - 1; ++i) {
        size_t level = (kBloomPyramidLevels - 2) - i;
        uint32_t upW = std::max(1u, width >> (level + 1));
        uint32_t upH = std::max(1u, height >> (level + 1));
        rhi::TextureDesc upDesc{
            .type = rhi::TextureType::Texture2D,
            .format = hdrFormat_,
            .width = upW,
            .height = upH,
            .depth = 1,
            .mipLevels = 1,
            .arrayLayers = 1,
            .usage = rhi::TextureUsageFlags::ColorAttachment | rhi::TextureUsageFlags::Sampled,
            .debugName = "BloomUpTarget",
        };
        auto upRes = device_->createTexture(upDesc);
        if (!upRes.hasValue()) {
            return upRes.error();
        }
        bloomUpTarget_[level] = upRes.value();
    }

    updatePostProcessDescriptors();

    return Result<void>::success();
}

void SceneRenderer::updatePostProcessDescriptors() {
    if (device_ == nullptr || !postProcessSampler_.isValid() || !hdrTarget_.isValid()) {
        return;
    }

    for (size_t i = 0; i < kBloomPyramidLevels; ++i) {
        if (!bloomDownSets_[i].isValid()) {
            continue;
        }
        rhi::TextureHandle srcTex = (i == 0) ? hdrTarget_ : bloomDownTarget_[i - 1];
        if (srcTex.isValid()) {
            std::array<rhi::DescriptorWrite, 1> writes{
                rhi::DescriptorWrite::makeCombinedImageSampler(0, srcTex, postProcessSampler_),
            };
            std::ignore = device_->updateDescriptorSet(bloomDownSets_[i], writes);
        }
    }

    for (size_t step = 0; step < kBloomPyramidLevels - 1; ++step) {
        if (!bloomUpSets_[step].isValid()) {
            continue;
        }
        rhi::TextureHandle sampleTex = (step == 0) ? bloomDownTarget_[kBloomPyramidLevels - 1]
                                                   : bloomUpTarget_[kBloomPyramidLevels - 1 - step];
        size_t addLevel = (kBloomPyramidLevels - 2) - step;
        rhi::TextureHandle addTex = bloomDownTarget_[addLevel];
        if (sampleTex.isValid() && addTex.isValid()) {
            std::array<rhi::DescriptorWrite, 2> writes{
                rhi::DescriptorWrite::makeCombinedImageSampler(0, sampleTex, postProcessSampler_),
                rhi::DescriptorWrite::makeCombinedImageSampler(1, addTex, postProcessSampler_),
            };
            std::ignore = device_->updateDescriptorSet(bloomUpSets_[step], writes);
        }
    }

    if (toneMappingSet_.isValid() && bloomUpTarget_[0].isValid()) {
        std::array<rhi::DescriptorWrite, 2> writes{
            rhi::DescriptorWrite::makeCombinedImageSampler(0, hdrTarget_, postProcessSampler_),
            rhi::DescriptorWrite::makeCombinedImageSampler(1, bloomUpTarget_[0],
                                                           postProcessSampler_),
        };
        std::ignore = device_->updateDescriptorSet(toneMappingSet_, writes);
    }
}

void SceneRenderer::destroyPostProcessTargets() {
    if (device_ != nullptr) {
        if (hdrTarget_.isValid()) {
            device_->destroyTexture(hdrTarget_);
            hdrTarget_ = {};
        }
        for (size_t i = 0; i < kBloomPyramidLevels; ++i) {
            if (bloomDownTarget_[i].isValid()) {
                device_->destroyTexture(bloomDownTarget_[i]);
                bloomDownTarget_[i] = {};
            }
        }
        for (size_t i = 0; i < kBloomPyramidLevels - 1; ++i) {
            if (bloomUpTarget_[i].isValid()) {
                device_->destroyTexture(bloomUpTarget_[i]);
                bloomUpTarget_[i] = {};
            }
        }
    }
}

void SceneRenderer::renderPostProcess(rhi::RhiCommandList& cmdList) {
    if (device_ == nullptr || !postProcessSettings_.enabled || !hdrTarget_.isValid() ||
        !colorTarget_.isValid()) {
        return;
    }

    const bool hasTimestamps = device_->getCaps().features.timestampQueries;
    const uint32_t baseQuery = (frameSlotIndex_ % 2) * 16;

    if (postProcessSettings_.bloomEnabled && bloomDownPipeline_.isValid() &&
        bloomUpPipeline_.isValid()) {
        if (hasTimestamps) {
            cmdList.writeTimestamp(baseQuery + 6, rhi::GpuTimestampStage::TopOfPipe);
        }
        // 1. Bloom Downsample Pyramid
        cmdList.bindPipeline(bloomDownPipeline_);
        for (size_t i = 0; i < kBloomPyramidLevels; ++i) {
            uint32_t dstW = std::max(1u, width_ >> (i + 1));
            uint32_t dstH = std::max(1u, height_ >> (i + 1));
            uint32_t srcW = (i == 0) ? width_ : std::max(1u, width_ >> i);
            uint32_t srcH = (i == 0) ? height_ : std::max(1u, height_ >> i);

            rhi::RenderPassBeginInfo passInfo{
                .colorTarget = bloomDownTarget_[i],
                .depthTarget = {},
                .clearColor = {0.0F, 0.0F, 0.0F, 1.0F},
                .clearDepth = 1.0F,
                .clearTarget = true,
                .clearDepthTarget = false,
            };
            cmdList.beginRenderPass(passInfo);
            cmdList.setViewport(0.0F, 0.0F, static_cast<float>(dstW), static_cast<float>(dstH));
            cmdList.setScissor(0, 0, dstW, dstH);

            const std::array<rhi::DescriptorSetHandle, 1> downSets = {bloomDownSets_[i]};
            cmdList.bindDescriptorSets(bloomDownPipelineLayout_, 0, downSets);

            BloomDownsamplePushConstants pc{
                .texelSize = Vec2(1.0F / static_cast<float>(srcW), 1.0F / static_cast<float>(srcH)),
                .threshold = postProcessSettings_.bloomThreshold,
                .knee = postProcessSettings_.bloomKnee,
                .isFirstPass = (i == 0) ? 1 : 0,
                .padding = {0.0F, 0.0F, 0.0F},
            };
            cmdList.pushConstants(bloomDownPipelineLayout_, rhi::ShaderStageFlags::Fragment, 0,
                                  sizeof(BloomDownsamplePushConstants), &pc);
            cmdList.draw(3, 1, 0, 0);
            cmdList.endRenderPass();
        }

        if (hasTimestamps) {
            cmdList.writeTimestamp(baseQuery + 7, rhi::GpuTimestampStage::BottomOfPipe);
            cmdList.writeTimestamp(baseQuery + 8, rhi::GpuTimestampStage::TopOfPipe);
        }

        // 2. Bloom Upsample Pyramid
        cmdList.bindPipeline(bloomUpPipeline_);
        for (size_t step = 0; step < kBloomPyramidLevels - 1; ++step) {
            size_t targetLevel = (kBloomPyramidLevels - 2) - step;
            uint32_t dstW = std::max(1u, width_ >> (targetLevel + 1));
            uint32_t dstH = std::max(1u, height_ >> (targetLevel + 1));
            uint32_t srcW = std::max(1u, width_ >> (targetLevel + 2));
            uint32_t srcH = std::max(1u, height_ >> (targetLevel + 2));

            rhi::RenderPassBeginInfo passInfo{
                .colorTarget = bloomUpTarget_[targetLevel],
                .depthTarget = {},
                .clearColor = {0.0F, 0.0F, 0.0F, 1.0F},
                .clearDepth = 1.0F,
                .clearTarget = true,
                .clearDepthTarget = false,
            };
            cmdList.beginRenderPass(passInfo);
            cmdList.setViewport(0.0F, 0.0F, static_cast<float>(dstW), static_cast<float>(dstH));
            cmdList.setScissor(0, 0, dstW, dstH);

            const std::array<rhi::DescriptorSetHandle, 1> upSets = {bloomUpSets_[step]};
            cmdList.bindDescriptorSets(bloomUpPipelineLayout_, 0, upSets);

            BloomUpsamplePushConstants pc{
                .texelSize = Vec2(1.0F / static_cast<float>(srcW), 1.0F / static_cast<float>(srcH)),
                .filterRadius = postProcessSettings_.bloomFilterRadius,
                .blendFactor = 1.0F,
            };
            cmdList.pushConstants(bloomUpPipelineLayout_, rhi::ShaderStageFlags::Fragment, 0,
                                  sizeof(BloomUpsamplePushConstants), &pc);
            cmdList.draw(3, 1, 0, 0);
            cmdList.endRenderPass();
        }

        if (hasTimestamps) {
            cmdList.writeTimestamp(baseQuery + 9, rhi::GpuTimestampStage::BottomOfPipe);
        }
    }

    // 3. Final Tone Mapping Pass
    if (toneMappingPipeline_.isValid()) {
        if (hasTimestamps) {
            cmdList.writeTimestamp(baseQuery + 10, rhi::GpuTimestampStage::TopOfPipe);
        }
        rhi::RenderPassBeginInfo passInfo{
            .colorTarget = colorTarget_,
            .depthTarget = {},
            .clearColor = {0.0F, 0.0F, 0.0F, 1.0F},
            .clearDepth = 1.0F,
            .clearTarget = true,
            .clearDepthTarget = false,
        };
        cmdList.beginRenderPass(passInfo);
        cmdList.setViewport(0.0F, 0.0F, static_cast<float>(width_), static_cast<float>(height_));
        cmdList.setScissor(0, 0, width_, height_);
        cmdList.bindPipeline(toneMappingPipeline_);

        const std::array<rhi::DescriptorSetHandle, 1> tmSets = {toneMappingSet_};
        cmdList.bindDescriptorSets(toneMappingPipelineLayout_, 0, tmSets);

        ToneMappingPushConstants pc{
            .exposure = postProcessSettings_.exposure,
            .toneMapOp = static_cast<int32_t>(postProcessSettings_.toneMapOp),
            .bloomIntensity =
                postProcessSettings_.bloomEnabled ? postProcessSettings_.bloomIntensity : 0.0F,
            .bloomEnabled = postProcessSettings_.bloomEnabled ? 1 : 0,
            .whitePoint = postProcessSettings_.reinhardWhitePoint,
            .padding = {0.0F, 0.0F, 0.0F},
        };
        cmdList.pushConstants(toneMappingPipelineLayout_, rhi::ShaderStageFlags::Fragment, 0,
                              sizeof(ToneMappingPushConstants), &pc);
        cmdList.draw(3, 1, 0, 0);
        cmdList.endRenderPass();

        if (hasTimestamps) {
            cmdList.writeTimestamp(baseQuery + 11, rhi::GpuTimestampStage::BottomOfPipe);
        }
    }

    if (hasTimestamps) {
        cmdList.writeTimestamp(baseQuery + 1, rhi::GpuTimestampStage::BottomOfPipe);
    }
}

void SceneRenderer::updateGpuTimings(rhi::RhiDevice& device) {
    if (!device.getCaps().features.timestampQueries) {
        gpuPassTimings_.valid = false;
        return;
    }

    // Double-buffered query retrieval: inspect query results from prior frame slot
    const uint32_t readbackBase = (((frameSlotIndex_ + 1) % 2)) * 16;

    auto totalOpt = device.getQueryTimeMs(readbackBase + 0, readbackBase + 1);
    if (totalOpt.has_value()) {
        gpuPassTimings_.totalSceneMs = totalOpt.value();
        gpuPassTimings_.shadowPassMs =
            device.getQueryTimeMs(readbackBase + 2, readbackBase + 3).value_or(0.0F);
        gpuPassTimings_.mainPbrPassMs =
            device.getQueryTimeMs(readbackBase + 4, readbackBase + 5).value_or(0.0F);
        gpuPassTimings_.bloomDownsampleMs =
            device.getQueryTimeMs(readbackBase + 6, readbackBase + 7).value_or(0.0F);
        gpuPassTimings_.bloomUpsampleMs =
            device.getQueryTimeMs(readbackBase + 8, readbackBase + 9).value_or(0.0F);
        gpuPassTimings_.toneMappingMs =
            device.getQueryTimeMs(readbackBase + 10, readbackBase + 11).value_or(0.0F);
        gpuPassTimings_.valid = true;
    }

    frameSlotIndex_++;
}

} // namespace eng::render
