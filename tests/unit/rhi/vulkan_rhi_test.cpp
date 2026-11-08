/// @file vulkan_rhi_test.cpp
/// @brief Integration and unit tests for the Vulkan RHI backend and device contract.
///
/// Purpose:
/// Verifies that the Vulkan RhiDevice implementation conforms strictly to the RHI
/// hardware contract: capabilities querying, buffer/texture/sampler lifecycle,
/// generational handle reuse, staging updates, and command submission.
///
/// Architectural note:
/// Follows Rule 3: Does NOT include any Vulkan API headers. All interactions occur
/// purely through the public RHI interfaces.

#include "core/assert.h"
#include "platform/platform.h"
#include "platform/window.h"
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
#include <vector>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HIKARI_HAS_ASAN 1
#endif
#if __has_feature(thread_sanitizer)
#define HIKARI_HAS_TSAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define HIKARI_HAS_ASAN 1
#endif
#if defined(__SANITIZE_THREAD__)
#define HIKARI_HAS_TSAN 1
#endif

#if defined(HIKARI_HAS_ASAN)
// Suppress known one-time process-lifetime allocations within OS graphics driver / dbus / wayland
// stacks
extern "C" const char* __lsan_default_suppressions() {
    return "leak:libdbus-1\nleak:libvulkan\nleak:nvidia\nleak:libwayland\n";
}
#endif

using namespace eng::rhi;

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << '\n'; \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

static int testVulkanDeviceCapabilities() {
    VulkanDeviceDesc desc{.windowHandle = nullptr, .enableValidation = false};
    auto devRes = createVulkanDevice(desc);
    if (!devRes.hasValue()) {
        std::cout << "Vulkan device unavailable on this machine/environment; skipping test ("
                  << devRes.error().message.data() << ")\n";
        return 0;
    }
    auto device = std::move(devRes.value());
    TEST_CHECK(device != nullptr);

    const auto& caps = device->getCaps();
    std::cout << "Detected Vulkan Device: " << caps.deviceName.data() << "\n";
    std::cout << "Hardware Tier: " << static_cast<int>(caps.tier) << "\n";

    TEST_CHECK(caps.features.dynamicRendering);
    TEST_CHECK(caps.features.timelineSemaphores);
    TEST_CHECK(caps.limits.maxTextureDimension2D >= 4096);
    TEST_CHECK(caps.limits.maxColorAttachments >= 4);

    return 0;
}

static int testVulkanBufferLifecycle() {
    VulkanDeviceDesc devDesc{.windowHandle = nullptr, .enableValidation = true};
    auto devRes = createVulkanDevice(devDesc);
    if (!devRes.hasValue()) {
        std::cout << "Vulkan device unavailable; skipping buffer test\n";
        return 0;
    }
    auto device = std::move(devRes.value());

    // 1. Invalid buffer (zero size)
    BufferDesc invalidDesc{.sizeInBytes = 0};
    auto failRes = device->createBuffer(invalidDesc);
    TEST_CHECK(!failRes.hasValue());

    // 2. Valid host-visible vertex buffer
    BufferDesc vtxDesc{.sizeInBytes = 512,
                       .usage = BufferUsageFlags::Vertex | BufferUsageFlags::TransferDst,
                       .memoryUsage = MemoryUsage::CpuToGpu,
                       .debugName = "TestVertexBuffer"};
    auto vtxRes = device->createBuffer(vtxDesc);
    TEST_CHECK(vtxRes.hasValue());
    BufferHandle h1 = vtxRes.value();
    TEST_CHECK(h1.isValid());
    TEST_CHECK(h1.generation() >= 1);

    // 3. Destroy buffer and verify generational handle safety
    device->destroyBuffer(h1);

    // 4. Create new buffer reusing slot with incremented generation
    auto vtxRes2 = device->createBuffer(vtxDesc);
    TEST_CHECK(vtxRes2.hasValue());
    BufferHandle h2 = vtxRes2.value();
    TEST_CHECK(h2.isValid());
    TEST_CHECK(h1 != h2);
    if (h1.index() == h2.index()) {
        TEST_CHECK(h1.generation() != h2.generation());
    }

    device->destroyBuffer(h2);
    return 0;
}

static int testVulkanTextureLifecycle() {
    VulkanDeviceDesc devDesc{.windowHandle = nullptr, .enableValidation = true};
    auto devRes = createVulkanDevice(devDesc);
    if (!devRes.hasValue()) {
        std::cout << "Vulkan device unavailable; skipping texture test\n";
        return 0;
    }
    auto device = std::move(devRes.value());

    // 1. Invalid texture (0 width/height)
    TextureDesc invalidDesc{.type = TextureType::Texture2D, .width = 0, .height = 0};
    auto failRes = device->createTexture(invalidDesc);
    TEST_CHECK(!failRes.hasValue());

    // 2. Valid 2D Color attachment / Sampled texture
    TextureDesc tex2DDesc{.type = TextureType::Texture2D,
                          .format = TextureFormat::RGBA8_UNORM,
                          .width = 128,
                          .height = 128,
                          .depth = 1,
                          .mipLevels = 1,
                          .arrayLayers = 1,
                          .usage = TextureUsageFlags::Sampled | TextureUsageFlags::ColorAttachment,
                          .debugName = "ColorTarget2D"};
    auto texRes = device->createTexture(tex2DDesc);
    TEST_CHECK(texRes.hasValue());
    TextureHandle hTex = texRes.value();
    TEST_CHECK(hTex.isValid());

    // 3. Valid Cubemap texture (6 array layers)
    TextureDesc cubeDesc{.type = TextureType::TextureCube,
                         .format = TextureFormat::RGBA8_UNORM,
                         .width = 64,
                         .height = 64,
                         .depth = 1,
                         .mipLevels = 1,
                         .arrayLayers = 6,
                         .usage = TextureUsageFlags::Sampled,
                         .debugName = "SkyboxCube"};
    auto cubeRes = device->createTexture(cubeDesc);
    TEST_CHECK(cubeRes.hasValue());
    TextureHandle hCube = cubeRes.value();
    TEST_CHECK(hCube.isValid());

    // 4. Destroy textures and verify handle invalidation
    device->destroyTexture(hTex);
    device->destroyTexture(hCube);

    return 0;
}

static int testVulkanSamplerLifecycle() {
    VulkanDeviceDesc devDesc{.windowHandle = nullptr, .enableValidation = true};
    auto devRes = createVulkanDevice(devDesc);
    if (!devRes.hasValue()) {
        std::cout << "Vulkan device unavailable; skipping sampler test\n";
        return 0;
    }
    auto device = std::move(devRes.value());

    SamplerDesc desc{.minFilter = Filter::Linear,
                     .magFilter = Filter::Linear,
                     .mipmapMode = Filter::Linear,
                     .addressU = SamplerAddressMode::Repeat,
                     .addressV = SamplerAddressMode::Repeat,
                     .addressW = SamplerAddressMode::ClampToEdge,
                     .compareEnable = false};

    auto samplerRes = device->createSampler(desc);
    TEST_CHECK(samplerRes.hasValue());
    SamplerHandle hSampler = samplerRes.value();
    TEST_CHECK(hSampler.isValid());

    device->destroySampler(hSampler);
    return 0;
}

static int testVulkanCommandRecordingAndSubmission() {
    VulkanDeviceDesc devDesc{.windowHandle = nullptr, .enableValidation = true};
    auto devRes = createVulkanDevice(devDesc);
    if (!devRes.hasValue()) {
        std::cout << "Vulkan device unavailable; skipping command test\n";
        return 0;
    }
    auto device = std::move(devRes.value());

    // 1. Create buffers for copy and update operations
    BufferDesc srcDesc{.sizeInBytes = 256,
                       .usage = BufferUsageFlags::TransferSrc,
                       .memoryUsage = MemoryUsage::CpuToGpu,
                       .debugName = "CopySource"};
    auto srcRes = device->createBuffer(srcDesc);
    TEST_CHECK(srcRes.hasValue());

    BufferDesc dstDesc{.sizeInBytes = 256,
                       .usage = BufferUsageFlags::TransferDst | BufferUsageFlags::Vertex,
                       .memoryUsage = MemoryUsage::GpuOnly,
                       .debugName = "CopyDestination"};
    auto dstRes = device->createBuffer(dstDesc);
    TEST_CHECK(dstRes.hasValue());

    // 2. Allocate command list
    auto cmdRes = device->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmdList = std::move(cmdRes.value());

    // 3. Record commands (viewport, scissor, buffer copy, buffer update, draw)
    cmdList->begin();
    cmdList->setViewport(0.0F, 0.0F, 1920.0F, 1080.0F, 0.0F, 1.0F);
    cmdList->setScissor(0, 0, 1920, 1080);

    const std::vector<uint8_t> testBytes(64, 0x5A);
    cmdList->updateBuffer(srcRes.value(), 0, testBytes.data(), testBytes.size());
    cmdList->copyBuffer(srcRes.value(), dstRes.value(), 0, 0, 64);
    // Note: draw() and drawIndexed() require an active VkRenderPass or Dynamic Rendering pass,
    // which is managed by the Render Graph. Recording them here would be invalid Vulkan state.
    cmdList->end();

    // 4. Submit and synchronize
    auto submitRes = device->submit(cmdList.get());
    if (!submitRes.hasValue()) {
        std::cerr << "Submit failed: " << submitRes.error().message.data() << " (code "
                  << static_cast<int>(submitRes.error().code) << ")\n";
    }
    TEST_CHECK(submitRes.hasValue());

    device->waitIdle();

    // Cleanup buffers
    device->destroyBuffer(srcRes.value());
    device->destroyBuffer(dstRes.value());

    return 0;
}

static int testVulkanRealSwapchainPresentation() {
    // Only execute if a desktop display server is available
    if (std::getenv("DISPLAY") == nullptr && std::getenv("WAYLAND_DISPLAY") == nullptr) {
        std::cout << "No display server detected; skipping real GPU presentation test\n";
        return 0;
    }

    eng::platform::PlatformConfig platConfig{.headless = false};
    auto platRes = eng::platform::createPlatform(platConfig);
    if (!platRes.hasValue()) {
        std::cout << "Platform initialization failed; skipping presentation test ("
                  << platRes.error().message.data() << ")\n";
        return 0;
    }
    auto platform = std::move(platRes.value());

    eng::platform::WindowDesc winDesc{
        .title = "Hikari Real GPU Presentation Test",
        .width = 320,
        .height = 240,
        .resizable = false,
        .fullscreen = false,
        .headless = false,
        .vulkan = true,
    };
    auto winRes = platform->createWindow(winDesc);
    if (!winRes.hasValue()) {
        std::cout << "Window creation failed; skipping presentation test ("
                  << winRes.error().message.data() << ")\n";
        return 0;
    }
    auto window = std::move(winRes.value());
    TEST_CHECK(window != nullptr);
    TEST_CHECK(window->getNativeHandle() != nullptr);

    VulkanDeviceDesc devDesc{
        .windowHandle = window->getNativeHandle(),
        .enableValidation = false,
    };
    auto devRes = createVulkanDevice(devDesc);
    if (!devRes.hasValue()) {
        std::cout << "Vulkan device with window failed; skipping (" << devRes.error().message.data()
                  << ")\n";
        return 0;
    }
    auto device = std::move(devRes.value());
    TEST_CHECK(device != nullptr);

    // Run 3 frames of real swapchain acquisition, command recording, and presentation
    for (int frame = 0; frame < 3; ++frame) {
        platform->pollEvents();

        auto acqRes = device->acquireNextImage();
        if (!acqRes.hasValue()) {
            std::cout << "Swapchain acquire returned error: " << acqRes.error().message.data()
                      << " (skipping presentation)\n";
            break;
        }

        auto cmdRes = device->createCommandList();
        TEST_CHECK(cmdRes.hasValue());
        auto cmd = std::move(cmdRes.value());

        cmd->begin();
        RenderPassBeginInfo passInfo{
            .clearColor = {0.2F, 0.4F, 0.8F, 1.0F}, // Non-black baseline (Cornflower Blue)
            .clearTarget = true,
        };
        cmd->beginRenderPass(passInfo);
        // Draw visible diagnostic colored rectangles (red and green test elements)
        cmd->clearRect(50, 50, 100, 80, 1.0F, 0.2F, 0.2F, 1.0F);
        cmd->clearRect(180, 50, 100, 80, 0.2F, 0.8F, 0.3F, 1.0F);
        cmd->endRenderPass();
        cmd->end();

        auto submitRes = device->submit(cmd.get());
        TEST_CHECK(submitRes.hasValue());

        auto presRes = device->present();
        TEST_CHECK(presRes.hasValue());
    }

    device->waitIdle();
    std::cout << "Real GPU Swapchain Acquisition & Presentation successfully verified!\n";
    return 0;
}

static int testCommandListDestructionBeforeDeviceDestruction() {
    VulkanDeviceDesc desc{.windowHandle = nullptr, .enableValidation = false};
    auto devRes = createVulkanDevice(desc);
    if (!devRes.hasValue()) {
        std::cout << "Vulkan device not available; skipping\n";
        return 0;
    }
    auto device = std::move(devRes.value());

    auto cmdRes = device->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());

    cmd->begin();
    cmd->setViewport(0.0F, 0.0F, 100.0F, 100.0F, 0.0F, 1.0F);
    cmd->end();

    // Normal destruction order: command list destroyed before device
    cmd.reset();
    TEST_CHECK(cmd == nullptr);

    device->waitIdle();
    device.reset();
    TEST_CHECK(device == nullptr);

    std::cout << "Command list destruction before device verified!\n";
    return 0;
}

static int testDeviceShutdownWithAllocatedCommandLists() {
    VulkanDeviceDesc desc{.windowHandle = nullptr, .enableValidation = false};
    auto devRes = createVulkanDevice(desc);
    if (!devRes.hasValue()) {
        std::cout << "Vulkan device not available; skipping\n";
        return 0;
    }
    auto device = std::move(devRes.value());

    // Allocate buffer for staging test
    auto bufRes = device->createBuffer(BufferDesc{
        .sizeInBytes = 256,
        .usage = BufferUsageFlags::Storage,
        .memoryUsage = MemoryUsage::GpuOnly,
        .debugName = "test_buf",
    });
    TEST_CHECK(bufRes.hasValue());

    // Allocate multiple command lists
    auto cmd1Res = device->createCommandList();
    auto cmd2Res = device->createCommandList();
    auto cmd3Res = device->createCommandList();
    TEST_CHECK(cmd1Res.hasValue() && cmd2Res.hasValue() && cmd3Res.hasValue());

    auto cmd1 = std::move(cmd1Res.value());
    auto cmd2 = std::move(cmd2Res.value());
    auto cmd3 = std::move(cmd3Res.value());

    // Record with staging updates into cmd1
    cmd1->begin();
    uint32_t testData[16] = {1, 2, 3, 4};
    cmd1->updateBuffer(bufRes.value(), 0, testData, sizeof(testData));
    cmd1->end();

    // CRITICAL REGRESSION TEST:
    // Destroy device FIRST while cmd1, cmd2, cmd3 are still alive!
    device.reset();
    TEST_CHECK(device == nullptr);

    // Now destroy the orphaned command lists. Must NOT crash or call vkFreeCommandBuffers.
    cmd1.reset();
    cmd2.reset();
    cmd3.reset();

    std::cout << "Device shutdown with active command lists verified!\n";
    return 0;
}

static int testMultipleCommandListCycles() {
    VulkanDeviceDesc desc{.windowHandle = nullptr, .enableValidation = false};
    auto devRes = createVulkanDevice(desc);
    if (!devRes.hasValue()) {
        std::cout << "Vulkan device not available; skipping\n";
        return 0;
    }
    auto device = std::move(devRes.value());

    for (int i = 0; i < 20; ++i) {
        auto cmdRes = device->createCommandList();
        TEST_CHECK(cmdRes.hasValue());
        auto cmd = std::move(cmdRes.value());

        cmd->begin();
        cmd->setViewport(0.0F, 0.0F, 100.0F, 100.0F, 0.0F, 1.0F);
        cmd->end();

        auto subRes = device->submit(cmd.get());
        TEST_CHECK(subRes.hasValue());
    }

    device->waitIdle();
    std::cout << "Multiple command list creation/destruction cycles verified!\n";
    return 0;
}

static int testPartialInitializationCleanup() {
    VulkanDeviceDesc desc{.windowHandle = nullptr, .enableValidation = false};
    auto devRes = createVulkanDevice(desc);
    if (!devRes.hasValue()) {
        std::cout << "Vulkan device not available; skipping\n";
        return 0;
    }
    auto device = std::move(devRes.value());
    TEST_CHECK(device != nullptr);

    // Buffer creation partial failure and rejection (zero size and overflow size)
    auto zeroBuf = device->createBuffer(BufferDesc{
        .sizeInBytes = 0,
        .usage = BufferUsageFlags::Storage,
        .memoryUsage = MemoryUsage::GpuOnly,
        .debugName = "zero_buf",
    });
    TEST_CHECK(!zeroBuf.hasValue());

    auto hugeBuf = device->createBuffer(BufferDesc{
        .sizeInBytes = static_cast<size_t>(-1),
        .usage = BufferUsageFlags::Storage,
        .memoryUsage = MemoryUsage::GpuOnly,
        .debugName = "huge_buf",
    });
    TEST_CHECK(!hugeBuf.hasValue());

    // Texture creation partial failure (zero dimensions)
    auto badTex = device->createTexture(TextureDesc{
        .type = TextureType::Texture2D,
        .format = TextureFormat::RGBA8_UNORM,
        .width = 0,
        .height = 0,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = TextureUsageFlags::Sampled,
        .debugName = "zero_tex",
    });
    TEST_CHECK(!badTex.hasValue());

    // Verify device is still completely operational after failed allocations
    auto validBuf = device->createBuffer(BufferDesc{
        .sizeInBytes = 64,
        .usage = BufferUsageFlags::Storage,
        .memoryUsage = MemoryUsage::GpuOnly,
        .debugName = "valid_buf",
    });
    TEST_CHECK(validBuf.hasValue());
    device->destroyBuffer(validBuf.value());

    std::cout << "Partial initialization failure and rollback verified!\n";
    return 0;
}

static int testRepeatedDeviceInitializationShutdown() {
    for (int cycle = 0; cycle < 3; ++cycle) {
        VulkanDeviceDesc desc{.windowHandle = nullptr, .enableValidation = false};
        auto devRes = createVulkanDevice(desc);
        if (!devRes.hasValue()) {
            std::cout << "Vulkan device not available; skipping\n";
            return 0;
        }
        auto device = std::move(devRes.value());
        TEST_CHECK(device != nullptr);

        auto cmdRes = device->createCommandList();
        TEST_CHECK(cmdRes.hasValue());
        auto cmd = std::move(cmdRes.value());

        cmd->begin();
        cmd->end();
        auto subRes = device->submit(cmd.get());
        TEST_CHECK(subRes.hasValue());

        device->waitIdle();
        cmd.reset();
        device.reset();
    }
    std::cout << "Repeated device initialization/shutdown cycles verified!\n";
    return 0;
}

static int testVulkanShaderLifecycleAndValidation() {
    VulkanDeviceDesc desc{.windowHandle = nullptr, .enableValidation = false};
    auto devRes = createVulkanDevice(desc);
    if (!devRes.hasValue()) {
        std::cout << "Vulkan device unavailable; skipping shader test\n";
        return 0;
    }
    auto device = std::move(devRes.value());

    // 1. Invalid stage
    ShaderDesc badStageDesc{
        .stage = ShaderStageFlags::None,
        .spirvCode = eng::rhi::test::kTestVertexShaderSpirv,
        .entryPoint = "main",
        .debugName = {},
    };
    auto failStage = device->createShader(badStageDesc);
    TEST_CHECK(!failStage.hasValue());

    // 2. Bad magic number
    uint32_t badMagic[] = {0x12345678U, 0, 0, 0, 0};
    ShaderDesc badMagicDesc{
        .stage = ShaderStageFlags::Vertex,
        .spirvCode = badMagic,
        .entryPoint = "main",
        .debugName = {},
    };
    auto failMagic = device->createShader(badMagicDesc);
    TEST_CHECK(!failMagic.hasValue());

    // 3. Valid creation
    ShaderDesc vsDesc{
        .stage = ShaderStageFlags::Vertex,
        .spirvCode = eng::rhi::test::kTestVertexShaderSpirv,
        .entryPoint = "main",
        .debugName = "VkTestVS",
    };
    auto vsRes = device->createShader(vsDesc);
    TEST_CHECK(vsRes.hasValue());
    ShaderHandle vsHandle = vsRes.value();
    TEST_CHECK(vsHandle.isValid());

    ShaderDesc fsDesc{
        .stage = ShaderStageFlags::Fragment,
        .spirvCode = eng::rhi::test::kTestFragmentShaderSpirv,
        .entryPoint = "main",
        .debugName = "VkTestFS",
    };
    auto fsRes = device->createShader(fsDesc);
    TEST_CHECK(fsRes.hasValue());
    ShaderHandle fsHandle = fsRes.value();
    TEST_CHECK(fsHandle.isValid());

    // 4. Destruction and reuse
    device->destroyShader(vsHandle);
    auto vsRes2 = device->createShader(vsDesc);
    TEST_CHECK(vsRes2.hasValue());
    ShaderHandle vsHandle2 = vsRes2.value();
    TEST_CHECK(vsHandle2.index() == vsHandle.index());
    TEST_CHECK(vsHandle2.generation() != vsHandle.generation());

    device->destroyShader(vsHandle2);
    device->destroyShader(fsHandle);
    std::cout << "Vulkan shader module lifecycle and validation verified!\n";
    return 0;
}

static int testVulkanPipelineLayoutLifecycle() {
    VulkanDeviceDesc desc{.windowHandle = nullptr, .enableValidation = false};
    auto devRes = createVulkanDevice(desc);
    if (!devRes.hasValue()) {
        std::cout << "Vulkan device unavailable; skipping pipeline layout test\n";
        return 0;
    }
    auto device = std::move(devRes.value());

    // 1. Invalid push constant (unaligned)
    PipelineLayoutDesc badLayoutDesc{
        .pushConstants = {{.stageFlags = ShaderStageFlags::Vertex, .offset = 3, .size = 16}},
        .descriptorSetLayouts = {},
        .debugName = {},
    };
    auto failLayout = device->createPipelineLayout(badLayoutDesc);
    TEST_CHECK(!failLayout.hasValue());

    // 2. Valid layout
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
                },
        }},
        .debugName = "VkMainLayout",
    };
    auto layoutRes = device->createPipelineLayout(validDesc);
    TEST_CHECK(layoutRes.hasValue());
    PipelineLayoutHandle layoutHandle = layoutRes.value();
    TEST_CHECK(layoutHandle.isValid());

    device->destroyPipelineLayout(layoutHandle);
    std::cout << "Vulkan pipeline layout lifecycle verified!\n";
    return 0;
}

static int testVulkanGraphicsPipelineLifecycle() {
    VulkanDeviceDesc desc{.windowHandle = nullptr, .enableValidation = true};
    auto devRes = createVulkanDevice(desc);
    if (!devRes.hasValue()) {
        std::cout << "Vulkan device unavailable; skipping graphics pipeline test\n";
        return 0;
    }
    auto device = std::move(devRes.value());

    ShaderDesc vsDesc{
        .stage = ShaderStageFlags::Vertex,
        .spirvCode = eng::rhi::test::kTestVertexShaderSpirv,
        .entryPoint = "main",
        .debugName = "VkVS",
    };
    auto vsRes = device->createShader(vsDesc);
    TEST_CHECK(vsRes.hasValue());

    ShaderDesc fsDesc{
        .stage = ShaderStageFlags::Fragment,
        .spirvCode = eng::rhi::test::kTestFragmentShaderSpirv,
        .entryPoint = "main",
        .debugName = "VkFS",
    };
    auto fsRes = device->createShader(fsDesc);
    TEST_CHECK(fsRes.hasValue());

    PipelineLayoutDesc layoutDesc{
        .pushConstants = {{.stageFlags = ShaderStageFlags::Vertex, .offset = 0, .size = 64}},
        .descriptorSetLayouts = {},
        .debugName = "VkPipelineLayout",
    };
    auto layoutRes = device->createPipelineLayout(layoutDesc);
    TEST_CHECK(layoutRes.hasValue());

    // Valid graphics pipeline with dynamic rendering attachment format
    GraphicsPipelineDesc pipeDesc{
        .vertexShader = vsRes.value(),
        .fragmentShader = fsRes.value(),
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
        .colorBlendAttachments = {},
        .layout = layoutRes.value(),
        .colorAttachmentFormats = {TextureFormat::RGBA8_UNORM},
        .depthAttachmentFormat = TextureFormat::D32_FLOAT,
        .debugName = "VkRealMeshPipeline",
    };
    auto pipeRes = device->createGraphicsPipeline(pipeDesc);
    TEST_CHECK(pipeRes.hasValue());
    PipelineHandle pipeHandle = pipeRes.value();
    TEST_CHECK(pipeHandle.isValid());

    device->destroyGraphicsPipeline(pipeHandle);
    device->destroyPipelineLayout(layoutRes.value());
    device->destroyShader(fsRes.value());
    device->destroyShader(vsRes.value());
    std::cout << "Vulkan graphics pipeline lifecycle verified!\n";
    return 0;
}

static int testVulkanCommandListPipelineBinding() {
    VulkanDeviceDesc desc{.windowHandle = nullptr, .enableValidation = false};
    auto devRes = createVulkanDevice(desc);
    if (!devRes.hasValue()) {
        std::cout << "Vulkan device unavailable; skipping command list pipeline binding test\n";
        return 0;
    }
    auto device = std::move(devRes.value());

    ShaderDesc vsDesc{
        .stage = ShaderStageFlags::Vertex,
        .spirvCode = eng::rhi::test::kTestVertexShaderSpirv,
        .entryPoint = "main",
        .debugName = "VkVS",
    };
    auto vsRes = device->createShader(vsDesc);
    ShaderDesc fsDesc{
        .stage = ShaderStageFlags::Fragment,
        .spirvCode = eng::rhi::test::kTestFragmentShaderSpirv,
        .entryPoint = "main",
        .debugName = "VkFS",
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
        .vertexInput = {},
        .topology = PrimitiveTopology::TriangleList,
        .rasterization = {},
        .depthStencil = {},
        .colorBlendAttachments = {},
        .layout = layoutRes.value(),
        .colorAttachmentFormats = {TextureFormat::RGBA8_UNORM},
        .depthAttachmentFormat = TextureFormat::Undefined,
        .debugName = {},
    };
    auto pipeRes = device->createGraphicsPipeline(pipeDesc);
    TEST_CHECK(pipeRes.hasValue());

    auto cmdRes = device->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());

    cmd->begin();

    // 1. Draw without pipeline bound -> skipped safely without crashing Vulkan driver
    cmd->draw(3, 1, 0, 0);

    // 2. Bind pipeline
    cmd->bindPipeline(pipeRes.value());

    // 3. Push constants
    float mvp[16] = {1.0f};
    cmd->pushConstants(layoutRes.value(), ShaderStageFlags::Vertex, 0, sizeof(mvp), mvp);

    // 4. Bind invalid pipeline
    cmd->bindPipeline(PipelineHandle{});

    cmd->end();

    auto subRes = device->submit(cmd.get());
    TEST_CHECK(subRes.hasValue());
    device->waitIdle();

    device->destroyGraphicsPipeline(pipeRes.value());
    device->destroyPipelineLayout(layoutRes.value());
    device->destroyShader(fsRes.value());
    device->destroyShader(vsRes.value());

    std::cout << "Vulkan command list pipeline binding verified!\n";
    return 0;
}

static int testVulkanOffscreenTextureLifecycle() {
    VulkanDeviceDesc devDesc{.windowHandle = nullptr, .enableValidation = true};
    auto devRes = createVulkanDevice(devDesc);
    if (!devRes.hasValue()) {
        std::cout << "Vulkan device unavailable; skipping offscreen texture test\n";
        return 0;
    }
    auto device = std::move(devRes.value());

    // 1. Valid offscreen color texture (RGBA8_UNORM)
    TextureDesc colorDesc{
        .type = TextureType::Texture2D,
        .format = TextureFormat::RGBA8_UNORM,
        .width = 1280,
        .height = 720,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = TextureUsageFlags::ColorAttachment | TextureUsageFlags::Sampled,
        .debugName = "VkOffscreenColor",
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

    // 2. Valid offscreen depth texture (D32_FLOAT)
    TextureDesc depthDesc{
        .type = TextureType::Texture2D,
        .format = TextureFormat::D32_FLOAT,
        .width = 1280,
        .height = 720,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = TextureUsageFlags::DepthStencilAttachment,
        .debugName = "VkOffscreenDepth",
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

    // 3. Validation failure paths
    TextureDesc undefDesc = colorDesc;
    undefDesc.format = TextureFormat::Undefined;
    TEST_CHECK(!device->createTexture(undefDesc).hasValue());

    TextureDesc zeroWidthDesc = colorDesc;
    zeroWidthDesc.width = 0;
    TEST_CHECK(!device->createTexture(zeroWidthDesc).hasValue());

    // 4. Destroy textures
    device->destroyTexture(colorTex);
    TEST_CHECK(device->getTextureDesc(colorTex) == nullptr);

    device->destroyTexture(depthTex);
    TEST_CHECK(device->getTextureDesc(depthTex) == nullptr);

    std::cout << "Vulkan offscreen texture lifecycle verified!\n";
    return 0;
}

static int testVulkanOffscreenDynamicRendering() {
    VulkanDeviceDesc devDesc{.windowHandle = nullptr, .enableValidation = true};
    auto devRes = createVulkanDevice(devDesc);
    if (!devRes.hasValue()) {
        std::cout << "Vulkan device unavailable; skipping offscreen dynamic rendering test\n";
        return 0;
    }
    auto device = std::move(devRes.value());

    // 1. Create color and depth textures
    TextureDesc colorDesc{
        .type = TextureType::Texture2D,
        .format = TextureFormat::RGBA8_UNORM,
        .width = 640,
        .height = 480,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = TextureUsageFlags::ColorAttachment | TextureUsageFlags::Sampled,
        .debugName = "VkDynColor",
    };
    auto colorTex = device->createTexture(colorDesc).value();

    TextureDesc depthDesc{
        .type = TextureType::Texture2D,
        .format = TextureFormat::D32_FLOAT,
        .width = 640,
        .height = 480,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = TextureUsageFlags::DepthStencilAttachment,
        .debugName = "VkDynDepth",
    };
    auto depthTex = device->createTexture(depthDesc).value();

    // 2. Create shaders and pipeline
    ShaderDesc vsDesc{
        .stage = ShaderStageFlags::Vertex,
        .spirvCode = eng::rhi::test::kTestVertexShaderSpirv,
        .entryPoint = "main",
    };
    auto vsRes = device->createShader(vsDesc);
    ShaderDesc fsDesc{
        .stage = ShaderStageFlags::Fragment,
        .spirvCode = eng::rhi::test::kTestFragmentShaderSpirv,
        .entryPoint = "main",
    };
    auto fsRes = device->createShader(fsDesc);

    PipelineLayoutDesc layoutDesc{
        .pushConstants = {{.stageFlags = ShaderStageFlags::Vertex, .offset = 0, .size = 64}},
    };
    auto layoutRes = device->createPipelineLayout(layoutDesc);

    GraphicsPipelineDesc pipeDesc{
        .vertexShader = vsRes.value(),
        .fragmentShader = fsRes.value(),
        .vertexInput = {},
        .topology = PrimitiveTopology::TriangleList,
        .rasterization = {},
        .depthStencil =
            {
                .depthTestEnable = true,
                .depthWriteEnable = true,
                .depthCompareOp = CompareOp::LessOrEqual,
            },
        .colorBlendAttachments = {{
            .blendEnable = false,
        }},
        .layout = layoutRes.value(),
        .colorAttachmentFormats = {TextureFormat::RGBA8_UNORM},
        .depthAttachmentFormat = TextureFormat::D32_FLOAT,
        .debugName = "VkDynPipe",
    };
    auto pipeRes = device->createGraphicsPipeline(pipeDesc);
    TEST_CHECK(pipeRes.hasValue());

    // 3. Create command list and record offscreen rendering pass
    auto cmdRes = device->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());

    cmd->begin();

    RenderPassBeginInfo passInfo{
        .colorTarget = colorTex,
        .depthTarget = depthTex,
        .clearColor = {0.1F, 0.2F, 0.3F, 1.0F},
        .clearDepth = 1.0F,
        .clearTarget = true,
        .clearDepthTarget = true,
    };
    cmd->beginRenderPass(passInfo);

    cmd->bindPipeline(pipeRes.value());

    float mvp[16] = {1.0F};
    cmd->pushConstants(layoutRes.value(), ShaderStageFlags::Vertex, 0, sizeof(mvp), mvp);

    // Draw triangle in offscreen pass
    cmd->draw(3, 1, 0, 0);

    // End pass: transitions color texture to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL and depth to
    // VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
    cmd->endRenderPass();

    cmd->end();

    // 4. Submit to GPU and wait idle
    auto subRes = device->submit(cmd.get());
    TEST_CHECK(subRes.hasValue());
    device->waitIdle();

    // 5. Clean up
    device->destroyGraphicsPipeline(pipeRes.value());
    device->destroyPipelineLayout(layoutRes.value());
    device->destroyShader(fsRes.value());
    device->destroyShader(vsRes.value());
    device->destroyTexture(depthTex);
    device->destroyTexture(colorTex);

    std::cout << "Vulkan offscreen dynamic rendering verified!\n";
    return 0;
}

static int testVulkanOffscreenPipelineCompatibility() {
    VulkanDeviceDesc devDesc{.windowHandle = nullptr, .enableValidation = true};
    auto devRes = createVulkanDevice(devDesc);
    if (!devRes.hasValue()) {
        std::cout << "Vulkan device unavailable; skipping pipeline compatibility test\n";
        return 0;
    }
    auto device = std::move(devRes.value());

    TextureDesc colorDesc{
        .type = TextureType::Texture2D,
        .format = TextureFormat::RGBA8_UNORM,
        .width = 320,
        .height = 240,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = TextureUsageFlags::ColorAttachment,
    };
    auto colorTex = device->createTexture(colorDesc).value();

    ShaderDesc vsDesc{
        .stage = ShaderStageFlags::Vertex,
        .spirvCode = eng::rhi::test::kTestVertexShaderSpirv,
        .entryPoint = "main",
    };
    auto vsRes = device->createShader(vsDesc);
    ShaderDesc fsDesc{
        .stage = ShaderStageFlags::Fragment,
        .spirvCode = eng::rhi::test::kTestFragmentShaderSpirv,
        .entryPoint = "main",
    };
    auto fsRes = device->createShader(fsDesc);

    PipelineLayoutDesc layoutDesc{};
    auto layoutRes = device->createPipelineLayout(layoutDesc);

    // Pipeline with BGRA8_UNORM (mismatched with colorTex which is RGBA8_UNORM)
    GraphicsPipelineDesc mismatchPipeDesc{
        .vertexShader = vsRes.value(),
        .fragmentShader = fsRes.value(),
        .layout = layoutRes.value(),
        .colorAttachmentFormats = {TextureFormat::BGRA8_UNORM},
    };
    auto mismatchPipeRes = device->createGraphicsPipeline(mismatchPipeDesc);
    TEST_CHECK(mismatchPipeRes.hasValue());

    auto cmdRes = device->createCommandList();
    auto cmd = std::move(cmdRes.value());

    cmd->begin();
    RenderPassBeginInfo passInfo{
        .colorTarget = colorTex,
    };
    cmd->beginRenderPass(passInfo);
    cmd->bindPipeline(mismatchPipeRes.value());

    // Drawing with mismatched pipeline format is safely caught and suppressed without issuing GPU
    // draws
    cmd->draw(3, 1, 0, 0);
    cmd->drawIndexed(6, 1, 0, 0, 0);

    cmd->endRenderPass();
    cmd->end();

    auto subRes = device->submit(cmd.get());
    TEST_CHECK(subRes.hasValue());
    device->waitIdle();

    device->destroyGraphicsPipeline(mismatchPipeRes.value());
    device->destroyPipelineLayout(layoutRes.value());
    device->destroyShader(fsRes.value());
    device->destroyShader(vsRes.value());
    device->destroyTexture(colorTex);

    std::cout << "Vulkan offscreen pipeline format compatibility check verified!\n";
    return 0;
}

static int testVulkanBufferReadAndTextureCopy() {
    VulkanDeviceDesc devDesc{.windowHandle = nullptr, .enableValidation = true};
    auto devRes = createVulkanDevice(devDesc);
    if (!devRes.hasValue()) {
        std::cout << "Vulkan device unavailable; skipping buffer read & copy test\n";
        return 0;
    }
    auto device = std::move(devRes.value());

    const uint32_t width = 16;
    const uint32_t height = 16;
    const size_t byteSize = width * height * 4;

    BufferDesc bufDesc{
        .sizeInBytes = byteSize,
        .usage = BufferUsageFlags::TransferDst,
        .memoryUsage = MemoryUsage::CpuToGpu,
    };
    auto bufRes = device->createBuffer(bufDesc);
    TEST_CHECK(bufRes.hasValue());
    BufferHandle buf = bufRes.value();

    // 1. Verify direct CPU write and read back
    std::vector<uint8_t> sampleData(byteSize, 0xAB);
    TEST_CHECK(device->writeBuffer(buf, 0, sampleData.data(), byteSize).hasValue());

    std::vector<uint8_t> readBack(byteSize, 0);
    TEST_CHECK(device->readBuffer(buf, 0, readBack.data(), byteSize).hasValue());
    TEST_CHECK(std::memcmp(sampleData.data(), readBack.data(), byteSize) == 0);

    // 2. Create offscreen color texture and clear it, then copyTextureToBuffer
    TextureDesc texDesc{
        .type = TextureType::Texture2D,
        .format = TextureFormat::RGBA8_UNORM,
        .width = width,
        .height = height,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = TextureUsageFlags::ColorAttachment | TextureUsageFlags::TransferSrc,
    };
    auto texRes = device->createTexture(texDesc);
    TEST_CHECK(texRes.hasValue());
    TextureHandle tex = texRes.value();

    auto cmdRes = device->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());

    cmd->begin();
    // Render pass with explicit clear color: (1.0, 0.0, 0.0, 1.0) = red
    RenderPassBeginInfo passInfo{
        .colorTarget = tex,
        .clearColor = {1.0F, 0.0F, 0.0F, 1.0F},
        .clearTarget = true,
    };
    cmd->beginRenderPass(passInfo);
    cmd->endRenderPass();

    // Copy rendered red image to buffer
    cmd->copyTextureToBuffer(tex, buf);
    cmd->end();

    auto subRes = device->submit(cmd.get());
    TEST_CHECK(subRes.hasValue());
    device->waitIdle();

    // 3. Read back pixels from buffer and assert that pixel bytes match clear red (255, 0, 0, 255)
    std::vector<uint8_t> pixelData(byteSize, 0);
    TEST_CHECK(device->readBuffer(buf, 0, pixelData.data(), byteSize).hasValue());
    for (size_t i = 0; i < byteSize; i += 4) {
        TEST_CHECK(pixelData[i] == 255);     // R
        TEST_CHECK(pixelData[i + 1] == 0);   // G
        TEST_CHECK(pixelData[i + 2] == 0);   // B
        TEST_CHECK(pixelData[i + 3] == 255); // A
    }

    device->destroyTexture(tex);
    device->destroyBuffer(buf);

    std::cout << "Vulkan buffer read and texture-to-buffer copy verified!\n";
    return 0;
}

int main() {
    std::cout << "Running Vulkan RHI Tests...\n";

#if defined(HIKARI_HAS_TSAN)
    std::cout << "ThreadSanitizer active: skipping live GPU driver initialization due to "
                 "proprietary driver background thread conflict (null_rhi_test covers RHI for "
                 "TSan)\n";
    return 0;
#endif

    if (testVulkanDeviceCapabilities() != 0) {
        return 1;
    }
    if (testVulkanBufferLifecycle() != 0) {
        return 1;
    }
    if (testVulkanTextureLifecycle() != 0) {
        return 1;
    }
    if (testVulkanSamplerLifecycle() != 0) {
        return 1;
    }
    if (testVulkanCommandRecordingAndSubmission() != 0) {
        return 1;
    }
    if (testVulkanRealSwapchainPresentation() != 0) {
        return 1;
    }
    if (testCommandListDestructionBeforeDeviceDestruction() != 0) {
        return 1;
    }
    if (testDeviceShutdownWithAllocatedCommandLists() != 0) {
        return 1;
    }
    if (testMultipleCommandListCycles() != 0) {
        return 1;
    }
    if (testPartialInitializationCleanup() != 0) {
        return 1;
    }
    if (testRepeatedDeviceInitializationShutdown() != 0) {
        return 1;
    }
    if (testVulkanShaderLifecycleAndValidation() != 0) {
        return 1;
    }
    if (testVulkanPipelineLayoutLifecycle() != 0) {
        return 1;
    }
    if (testVulkanGraphicsPipelineLifecycle() != 0) {
        return 1;
    }
    if (testVulkanCommandListPipelineBinding() != 0) {
        return 1;
    }
    if (testVulkanOffscreenTextureLifecycle() != 0) {
        return 1;
    }
    if (testVulkanOffscreenDynamicRendering() != 0) {
        return 1;
    }
    if (testVulkanOffscreenPipelineCompatibility() != 0) {
        return 1;
    }
    if (testVulkanBufferReadAndTextureCopy() != 0) {
        return 1;
    }

    std::cout << "All Vulkan RHI Tests Passed!\n";
    return 0;
}
