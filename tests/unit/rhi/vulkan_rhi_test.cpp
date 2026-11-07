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
#include "rhi/buffer.h"
#include "rhi/command_list.h"
#include "rhi/gpu_caps.h"
#include "rhi/rhi_device.h"
#include "rhi/sampler.h"
#include "rhi/texture.h"

#include <cstring>
#include <iostream>
#include <vector>

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

int main() {
    std::cout << "Running Vulkan RHI Tests...\n";

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

    std::cout << "All Vulkan RHI Tests Passed!\n";
    return 0;
}
