/// @file null_rhi_test.cpp
/// @brief Unit tests for RHI abstractions and Null headless backend.

#include "../../../engine/rhi/src/null/null_command_list.h"
#include "core/assert.h"
#include "rhi/buffer.h"
#include "rhi/command_list.h"
#include "rhi/gpu_caps.h"
#include "rhi/pipeline.h"
#include "rhi/rhi_device.h"
#include "rhi/sampler.h"
#include "rhi/shader.h"
#include "rhi/texture.h"
#include "test_shaders.h"

#include <cstring>
#include <iostream>

using namespace eng::rhi;

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__          \
                      << std::endl;                                                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

static int testGpuCapsQuery() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());

    const auto& caps = device->getCaps();
    TEST_CHECK(caps.tier == HardwareTier::Tier0);
    TEST_CHECK(caps.features.dynamicRendering);
    TEST_CHECK(caps.features.timelineSemaphores);
    TEST_CHECK(!caps.features.rayTracing);
    TEST_CHECK(!caps.features.meshShaders);
    TEST_CHECK(caps.limits.maxTextureDimension2D == 4096);
    TEST_CHECK(caps.limits.maxColorAttachments == 8);

    return 0;
}

static int testBufferLifecycleAndGenerationalSafety() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());

    // 1. Invalid size
    BufferDesc invalidDesc{.sizeInBytes = 0};
    auto failRes = device->createBuffer(invalidDesc);
    TEST_CHECK(!failRes.hasValue());

    // 2. Valid creation
    BufferDesc desc{.sizeInBytes = 256,
                    .usage = BufferUsageFlags::Vertex | BufferUsageFlags::TransferDst,
                    .memoryUsage = MemoryUsage::GpuOnly,
                    .debugName = "TestVertexBuffer"};
    auto bufRes = device->createBuffer(desc);
    TEST_CHECK(bufRes.hasValue());
    BufferHandle h1 = bufRes.value();
    TEST_CHECK(h1.isValid());
    TEST_CHECK(h1.index() == 0);
    TEST_CHECK(h1.generation() == 1);

    // 3. Destroy and verify stale handle safety
    device->destroyBuffer(h1);

    // 4. Create new buffer reusing slot 0 with incremented generation
    auto bufRes2 = device->createBuffer(desc);
    TEST_CHECK(bufRes2.hasValue());
    BufferHandle h2 = bufRes2.value();
    TEST_CHECK(h2.isValid());
    TEST_CHECK(h2.index() == 0);
    TEST_CHECK(h2.generation() == 2);

    // Stale handle comparison
    TEST_CHECK(h1 != h2);
    TEST_CHECK(h1.index() == h2.index());
    TEST_CHECK(h1.generation() != h2.generation());

    return 0;
}

static int testTextureAndSamplerLifecycle() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());

    // Texture dimension limit failure
    TextureDesc badDesc{.type = TextureType::Texture2D,
                        .format = TextureFormat::RGBA8_UNORM,
                        .width = 16384, // Exceeds 4096 limit
                        .height = 1024,
                        .depth = 1,
                        .mipLevels = 1,
                        .arrayLayers = 1,
                        .usage = TextureUsageFlags::Sampled};
    auto badTexRes = device->createTexture(badDesc);
    TEST_CHECK(!badTexRes.hasValue());

    // Valid texture
    TextureDesc validDesc = badDesc;
    validDesc.width = 1024;
    auto texRes = device->createTexture(validDesc);
    TEST_CHECK(texRes.hasValue());
    TextureHandle th = texRes.value();
    TEST_CHECK(th.isValid());

    device->destroyTexture(th);

    // Sampler creation
    SamplerDesc sDesc{.minFilter = Filter::Linear,
                      .magFilter = Filter::Linear,
                      .mipmapMode = Filter::Linear,
                      .addressU = SamplerAddressMode::ClampToEdge,
                      .addressV = SamplerAddressMode::ClampToEdge,
                      .addressW = SamplerAddressMode::ClampToEdge,
                      .anisotropyEnable = true,
                      .maxAnisotropy = 8.0F,
                      .minLod = 0.0F,
                      .maxLod = 10.0F,
                      .mipLodBias = 0.5F,
                      .compareEnable = false};
    auto sampRes = device->createSampler(sDesc);
    TEST_CHECK(sampRes.hasValue());
    SamplerHandle sh = sampRes.value();
    TEST_CHECK(sh.isValid());

    // Invalid sampler with minLod > maxLod
    SamplerDesc badLodDesc = sDesc;
    badLodDesc.minLod = 5.0F;
    badLodDesc.maxLod = 2.0F;
    TEST_CHECK(!device->createSampler(badLodDesc).hasValue());

    // Invalid sampler with maxAnisotropy < 1.0F
    SamplerDesc badAnisoDesc = sDesc;
    badAnisoDesc.anisotropyEnable = true;
    badAnisoDesc.maxAnisotropy = 0.5F;
    TEST_CHECK(!device->createSampler(badAnisoDesc).hasValue());

    device->destroySampler(sh);

    return 0;
}

static int testCommandRecordingAndExecution() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());

    // Create src and dst buffers
    BufferDesc bDesc{.sizeInBytes = 64,
                     .usage = BufferUsageFlags::TransferSrc | BufferUsageFlags::TransferDst,
                     .memoryUsage = MemoryUsage::GpuOnly};
    auto srcRes = device->createBuffer(bDesc);
    auto dstRes = device->createBuffer(bDesc);
    TEST_CHECK(srcRes.hasValue() && dstRes.hasValue());
    BufferHandle srcBuf = srcRes.value();
    BufferHandle dstBuf = dstRes.value();

    auto cmdRes = device->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());

    // Recording
    cmd->begin();
    cmd->setViewport(0.0f, 0.0f, 1920.0f, 1080.0f);
    cmd->setScissor(0, 0, 1920, 1080);
    cmd->bindVertexBuffer(0, srcBuf, 0);
    cmd->draw(3, 1, 0, 0);
    cmd->drawIndexed(6, 1, 0, 0, 0);

    const uint8_t testPayload[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    cmd->updateBuffer(srcBuf, 0, testPayload, sizeof(testPayload));
    cmd->copyBuffer(srcBuf, dstBuf, 0, 0, sizeof(testPayload));
    cmd->end();

    // Submission
    auto subRes = device->submit(cmd.get());
    TEST_CHECK(subRes.hasValue());

    device->waitIdle();

    return 0;
}

static int testShaderLifecycleAndValidation() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());

    // 1. Invalid shader stage
    ShaderDesc invalidStageDesc{
        .stage = ShaderStageFlags::None,
        .spirvCode = eng::rhi::test::kTestVertexShaderSpirv,
    };
    auto failStageRes = device->createShader(invalidStageDesc);
    TEST_CHECK(!failStageRes.hasValue());

    // 2. Empty code
    ShaderDesc emptyDesc{
        .stage = ShaderStageFlags::Vertex,
        .spirvCode = {},
    };
    auto failEmptyRes = device->createShader(emptyDesc);
    TEST_CHECK(!failEmptyRes.hasValue());

    // 3. Short binary (< 5 words)
    uint32_t shortWords[] = {0x07230203U, 0x00010000U};
    ShaderDesc shortDesc{
        .stage = ShaderStageFlags::Vertex,
        .spirvCode = shortWords,
    };
    auto failShortRes = device->createShader(shortDesc);
    TEST_CHECK(!failShortRes.hasValue());

    // 4. Invalid magic number
    uint32_t badMagicWords[] = {0x12345678U, 0x00010000U, 0, 0, 0};
    ShaderDesc badMagicDesc{
        .stage = ShaderStageFlags::Vertex,
        .spirvCode = badMagicWords,
    };
    auto failMagicRes = device->createShader(badMagicDesc);
    TEST_CHECK(!failMagicRes.hasValue());

    // 5. Valid vertex shader creation
    ShaderDesc vsDesc{
        .stage = ShaderStageFlags::Vertex,
        .spirvCode = eng::rhi::test::kTestVertexShaderSpirv,
        .entryPoint = "main",
        .debugName = "TestVS",
    };
    auto vsRes = device->createShader(vsDesc);
    TEST_CHECK(vsRes.hasValue());
    ShaderHandle vsHandle = vsRes.value();
    TEST_CHECK(vsHandle.isValid());

    // 6. Valid fragment shader creation
    ShaderDesc fsDesc{
        .stage = ShaderStageFlags::Fragment,
        .spirvCode = eng::rhi::test::kTestFragmentShaderSpirv,
        .entryPoint = "main",
        .debugName = "TestFS",
    };
    auto fsRes = device->createShader(fsDesc);
    TEST_CHECK(fsRes.hasValue());
    ShaderHandle fsHandle = fsRes.value();
    TEST_CHECK(fsHandle.isValid());

    // 7. Destruction and generational invalidation
    device->destroyShader(vsHandle);
    auto vsRes2 = device->createShader(vsDesc);
    TEST_CHECK(vsRes2.hasValue());
    ShaderHandle vsHandle2 = vsRes2.value();
    TEST_CHECK(vsHandle2.index() == vsHandle.index());
    TEST_CHECK(vsHandle2.generation() != vsHandle.generation());

    device->destroyShader(vsHandle2);
    device->destroyShader(fsHandle);

    return 0;
}

static int testPipelineLayoutLifecycleAndValidation() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());

    // 1. Invalid push constant alignment
    PipelineLayoutDesc unalignedDesc{
        .pushConstants = {{.stageFlags = ShaderStageFlags::Vertex, .offset = 3, .size = 16}},
        .descriptorSetLayouts = {},
        .debugName = {},
    };
    auto failUnaligned = device->createPipelineLayout(unalignedDesc);
    TEST_CHECK(!failUnaligned.hasValue());

    // 2. Push constant size > 128
    PipelineLayoutDesc tooBigDesc{
        .pushConstants = {{.stageFlags = ShaderStageFlags::Vertex, .offset = 0, .size = 144}},
        .descriptorSetLayouts = {},
        .debugName = {},
    };
    auto failTooBig = device->createPipelineLayout(tooBigDesc);
    TEST_CHECK(!failTooBig.hasValue());

    // 3. Duplicate descriptor set binding
    PipelineLayoutDesc dupBindingDesc{
        .pushConstants = {},
        .descriptorSetLayouts = {DescriptorSetLayoutDesc{
            .bindings =
                {
                    DescriptorSetLayoutBinding{.binding = 0,
                                               .descriptorType = DescriptorType::UniformBuffer,
                                               .descriptorCount = 1,
                                               .stageFlags = ShaderStageFlags::AllGraphics},
                    DescriptorSetLayoutBinding{.binding = 0,
                                               .descriptorType = DescriptorType::SampledImage,
                                               .descriptorCount = 1,
                                               .stageFlags = ShaderStageFlags::AllGraphics},
                },
        }},
        .debugName = {},
    };
    auto failDup = device->createPipelineLayout(dupBindingDesc);
    TEST_CHECK(!failDup.hasValue());

    // 4. Valid layout creation
    PipelineLayoutDesc validDesc{
        .pushConstants = {{.stageFlags = ShaderStageFlags::Vertex, .offset = 0, .size = 64}},
        .descriptorSetLayouts = {DescriptorSetLayoutDesc{
            .bindings =
                {
                    DescriptorSetLayoutBinding{
                        .binding = 0,
                        .descriptorType = DescriptorType::UniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags = ShaderStageFlags::Vertex,
                    },
                    DescriptorSetLayoutBinding{
                        .binding = 1,
                        .descriptorType = DescriptorType::CombinedImageSampler,
                        .descriptorCount = 1,
                        .stageFlags = ShaderStageFlags::Fragment,
                    },
                },
        }},
        .debugName = "MainPipelineLayout",
    };
    auto layoutRes = device->createPipelineLayout(validDesc);
    TEST_CHECK(layoutRes.hasValue());
    PipelineLayoutHandle layoutHandle = layoutRes.value();
    TEST_CHECK(layoutHandle.isValid());

    // 5. Destruction and reuse
    device->destroyPipelineLayout(layoutHandle);
    auto layoutRes2 = device->createPipelineLayout(validDesc);
    TEST_CHECK(layoutRes2.hasValue());
    PipelineLayoutHandle layoutHandle2 = layoutRes2.value();
    TEST_CHECK(layoutHandle2.index() == layoutHandle.index());
    TEST_CHECK(layoutHandle2.generation() != layoutHandle.generation());

    device->destroyPipelineLayout(layoutHandle2);

    return 0;
}

static int testGraphicsPipelineLifecycleAndValidation() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());

    // Setup shaders and layout
    ShaderDesc vsDesc{
        .stage = ShaderStageFlags::Vertex,
        .spirvCode = eng::rhi::test::kTestVertexShaderSpirv,
    };
    auto vsRes = device->createShader(vsDesc);
    TEST_CHECK(vsRes.hasValue());
    ShaderHandle vsHandle = vsRes.value();

    ShaderDesc fsDesc{
        .stage = ShaderStageFlags::Fragment,
        .spirvCode = eng::rhi::test::kTestFragmentShaderSpirv,
    };
    auto fsRes = device->createShader(fsDesc);
    TEST_CHECK(fsRes.hasValue());
    ShaderHandle fsHandle = fsRes.value();

    PipelineLayoutDesc layoutDesc{
        .pushConstants = {{.stageFlags = ShaderStageFlags::Vertex, .offset = 0, .size = 64}},
        .descriptorSetLayouts = {},
        .debugName = {},
    };
    auto layoutRes = device->createPipelineLayout(layoutDesc);
    TEST_CHECK(layoutRes.hasValue());
    PipelineLayoutHandle layoutHandle = layoutRes.value();

    // 1. Missing vertex shader
    GraphicsPipelineDesc missingVsDesc{
        .vertexShader = {},
        .fragmentShader = fsHandle,
        .layout = layoutHandle,
        .colorAttachmentFormats = {TextureFormat::RGBA8_UNORM},
    };
    auto failMissingVs = device->createGraphicsPipeline(missingVsDesc);
    TEST_CHECK(!failMissingVs.hasValue());

    // 2. Incompatible stage (passing fragment shader as vertex shader)
    GraphicsPipelineDesc wrongStageDesc{
        .vertexShader = fsHandle,
        .fragmentShader = fsHandle,
        .layout = layoutHandle,
        .colorAttachmentFormats = {TextureFormat::RGBA8_UNORM},
    };
    auto failWrongStage = device->createGraphicsPipeline(wrongStageDesc);
    TEST_CHECK(!failWrongStage.hasValue());

    // 3. Invalid vertex attribute referencing non-existent binding
    GraphicsPipelineDesc badAttrDesc{
        .vertexShader = vsHandle,
        .fragmentShader = fsHandle,
        .vertexInput =
            {
                .bindings = {{.binding = 0, .stride = 32}},
                .attributes = {{.location = 0, .binding = 1, .format = VertexFormat::Float3}},
            },
        .layout = layoutHandle,
        .colorAttachmentFormats = {TextureFormat::RGBA8_UNORM},
    };
    auto failBadAttr = device->createGraphicsPipeline(badAttrDesc);
    TEST_CHECK(!failBadAttr.hasValue());

    // 4. Missing attachments
    GraphicsPipelineDesc noAttachDesc{
        .vertexShader = vsHandle,
        .fragmentShader = fsHandle,
        .layout = layoutHandle,
        .colorAttachmentFormats = {},
        .depthAttachmentFormat = TextureFormat::Undefined,
    };
    auto failNoAttach = device->createGraphicsPipeline(noAttachDesc);
    TEST_CHECK(!failNoAttach.hasValue());

    // 5. Valid Graphics Pipeline Creation
    GraphicsPipelineDesc validPipelineDesc{
        .vertexShader = vsHandle,
        .fragmentShader = fsHandle,
        .vertexInput =
            {
                .bindings = {{.binding = 0, .stride = 32, .inputRate = VertexInputRate::Vertex}},
                .attributes =
                    {
                        {.location = 0, .binding = 0, .format = VertexFormat::Float3, .offset = 0},
                        {.location = 1, .binding = 0, .format = VertexFormat::Float3, .offset = 12},
                        {.location = 2, .binding = 0, .format = VertexFormat::Float2, .offset = 24},
                    },
            },
        .topology = PrimitiveTopology::TriangleList,
        .rasterization =
            {
                .polygonMode = PolygonMode::Fill,
                .cullMode = CullMode::Back,
                .frontFace = FrontFace::CounterClockwise,
            },
        .depthStencil =
            {
                .depthTestEnable = true,
                .depthWriteEnable = true,
                .depthCompareOp = CompareOp::Less,
            },
        .layout = layoutHandle,
        .colorAttachmentFormats = {TextureFormat::RGBA8_UNORM},
        .depthAttachmentFormat = TextureFormat::D32_FLOAT,
        .debugName = "MeshPipeline",
    };
    auto pipeRes = device->createGraphicsPipeline(validPipelineDesc);
    TEST_CHECK(pipeRes.hasValue());
    PipelineHandle pipeHandle = pipeRes.value();
    TEST_CHECK(pipeHandle.isValid());

    // 6. Destruction and generational reuse
    device->destroyGraphicsPipeline(pipeHandle);
    auto pipeRes2 = device->createGraphicsPipeline(validPipelineDesc);
    TEST_CHECK(pipeRes2.hasValue());
    PipelineHandle pipeHandle2 = pipeRes2.value();
    TEST_CHECK(pipeHandle2.index() == pipeHandle.index());
    TEST_CHECK(pipeHandle2.generation() != pipeHandle.generation());

    device->destroyGraphicsPipeline(pipeHandle2);
    device->destroyPipelineLayout(layoutHandle);
    device->destroyShader(fsHandle);
    device->destroyShader(vsHandle);

    return 0;
}

static int testCommandListPipelineBindingAndValidation() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());

    // Setup pipeline
    ShaderDesc vsDesc{
        .stage = ShaderStageFlags::Vertex,
        .spirvCode = eng::rhi::test::kTestVertexShaderSpirv,
    };
    auto vsRes = device->createShader(vsDesc);
    ShaderDesc fsDesc{
        .stage = ShaderStageFlags::Fragment,
        .spirvCode = eng::rhi::test::kTestFragmentShaderSpirv,
    };
    auto fsRes = device->createShader(fsDesc);

    PipelineLayoutDesc layoutDesc{
        .pushConstants = {{.stageFlags = ShaderStageFlags::Vertex, .offset = 0, .size = 64}},
        .descriptorSetLayouts = {},
        .debugName = {},
    };
    auto layoutRes = device->createPipelineLayout(layoutDesc);

    GraphicsPipelineDesc pipeDesc{
        .vertexShader = vsRes.value(),
        .fragmentShader = fsRes.value(),
        .layout = layoutRes.value(),
        .colorAttachmentFormats = {TextureFormat::RGBA8_UNORM},
    };
    auto pipeRes = device->createGraphicsPipeline(pipeDesc);
    PipelineHandle pipeline = pipeRes.value();

    auto cmdRes = device->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());

    cmd->begin();

    // 1. Draw without pipeline bound -> flagged as unbound draw
    cmd->draw(3, 1, 0, 0);

    // 2. Bind valid pipeline
    cmd->bindPipeline(pipeline);

    // 3. Push constants with valid layout
    float matrix[16] = {1.0f};
    cmd->pushConstants(layoutRes.value(), ShaderStageFlags::Vertex, 0, sizeof(matrix), matrix);

    // 4. Draw with pipeline bound
    cmd->drawIndexed(6, 1, 0, 0, 0);

    // 5. Bind invalid pipeline handle
    cmd->bindPipeline(PipelineHandle{});

    // 6. Bind destroyed pipeline handle
    device->destroyGraphicsPipeline(pipeline);
    cmd->bindPipeline(pipeline);

    cmd->end();

    auto subRes = device->submit(cmd.get());
    TEST_CHECK(subRes.hasValue());

    device->destroyPipelineLayout(layoutRes.value());
    device->destroyShader(fsRes.value());
    device->destroyShader(vsRes.value());

    return 0;
}

static int testOffscreenTextureLifecycleAndValidation() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());

    // 1. Valid offscreen color attachment
    TextureDesc colorDesc{
        .type = TextureType::Texture2D,
        .format = TextureFormat::RGBA8_UNORM,
        .width = 1280,
        .height = 720,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = TextureUsageFlags::ColorAttachment | TextureUsageFlags::Sampled,
        .debugName = "OffscreenColorTarget",
    };
    auto colorRes = device->createTexture(colorDesc);
    TEST_CHECK(colorRes.hasValue());
    TextureHandle colorTex = colorRes.value();
    TEST_CHECK(colorTex.isValid());

    const auto* queriedColorDesc = device->getTextureDesc(colorTex);
    TEST_CHECK(queriedColorDesc != nullptr);
    TEST_CHECK(queriedColorDesc->width == 1280);
    TEST_CHECK(queriedColorDesc->height == 720);
    TEST_CHECK(queriedColorDesc->format == TextureFormat::RGBA8_UNORM);

    // 2. Valid offscreen depth attachment
    TextureDesc depthDesc{
        .type = TextureType::Texture2D,
        .format = TextureFormat::D32_FLOAT,
        .width = 1280,
        .height = 720,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = TextureUsageFlags::DepthStencilAttachment,
        .debugName = "OffscreenDepthTarget",
    };
    auto depthRes = device->createTexture(depthDesc);
    TEST_CHECK(depthRes.hasValue());
    TextureHandle depthTex = depthRes.value();
    TEST_CHECK(depthTex.isValid());

    const auto* queriedDepthDesc = device->getTextureDesc(depthTex);
    TEST_CHECK(queriedDepthDesc != nullptr);
    TEST_CHECK(queriedDepthDesc->width == 1280);
    TEST_CHECK(queriedDepthDesc->height == 720);
    TEST_CHECK(queriedDepthDesc->format == TextureFormat::D32_FLOAT);

    // 3. Validation negative tests
    // 3a. Undefined format
    TextureDesc undefDesc = colorDesc;
    undefDesc.format = TextureFormat::Undefined;
    TEST_CHECK(!device->createTexture(undefDesc).hasValue());

    // 3b. Zero width
    TextureDesc zeroWidthDesc = colorDesc;
    zeroWidthDesc.width = 0;
    TEST_CHECK(!device->createTexture(zeroWidthDesc).hasValue());

    // 3c. Zero height
    TextureDesc zeroHeightDesc = colorDesc;
    zeroHeightDesc.height = 0;
    TEST_CHECK(!device->createTexture(zeroHeightDesc).hasValue());

    // 3d. Dimension exceeding limit (limit is 4096 on NullDevice)
    TextureDesc overflowDesc = colorDesc;
    overflowDesc.width = 8192;
    TEST_CHECK(!device->createTexture(overflowDesc).hasValue());

    // 4. Destruction and handle invalidation
    device->destroyTexture(colorTex);
    TEST_CHECK(device->getTextureDesc(colorTex) == nullptr);

    device->destroyTexture(depthTex);
    TEST_CHECK(device->getTextureDesc(depthTex) == nullptr);

    return 0;
}

static int testOffscreenDynamicRendering() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());

    // Create color target (1280x720)
    TextureDesc colorDesc{
        .type = TextureType::Texture2D,
        .format = TextureFormat::RGBA8_UNORM,
        .width = 1280,
        .height = 720,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = TextureUsageFlags::ColorAttachment | TextureUsageFlags::Sampled,
    };
    auto colorTex = device->createTexture(colorDesc).value();

    // Create depth target (1280x720)
    TextureDesc depthDesc{
        .type = TextureType::Texture2D,
        .format = TextureFormat::D32_FLOAT,
        .width = 1280,
        .height = 720,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = TextureUsageFlags::DepthStencilAttachment,
    };
    auto depthTex = device->createTexture(depthDesc).value();

    // Create mismatched depth target (800x600)
    TextureDesc badDepthDesc = depthDesc;
    badDepthDesc.width = 800;
    badDepthDesc.height = 600;
    auto badDepthTex = device->createTexture(badDepthDesc).value();

    auto cmdRes = device->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());
    auto* nullCmd = static_cast<NullCommandList*>(cmd.get());

    // Test 1: Color-only offscreen render pass
    cmd->begin();
    RenderPassBeginInfo colorOnlyPass{
        .colorTarget = colorTex,
        .clearColor = {0.1F, 0.2F, 0.3F, 1.0F},
    };
    cmd->beginRenderPass(colorOnlyPass);
    TEST_CHECK(nullCmd->isInsideRenderPass());
    TEST_CHECK(nullCmd->getActiveColorTarget() == colorTex);
    TEST_CHECK(!nullCmd->getActiveDepthTarget().isValid());
    TEST_CHECK(nullCmd->getActiveColorFormat() == TextureFormat::RGBA8_UNORM);
    TEST_CHECK(nullCmd->getActiveDepthFormat() == TextureFormat::Undefined);
    TEST_CHECK(nullCmd->getActiveWidth() == 1280);
    TEST_CHECK(nullCmd->getActiveHeight() == 720);
    cmd->endRenderPass();
    TEST_CHECK(!nullCmd->isInsideRenderPass());
    cmd->end();

    // Test 2: Color + Depth offscreen render pass
    cmd->begin();
    RenderPassBeginInfo colorDepthPass{
        .colorTarget = colorTex,
        .depthTarget = depthTex,
        .clearDepth = 1.0F,
        .clearDepthTarget = true,
    };
    cmd->beginRenderPass(colorDepthPass);
    TEST_CHECK(nullCmd->isInsideRenderPass());
    TEST_CHECK(nullCmd->getActiveColorTarget() == colorTex);
    TEST_CHECK(nullCmd->getActiveDepthTarget() == depthTex);
    TEST_CHECK(nullCmd->getActiveColorFormat() == TextureFormat::RGBA8_UNORM);
    TEST_CHECK(nullCmd->getActiveDepthFormat() == TextureFormat::D32_FLOAT);
    cmd->endRenderPass();
    TEST_CHECK(!nullCmd->isInsideRenderPass());
    cmd->end();

    // Test 3: Backward compatible Swapchain render pass (colorTarget is default invalid handle)
    cmd->begin();
    RenderPassBeginInfo swapchainPass{
        .colorTarget = {},
    };
    cmd->beginRenderPass(swapchainPass);
    TEST_CHECK(nullCmd->isInsideRenderPass());
    TEST_CHECK(!nullCmd->getActiveColorTarget().isValid());
    cmd->endRenderPass();
    TEST_CHECK(!nullCmd->isInsideRenderPass());
    cmd->end();

    // Negative Test 4: Invalid color attachment handle (destroyed)
    auto tempTex = device->createTexture(colorDesc).value();
    device->destroyTexture(tempTex);
    cmd->begin();
    RenderPassBeginInfo invalidColorPass{
        .colorTarget = tempTex,
    };
    cmd->beginRenderPass(invalidColorPass);
    TEST_CHECK(nullCmd->hasInvalidAttachment());
    cmd->endRenderPass();
    cmd->end();

    // Negative Test 5: Dimension mismatch between color and depth
    cmd->begin();
    RenderPassBeginInfo mismatchPass{
        .colorTarget = colorTex,
        .depthTarget = badDepthTex,
    };
    cmd->beginRenderPass(mismatchPass);
    TEST_CHECK(nullCmd->hasDimensionMismatch());
    cmd->endRenderPass();
    cmd->end();

    // Negative Test 6: Nested beginRenderPass
    cmd->begin();
    cmd->beginRenderPass(colorDepthPass);
    cmd->beginRenderPass(colorDepthPass);
    TEST_CHECK(nullCmd->hasNestedRenderPass());
    cmd->endRenderPass();
    cmd->end();

    // Negative Test 7: Orphan endRenderPass
    cmd->begin();
    cmd->endRenderPass();
    TEST_CHECK(nullCmd->hasOrphanEndPass());
    cmd->end();

    device->destroyTexture(badDepthTex);
    device->destroyTexture(depthTex);
    device->destroyTexture(colorTex);

    return 0;
}

static int testOffscreenPipelineCompatibility() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());

    ShaderDesc vsDesc{
        .stage = ShaderStageFlags::Vertex,
        .spirvCode = eng::rhi::test::kTestVertexShaderSpirv,
    };
    auto vsRes = device->createShader(vsDesc);
    ShaderDesc fsDesc{
        .stage = ShaderStageFlags::Fragment,
        .spirvCode = eng::rhi::test::kTestFragmentShaderSpirv,
    };
    auto fsRes = device->createShader(fsDesc);

    PipelineLayoutDesc layoutDesc{};
    auto layoutRes = device->createPipelineLayout(layoutDesc);

    // Create pipeline configured for RGBA8_UNORM color attachment and D32_FLOAT depth attachment
    GraphicsPipelineDesc pipeDesc{
        .vertexShader = vsRes.value(),
        .fragmentShader = fsRes.value(),
        .layout = layoutRes.value(),
        .colorAttachmentFormats = {TextureFormat::RGBA8_UNORM},
        .depthAttachmentFormat = TextureFormat::D32_FLOAT,
    };
    auto pipeRes = device->createGraphicsPipeline(pipeDesc);
    TEST_CHECK(pipeRes.hasValue());
    PipelineHandle pipeline = pipeRes.value();

    // Create matching color & depth textures
    TextureDesc colorDesc{
        .type = TextureType::Texture2D,
        .format = TextureFormat::RGBA8_UNORM,
        .width = 800,
        .height = 600,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = TextureUsageFlags::ColorAttachment,
    };
    auto colorTex = device->createTexture(colorDesc).value();

    TextureDesc depthDesc{
        .type = TextureType::Texture2D,
        .format = TextureFormat::D32_FLOAT,
        .width = 800,
        .height = 600,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = TextureUsageFlags::DepthStencilAttachment,
    };
    auto depthTex = device->createTexture(depthDesc).value();

    // Create mismatched color texture (BGRA8_UNORM)
    TextureDesc bgraColorDesc = colorDesc;
    bgraColorDesc.format = TextureFormat::BGRA8_UNORM;
    auto bgraColorTex = device->createTexture(bgraColorDesc).value();

    // Create mismatched depth texture (D24_UNORM_S8_UINT)
    TextureDesc d24DepthDesc = depthDesc;
    d24DepthDesc.format = TextureFormat::D24_UNORM_S8_UINT;
    auto d24DepthTex = device->createTexture(d24DepthDesc).value();

    auto cmdRes = device->createCommandList();
    auto cmd = std::move(cmdRes.value());
    auto* nullCmd = static_cast<NullCommandList*>(cmd.get());

    // 1. Draw with matching pipeline and attachments -> should NOT have format mismatch
    cmd->begin();
    RenderPassBeginInfo matchPass{
        .colorTarget = colorTex,
        .depthTarget = depthTex,
    };
    cmd->beginRenderPass(matchPass);
    cmd->bindPipeline(pipeline);
    cmd->draw(3, 1, 0, 0);
    cmd->drawIndexed(6, 1, 0, 0, 0);
    TEST_CHECK(!nullCmd->hasFormatMismatch());
    TEST_CHECK(nullCmd->getDrawCount() == 1);
    TEST_CHECK(nullCmd->getDrawIndexedCount() == 1);
    cmd->endRenderPass();
    cmd->end();

    // 2. Draw with mismatched color format -> should flag format mismatch and SUPPRESS draw
    cmd->begin();
    RenderPassBeginInfo colorMismatchPass{
        .colorTarget = bgraColorTex,
        .depthTarget = depthTex,
    };
    cmd->beginRenderPass(colorMismatchPass);
    cmd->bindPipeline(pipeline);
    cmd->draw(3, 1, 0, 0);
    TEST_CHECK(nullCmd->hasFormatMismatch());
    TEST_CHECK(nullCmd->getDrawCount() == 0); // Suppressed! Not recorded!
    cmd->endRenderPass();
    cmd->end();

    // 3. Draw with mismatched depth format -> should flag format mismatch and SUPPRESS drawIndexed
    cmd->begin();
    RenderPassBeginInfo depthMismatchPass{
        .colorTarget = colorTex,
        .depthTarget = d24DepthTex,
    };
    cmd->beginRenderPass(depthMismatchPass);
    cmd->bindPipeline(pipeline);
    cmd->drawIndexed(6, 1, 0, 0, 0);
    TEST_CHECK(nullCmd->hasFormatMismatch());
    TEST_CHECK(nullCmd->getDrawIndexedCount() == 0); // Suppressed! Not recorded!
    cmd->endRenderPass();
    cmd->end();

    // Clean up
    device->destroyTexture(d24DepthTex);
    device->destroyTexture(bgraColorTex);
    device->destroyTexture(depthTex);
    device->destroyTexture(colorTex);
    device->destroyGraphicsPipeline(pipeline);
    device->destroyPipelineLayout(layoutRes.value());
    device->destroyShader(fsRes.value());
    device->destroyShader(vsRes.value());

    return 0;
}

static int testBufferReadAndTextureCopy() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());

    BufferDesc bDesc{
        .sizeInBytes = 256,
        .usage = BufferUsageFlags::TransferDst,
        .memoryUsage = MemoryUsage::CpuToGpu,
    };
    auto bufRes = device->createBuffer(bDesc);
    TEST_CHECK(bufRes.hasValue());
    BufferHandle buf = bufRes.value();

    uint8_t writeBytes[4] = {0x11, 0x22, 0x33, 0x44};
    TEST_CHECK(device->writeBuffer(buf, 0, writeBytes, sizeof(writeBytes)).hasValue());

    uint8_t readBytes[4] = {0};
    TEST_CHECK(device->readBuffer(buf, 0, readBytes, sizeof(readBytes)).hasValue());
    TEST_CHECK(std::memcmp(writeBytes, readBytes, sizeof(writeBytes)) == 0);

    // Negative tests for readBuffer
    TEST_CHECK(!device->readBuffer(buf, 0, nullptr, 4).hasValue());
    TEST_CHECK(!device->readBuffer(buf, 250, readBytes, 10).hasValue());

    TextureDesc tDesc{
        .type = TextureType::Texture2D,
        .format = TextureFormat::RGBA8_UNORM,
        .width = 64,
        .height = 64,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = TextureUsageFlags::ColorAttachment | TextureUsageFlags::TransferSrc,
    };
    auto texRes = device->createTexture(tDesc);
    TEST_CHECK(texRes.hasValue());
    TextureHandle tex = texRes.value();

    auto cmdRes = device->createCommandList();
    auto cmd = std::move(cmdRes.value());
    cmd->begin();
    cmd->copyTextureToBuffer(tex, buf);
    cmd->end();

    device->destroyTexture(tex);
    device->destroyBuffer(buf);
    return 0;
}

static int testDescriptorLifecycleAndBinding() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());

    // 1. Descriptor Layout Creation and Validation
    DescriptorSetLayoutDesc invalidDescDuplicate{
        .bindings =
            {
                DescriptorBindingDesc{.binding = 0,
                                      .descriptorType = DescriptorType::UniformBuffer,
                                      .descriptorCount = 1,
                                      .stageFlags = ShaderStageFlags::Vertex},
                DescriptorBindingDesc{.binding = 0,
                                      .descriptorType = DescriptorType::UniformBuffer,
                                      .descriptorCount = 1,
                                      .stageFlags = ShaderStageFlags::Fragment},
            },
    };
    TEST_CHECK(!device->createDescriptorSetLayout(invalidDescDuplicate).hasValue());

    DescriptorSetLayoutDesc invalidDescZeroCount{
        .bindings =
            {
                DescriptorBindingDesc{.binding = 0,
                                      .descriptorType = DescriptorType::UniformBuffer,
                                      .descriptorCount = 0,
                                      .stageFlags = ShaderStageFlags::Vertex},
            },
    };
    TEST_CHECK(!device->createDescriptorSetLayout(invalidDescZeroCount).hasValue());

    DescriptorSetLayoutDesc invalidDescNoStage{
        .bindings =
            {
                DescriptorBindingDesc{.binding = 0,
                                      .descriptorType = DescriptorType::UniformBuffer,
                                      .descriptorCount = 1,
                                      .stageFlags = ShaderStageFlags::None},
            },
    };
    TEST_CHECK(!device->createDescriptorSetLayout(invalidDescNoStage).hasValue());

    DescriptorSetLayoutDesc validLayoutDesc{
        .bindings =
            {
                DescriptorBindingDesc{.binding = 0,
                                      .descriptorType = DescriptorType::UniformBuffer,
                                      .descriptorCount = 1,
                                      .stageFlags = ShaderStageFlags::Vertex},
                DescriptorBindingDesc{.binding = 1,
                                      .descriptorType = DescriptorType::CombinedImageSampler,
                                      .descriptorCount = 1,
                                      .stageFlags = ShaderStageFlags::Fragment},
            },
        .debugName = "TestDescriptorLayout",
    };
    auto layoutRes = device->createDescriptorSetLayout(validLayoutDesc);
    TEST_CHECK(layoutRes.hasValue());
    DescriptorSetLayoutHandle layoutHandle = layoutRes.value();
    TEST_CHECK(layoutHandle.isValid());

    // 2. Descriptor Pool Lifecycle and Capacity
    DescriptorPoolDesc invalidPoolZeroSets{
        .maxSets = 0,
        .poolSizes = {{.type = DescriptorType::UniformBuffer, .count = 2}},
    };
    TEST_CHECK(!device->createDescriptorPool(invalidPoolZeroSets).hasValue());

    DescriptorPoolDesc invalidPoolEmptySizes{
        .maxSets = 2,
        .poolSizes = {},
    };
    TEST_CHECK(!device->createDescriptorPool(invalidPoolEmptySizes).hasValue());

    DescriptorPoolDesc poolDesc{
        .maxSets = 2,
        .poolSizes =
            {
                {.type = DescriptorType::UniformBuffer, .count = 2},
                {.type = DescriptorType::CombinedImageSampler, .count = 2},
            },
        .debugName = "TestDescriptorPool",
    };
    auto poolRes = device->createDescriptorPool(poolDesc);
    TEST_CHECK(poolRes.hasValue());
    DescriptorPoolHandle poolHandle = poolRes.value();
    TEST_CHECK(poolHandle.isValid());

    // Allocate up to maxSets
    auto setRes1 = device->allocateDescriptorSet(poolHandle, layoutHandle);
    TEST_CHECK(setRes1.hasValue());
    DescriptorSetHandle set1 = setRes1.value();
    TEST_CHECK(set1.isValid());

    auto setRes2 = device->allocateDescriptorSet(poolHandle, layoutHandle);
    TEST_CHECK(setRes2.hasValue());
    DescriptorSetHandle set2 = setRes2.value();
    TEST_CHECK(set2.isValid());

    // Exceed capacity
    auto setRes3 = device->allocateDescriptorSet(poolHandle, layoutHandle);
    TEST_CHECK(!setRes3.hasValue());
    TEST_CHECK(setRes3.error().code == eng::ErrorCode::OutOfMemory);

    // 3. Resource Creation and Descriptor Updates
    BufferDesc uboDesc{
        .sizeInBytes = 256,
        .usage = BufferUsageFlags::Uniform | BufferUsageFlags::TransferDst,
        .memoryUsage = MemoryUsage::CpuToGpu,
    };
    auto uboRes = device->createBuffer(uboDesc);
    TEST_CHECK(uboRes.hasValue());
    BufferHandle ubo = uboRes.value();

    TextureDesc texDesc{
        .type = TextureType::Texture2D,
        .format = TextureFormat::RGBA8_UNORM,
        .width = 64,
        .height = 64,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = TextureUsageFlags::Sampled | TextureUsageFlags::TransferDst,
    };
    auto texRes = device->createTexture(texDesc);
    TEST_CHECK(texRes.hasValue());
    TextureHandle tex = texRes.value();

    SamplerDesc smpDesc{
        .minFilter = Filter::Linear,
        .magFilter = Filter::Linear,
    };
    auto smpRes = device->createSampler(smpDesc);
    TEST_CHECK(smpRes.hasValue());
    SamplerHandle smp = smpRes.value();

    // Reject updates with invalid buffer handle or out-of-bounds offset/range
    DescriptorWrite badBufferWrite =
        DescriptorWrite::makeBuffer(0, DescriptorType::UniformBuffer, BufferHandle{}, 0, 128);
    TEST_CHECK(!device->updateDescriptorSet(set1, std::span{&badBufferWrite, 1}).hasValue());

    DescriptorWrite oobOffsetWrite =
        DescriptorWrite::makeBuffer(0, DescriptorType::UniformBuffer, ubo, 300, 64);
    TEST_CHECK(!device->updateDescriptorSet(set1, std::span{&oobOffsetWrite, 1}).hasValue());

    DescriptorWrite oobRangeWrite =
        DescriptorWrite::makeBuffer(0, DescriptorType::UniformBuffer, ubo, 200, 100);
    TEST_CHECK(!device->updateDescriptorSet(set1, std::span{&oobRangeWrite, 1}).hasValue());

    // Reject update on undeclared binding or mismatched descriptor type
    DescriptorWrite undeclaredWrite =
        DescriptorWrite::makeBuffer(7, DescriptorType::UniformBuffer, ubo, 0, 64);
    TEST_CHECK(!device->updateDescriptorSet(set1, std::span{&undeclaredWrite, 1}).hasValue());

    DescriptorWrite typeMismatchWrite = DescriptorWrite::makeCombinedImageSampler(0, tex, smp);
    TEST_CHECK(!device->updateDescriptorSet(set1, std::span{&typeMismatchWrite, 1}).hasValue());

    // Valid descriptor writes
    DescriptorWrite validWrites[] = {
        DescriptorWrite::makeBuffer(0, DescriptorType::UniformBuffer, ubo, 0, 256),
        DescriptorWrite::makeCombinedImageSampler(1, tex, smp),
    };
    TEST_CHECK(device->updateDescriptorSet(set1, validWrites).hasValue());

    // 4. Pipeline Layout Integration and Command List Binding
    PipelineLayoutDesc pipeLayoutDesc{
        .pushConstants =
            {
                PushConstantRange{.stageFlags = ShaderStageFlags::Vertex, .offset = 0, .size = 64},
            },
        .descriptorSetLayoutHandles = {layoutHandle},
        .debugName = "TestPipelineLayoutWithDescriptors",
    };
    auto pipeLayoutRes = device->createPipelineLayout(pipeLayoutDesc);
    TEST_CHECK(pipeLayoutRes.hasValue());
    PipelineLayoutHandle pipeLayout = pipeLayoutRes.value();

    auto cmdRes = device->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());
    cmd->begin();

    DescriptorSetHandle setsToBind[] = {set1};
    cmd->bindDescriptorSets(pipeLayout, 0, setsToBind);
    uint32_t pcData[16] = {1, 2, 3, 4};
    cmd->pushConstants(pipeLayout, ShaderStageFlags::Vertex, 0, 64, pcData);
    cmd->end();

    auto* nullCmd = static_cast<NullCommandList*>(cmd.get());
    if (nullCmd != nullptr) {
        TEST_CHECK(nullCmd->getBindDescriptorSetsCount() == 1);
        TEST_CHECK(nullCmd->getPushConstantCount() == 1);
        TEST_CHECK(nullCmd->getDescriptorSetBindings().size() == 1);
        TEST_CHECK(nullCmd->getDescriptorSetBindings()[0].sets.size() == 1);
        TEST_CHECK(nullCmd->getDescriptorSetBindings()[0].sets[0] == set1);
    }

    // 5. Pool Reset and Invalidation
    TEST_CHECK(device->resetDescriptorPool(poolHandle).hasValue());
    // set1 should now be invalidated
    DescriptorWrite afterResetWrite =
        DescriptorWrite::makeBuffer(0, DescriptorType::UniformBuffer, ubo, 0, 256);
    TEST_CHECK(!device->updateDescriptorSet(set1, std::span{&afterResetWrite, 1}).hasValue());

    // Allocation should succeed again after reset
    auto setAfterReset = device->allocateDescriptorSet(poolHandle, layoutHandle);
    TEST_CHECK(setAfterReset.hasValue());

    // 6. Destruction Cleanup
    device->destroyDescriptorPool(poolHandle);
    // Allocated sets from destroyed pool are now invalid
    TEST_CHECK(!device->updateDescriptorSet(setAfterReset.value(), std::span{&afterResetWrite, 1})
                    .hasValue());

    device->destroyPipelineLayout(pipeLayout);
    device->destroyDescriptorSetLayout(layoutHandle);
    device->destroySampler(smp);
    device->destroyTexture(tex);
    device->destroyBuffer(ubo);

    return 0;
}

static int testTextureTransfersAndLayoutTransitions() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());

    const uint32_t width = 16;
    const uint32_t height = 16;
    const size_t byteSize = width * height * 4;

    TextureDesc texDesc{
        .type = TextureType::Texture2D,
        .format = TextureFormat::RGBA8_UNORM,
        .width = width,
        .height = height,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = TextureUsageFlags::Sampled | TextureUsageFlags::TransferDst,
        .debugName = "TestTexture",
    };
    auto texRes = device->createTexture(texDesc);
    TEST_CHECK(texRes.hasValue());
    TextureHandle tex = texRes.value();

    BufferDesc bufDesc{
        .sizeInBytes = byteSize,
        .usage = BufferUsageFlags::TransferSrc,
        .memoryUsage = MemoryUsage::CpuToGpu,
        .debugName = "TestStagingBuffer",
    };
    auto bufRes = device->createBuffer(bufDesc);
    TEST_CHECK(bufRes.hasValue());
    BufferHandle buf = bufRes.value();

    auto cmdRes = device->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());

    cmd->begin();
    cmd->transitionTextureLayout(tex, TextureLayout::Undefined, TextureLayout::TransferDst, 0, 1, 0,
                                 1);
    cmd->copyBufferToTexture(buf, tex, 0, 0, 0);
    cmd->transitionTextureLayout(tex, TextureLayout::TransferDst, TextureLayout::ShaderReadOnly, 0,
                                 1, 0, 1);

    std::vector<uint8_t> pixelData(byteSize, 0x7F);
    cmd->updateTexture(tex, pixelData.data(), pixelData.size(), 0, 0);
    cmd->generateMipmaps(tex);
    cmd->end();

    auto* nullCmd = static_cast<NullCommandList*>(cmd.get());
    TEST_CHECK(nullCmd->getTransitionTextureLayoutCommands().size() == 2);
    TEST_CHECK(nullCmd->getCopyBufferToTextureCommands().size() == 1);
    TEST_CHECK(nullCmd->getUpdateTextureCommands().size() == 1);
    TEST_CHECK(nullCmd->getGenerateMipmapsCommands().size() == 1);

    TEST_CHECK(device->submit(cmd.get()).hasValue());

    device->destroyBuffer(buf);
    device->destroyTexture(tex);
    return 0;
}

static int testTextureFormatCapabilities() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto dev = std::move(devRes.value());

    TEST_CHECK(dev->isTextureFormatSupported(TextureFormat::RGBA16_FLOAT,
                                             TextureUsageFlags::ColorAttachment |
                                                 TextureUsageFlags::Sampled));
    TEST_CHECK(dev->isTextureFormatSupported(TextureFormat::RGBA8_UNORM,
                                             TextureUsageFlags::ColorAttachment |
                                                 TextureUsageFlags::Sampled));
    TEST_CHECK(dev->isTextureFormatSupported(TextureFormat::D32_FLOAT,
                                             TextureUsageFlags::DepthStencilAttachment));
    TEST_CHECK(!dev->isTextureFormatSupported(TextureFormat::Undefined,
                                              TextureUsageFlags::ColorAttachment));
    return 0;
}

int main() {
    if (testGpuCapsQuery() != 0) {
        return 1;
    }
    if (testBufferLifecycleAndGenerationalSafety() != 0) {
        return 1;
    }
    if (testTextureAndSamplerLifecycle() != 0) {
        return 1;
    }
    if (testCommandRecordingAndExecution() != 0) {
        return 1;
    }
    if (testShaderLifecycleAndValidation() != 0) {
        return 1;
    }
    if (testPipelineLayoutLifecycleAndValidation() != 0) {
        return 1;
    }
    if (testGraphicsPipelineLifecycleAndValidation() != 0) {
        return 1;
    }
    if (testCommandListPipelineBindingAndValidation() != 0) {
        return 1;
    }
    if (testOffscreenTextureLifecycleAndValidation() != 0) {
        return 1;
    }
    if (testOffscreenDynamicRendering() != 0) {
        return 1;
    }
    if (testOffscreenPipelineCompatibility() != 0) {
        return 1;
    }
    if (testBufferReadAndTextureCopy() != 0) {
        return 1;
    }
    if (testDescriptorLifecycleAndBinding() != 0) {
        return 1;
    }
    if (testTextureTransfersAndLayoutTransitions() != 0) {
        return 1;
    }
    if (testTextureFormatCapabilities() != 0) {
        return 1;
    }
    std::cout << "All Null RHI unit tests passed!\n";
    return 0;
}
