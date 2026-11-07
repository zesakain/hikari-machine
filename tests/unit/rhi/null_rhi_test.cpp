/// @file null_rhi_test.cpp
/// @brief Unit tests for RHI abstractions and Null headless backend.

#include "core/assert.h"
#include "rhi/buffer.h"
#include "rhi/command_list.h"
#include "rhi/gpu_caps.h"
#include "rhi/rhi_device.h"
#include "rhi/sampler.h"
#include "rhi/texture.h"

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
    std::cout << "All Null RHI unit tests passed!\n";
    return 0;
}
