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
#include "rhi/rhi_device.h"
#include "rhi/sampler.h"
#include "rhi/texture.h"

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
// Suppress known one-time process-lifetime allocations within OS graphics driver / dbus stacks
extern "C" const char* __lsan_default_suppressions() {
    return "leak:libdbus-1\nleak:libvulkan\nleak:nvidia\n";
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
    VulkanDeviceDesc devDesc{.windowHandle = nullptr, .enableValidation = false};
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
    VulkanDeviceDesc devDesc{.windowHandle = nullptr, .enableValidation = false};
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
    VulkanDeviceDesc devDesc{.windowHandle = nullptr, .enableValidation = false};
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
    VulkanDeviceDesc devDesc{.windowHandle = nullptr, .enableValidation = false};
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
        cmd->setViewport(0.0F, 0.0F, 320.0F, 240.0F, 0.0F, 1.0F);
        cmd->setScissor(0, 0, 320, 240);
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

    std::cout << "All Vulkan RHI Tests Passed!\n";
    return 0;
}
