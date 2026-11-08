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
                      .compareEnable = false};
    auto sampRes = device->createSampler(sDesc);
    TEST_CHECK(sampRes.hasValue());
    SamplerHandle sh = sampRes.value();
    TEST_CHECK(sh.isValid());

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
    TEST_CHECK(!nullCmd->hasFormatMismatch());
    cmd->endRenderPass();
    cmd->end();

    // 2. Draw with mismatched color format -> should flag format mismatch
    cmd->begin();
    RenderPassBeginInfo colorMismatchPass{
        .colorTarget = bgraColorTex,
        .depthTarget = depthTex,
    };
    cmd->beginRenderPass(colorMismatchPass);
    cmd->bindPipeline(pipeline);
    cmd->draw(3, 1, 0, 0);
    TEST_CHECK(nullCmd->hasFormatMismatch());
    cmd->endRenderPass();
    cmd->end();

    // 3. Draw with mismatched depth format -> should flag format mismatch
    cmd->begin();
    RenderPassBeginInfo depthMismatchPass{
        .colorTarget = colorTex,
        .depthTarget = d24DepthTex,
    };
    cmd->beginRenderPass(depthMismatchPass);
    cmd->bindPipeline(pipeline);
    cmd->draw(3, 1, 0, 0);
    TEST_CHECK(nullCmd->hasFormatMismatch());
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
    std::cout << "All Null RHI unit tests passed!\n";
    return 0;
}
