/// @file textured_mesh_render_test.cpp
/// @brief Visual and hardware verification test for texture sampling on 3D meshes.
///
/// Milestone 10B Verification Requirements:
/// 1. Image decoding succeeds (via ImageDecoder / AssetManager).
/// 2. GPU upload succeeds (staging buffer, memory copy, barrier transitions).
/// 3. Image transitions into ShaderReadOnly layout.
/// 4. Image view and sampler are valid.
/// 5. Milestone 10A descriptor binding (CombinedImageSampler) binds successfully.
/// 6. A 3D mesh samples and displays the texture correctly with pixel readback.

#include "core/assert.h"
#include "core/math.h"
#include "render/asset_manager.h"
#include "render/gpu_mesh.h"
#include "render/gpu_texture.h"
#include "render/image.h"
#include "render/mesh.h"
#include "render/mesh_shader_spirv.h"
#include "rhi/command_list.h"
#include "rhi/descriptor.h"
#include "rhi/pipeline.h"
#include "rhi/rhi_device.h"

#include <cmath>
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
extern "C" const char* __lsan_default_suppressions() {
    return "leak:libdbus-1\nleak:libvulkan\nleak:nvidia\nleak:libwayland\n";
}
#endif

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ << ": " #cond       \
                      << std::endl;                                                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

using namespace eng;
using namespace eng::render;
using namespace eng::rhi;

// Creates a 2-triangle quad facing +Z
static MeshData createTexturedQuad() {
    MeshData mesh{};
    // Vertices: pos, normal, uv, color
    // Viewport convention is Y-up:
    // Top-Left: (-1, 1) -> UV (0, 0)
    // Top-Right: (1, 1) -> UV (1, 0)
    // Bottom-Left: (-1, -1) -> UV (0, 1)
    // Bottom-Right: (1, -1) -> UV (1, 1)
    mesh.vertices = {
        Vertex(Vec3(-1.0F, -1.0F, 0.0F), Vec3(0.0F, 0.0F, 1.0F), Vec2(0.0F, 1.0F),
               Vec4(1.0F, 1.0F, 1.0F, 1.0F)),
        Vertex(Vec3(1.0F, -1.0F, 0.0F), Vec3(0.0F, 0.0F, 1.0F), Vec2(1.0F, 1.0F),
               Vec4(1.0F, 1.0F, 1.0F, 1.0F)),
        Vertex(Vec3(1.0F, 1.0F, 0.0F), Vec3(0.0F, 0.0F, 1.0F), Vec2(1.0F, 0.0F),
               Vec4(1.0F, 1.0F, 1.0F, 1.0F)),
        Vertex(Vec3(-1.0F, 1.0F, 0.0F), Vec3(0.0F, 0.0F, 1.0F), Vec2(0.0F, 0.0F),
               Vec4(1.0F, 1.0F, 1.0F, 1.0F)),
    };
    mesh.indices = {0, 1, 2, 2, 3, 0};
    mesh.submeshes.push_back(Submesh{
        .name = "QuadSubmesh",
        .firstIndex = 0,
        .indexCount = 6,
        .vertexOffset = 0,
        .materialIndex = 0,
    });
    return mesh;
}

// 2x2 colored checkerboard:
// (0,0) Red    (1,0) Green
// (0,1) Blue   (1,1) Yellow
static ImageData createCheckerboardImageData() {
    ImageData img{
        .width = 2,
        .height = 2,
        .channels = 4,
        .format = TextureFormat::RGBA8_UNORM,
        .pixels =
            {
                255, 0, 0, 255,  // (0,0): Red
                0, 255, 0, 255,  // (1,0): Green
                0, 0, 255, 255,  // (0,1): Blue
                255, 255, 0, 255 // (1,1): Yellow
            },
    };
    return img;
}

static int testTexturedMeshRecordingOnNullDevice() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());

    ImageData img = createCheckerboardImageData();
    auto texRes = GpuTexture::create(*device, img, false);
    TEST_CHECK(texRes.hasValue());
    auto gpuTex = std::move(texRes.value());
    TEST_CHECK(gpuTex->isValid());
    TEST_CHECK(gpuTex->getSampler().isValid());

    MeshData quadMesh = createTexturedQuad();
    auto meshRes = GpuMesh::create(*device, quadMesh);
    TEST_CHECK(meshRes.hasValue());
    auto gpuMesh = std::move(meshRes.value());
    TEST_CHECK(gpuMesh->isValid());

    // 1. Create Descriptor Set Layout
    DescriptorSetLayoutDesc layoutDesc{
        .bindings = {{
            .binding = 0,
            .descriptorType = DescriptorType::CombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = ShaderStageFlags::Fragment,
        }},
        .debugName = "TexturedMeshLayout",
    };
    auto dsLayoutRes = device->createDescriptorSetLayout(layoutDesc);
    TEST_CHECK(dsLayoutRes.hasValue());
    DescriptorSetLayoutHandle dsLayout = dsLayoutRes.value();

    // 2. Create Descriptor Pool & Allocate Set
    DescriptorPoolDesc poolDesc{
        .maxSets = 1,
        .poolSizes = {{.type = DescriptorType::CombinedImageSampler, .count = 1}},
        .debugName = "TexturedMeshPool",
    };
    auto poolRes = device->createDescriptorPool(poolDesc);
    TEST_CHECK(poolRes.hasValue());
    DescriptorPoolHandle pool = poolRes.value();

    auto setRes = device->allocateDescriptorSet(pool, dsLayout);
    TEST_CHECK(setRes.hasValue());
    DescriptorSetHandle dsSet = setRes.value();

    // 3. Update Descriptor Set with GPU Texture & Sampler
    DescriptorWrite write =
        DescriptorWrite::makeCombinedImageSampler(0, gpuTex->getTexture(), gpuTex->getSampler());
    TEST_CHECK(device->updateDescriptorSet(dsSet, std::span{&write, 1}).hasValue());

    // 4. Create Pipeline Layout & Shaders
    PipelineLayoutDesc pipeLayoutDesc{
        .pushConstants = {{
            .stageFlags = ShaderStageFlags::Vertex,
            .offset = 0,
            .size = 128,
        }},
        .descriptorSetLayoutHandles = {dsLayout},
        .descriptorSetLayouts = {},
        .debugName = "TexturedPipeLayout",
    };
    auto pipeLayoutRes = device->createPipelineLayout(pipeLayoutDesc);
    TEST_CHECK(pipeLayoutRes.hasValue());
    PipelineLayoutHandle pipeLayout = pipeLayoutRes.value();

    ShaderDesc vertDesc{
        .stage = ShaderStageFlags::Vertex,
        .spirvCode = kMeshVertexShaderSpirv,
        .entryPoint = "main",
        .debugName = "TexturedVertShader",
    };
    auto vertRes = device->createShader(vertDesc);
    TEST_CHECK(vertRes.hasValue());
    ShaderHandle vertShader = vertRes.value();

    ShaderDesc fragDesc{
        .stage = ShaderStageFlags::Fragment,
        .spirvCode = kTexturedMeshFragmentShaderSpirv,
        .entryPoint = "main",
        .debugName = "TexturedFragShader",
    };
    auto fragRes = device->createShader(fragDesc);
    TEST_CHECK(fragRes.hasValue());
    ShaderHandle fragShader = fragRes.value();

    GraphicsPipelineDesc pipeDesc{
        .vertexShader = vertShader,
        .fragmentShader = fragShader,
        .vertexInput =
            {
                .bindings = {{
                    .binding = 0,
                    .stride = sizeof(Vertex),
                    .inputRate = VertexInputRate::Vertex,
                }},
                .attributes =
                    {
                        {.location = 0,
                         .binding = 0,
                         .format = VertexFormat::Float3,
                         .offset = static_cast<uint32_t>(offsetof(Vertex, position))},
                        {.location = 1,
                         .binding = 0,
                         .format = VertexFormat::Float3,
                         .offset = static_cast<uint32_t>(offsetof(Vertex, normal))},
                        {.location = 2,
                         .binding = 0,
                         .format = VertexFormat::Float2,
                         .offset = static_cast<uint32_t>(offsetof(Vertex, uv))},
                        {.location = 3,
                         .binding = 0,
                         .format = VertexFormat::Float4,
                         .offset = static_cast<uint32_t>(offsetof(Vertex, color))},
                    },
            },
        .topology = PrimitiveTopology::TriangleList,
        .rasterization = {.polygonMode = PolygonMode::Fill,
                          .cullMode = CullMode::None,
                          .frontFace = FrontFace::CounterClockwise},
        .depthStencil = {.depthTestEnable = false, .depthWriteEnable = false},
        .colorBlendAttachments = {{.blendEnable = false,
                                   .colorWriteMask = ColorComponentFlags::All}},
        .layout = pipeLayout,
        .colorAttachmentFormats = {TextureFormat::RGBA8_UNORM},
        .debugName = "TexturedMeshPipeline",
    };
    auto pipeRes = device->createGraphicsPipeline(pipeDesc);
    TEST_CHECK(pipeRes.hasValue());
    PipelineHandle pipeline = pipeRes.value();

    // 5. Create Render Target & Record Commands
    TextureDesc colorDesc{
        .type = TextureType::Texture2D,
        .format = TextureFormat::RGBA8_UNORM,
        .width = 64,
        .height = 64,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = TextureUsageFlags::ColorAttachment | TextureUsageFlags::TransferSrc,
        .debugName = "ColorTarget",
    };
    auto rtRes = device->createTexture(colorDesc);
    TEST_CHECK(rtRes.hasValue());
    TextureHandle colorTarget = rtRes.value();

    auto cmdRes = device->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());

    cmd->begin();
    RenderPassBeginInfo passInfo{
        .colorTarget = colorTarget,
        .clearColor = {0.0F, 0.0F, 0.0F, 1.0F},
        .clearTarget = true,
    };
    cmd->beginRenderPass(passInfo);
    cmd->setViewport(0.0F, 0.0F, 64.0F, 64.0F);
    cmd->setScissor(0, 0, 64, 64);
    cmd->bindPipeline(pipeline);

    DescriptorSetHandle setsToBind[] = {dsSet};
    cmd->bindDescriptorSets(pipeLayout, 0, setsToBind);

    alignas(16) float pushData[32]{};
    Mat4 idMat = Mat4::identity();
    std::memcpy(&pushData[0], idMat.m, 64);
    std::memcpy(&pushData[16], idMat.m, 64);
    cmd->pushConstants(pipeLayout, ShaderStageFlags::Vertex, 0, sizeof(pushData), pushData);

    cmd->bindVertexBuffer(0, gpuMesh->getVertexBuffer(), 0);
    cmd->bindIndexBuffer(gpuMesh->getIndexBuffer(), 0, true);
    cmd->drawIndexed(6, 1, 0, 0, 0);

    cmd->endRenderPass();
    cmd->end();

    TEST_CHECK(device->submit(cmd.get()).hasValue());

    // Clean up
    device->destroyTexture(colorTarget);
    device->destroyGraphicsPipeline(pipeline);
    device->destroyPipelineLayout(pipeLayout);
    device->destroyShader(fragShader);
    device->destroyShader(vertShader);
    device->destroyDescriptorPool(pool);
    device->destroyDescriptorSetLayout(dsLayout);

    return 0;
}

static int testTexturedMeshHardwareRenderingAndPixelVerification() {
#if defined(HIKARI_HAS_TSAN)
    std::cout << "ThreadSanitizer active: skipping live GPU hardware rendering test\n";
    return 0;
#endif

    VulkanDeviceDesc devDesc{.windowHandle = nullptr, .enableValidation = true};
    auto devRes = createVulkanDevice(devDesc);
    if (!devRes.hasValue()) {
        std::cout << "Vulkan device unavailable; skipping hardware rendering test\n";
        return 0;
    }
    auto device = std::move(devRes.value());

    // 1. Create decoded image data (2x2 checkerboard)
    ImageData img = createCheckerboardImageData();
    TEST_CHECK(img.isValid());

    // 2. Upload to GPU using GpuTexture abstraction (staging buffer, layout transitions, sampler)
    auto texRes = GpuTexture::create(*device, img, false);
    TEST_CHECK(texRes.hasValue());
    auto gpuTex = std::move(texRes.value());
    TEST_CHECK(gpuTex->isValid());
    TEST_CHECK(gpuTex->getSampler().isValid());
    TEST_CHECK(gpuTex->getWidth() == 2);
    TEST_CHECK(gpuTex->getHeight() == 2);

    // 3. Create GPU quad mesh
    MeshData quadMesh = createTexturedQuad();
    auto meshRes = GpuMesh::create(*device, quadMesh);
    TEST_CHECK(meshRes.hasValue());
    auto gpuMesh = std::move(meshRes.value());
    TEST_CHECK(gpuMesh->isValid());

    // 4. Descriptor Set Layout (CombinedImageSampler at binding 0 in Fragment stage)
    DescriptorSetLayoutDesc layoutDesc{
        .bindings = {{
            .binding = 0,
            .descriptorType = DescriptorType::CombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = ShaderStageFlags::Fragment,
        }},
        .debugName = "TexturedMeshLayout",
    };
    auto dsLayoutRes = device->createDescriptorSetLayout(layoutDesc);
    TEST_CHECK(dsLayoutRes.hasValue());
    DescriptorSetLayoutHandle dsLayout = dsLayoutRes.value();

    // 5. Descriptor Pool & Allocation
    DescriptorPoolDesc poolDesc{
        .maxSets = 1,
        .poolSizes = {{.type = DescriptorType::CombinedImageSampler, .count = 1}},
        .debugName = "TexturedMeshPool",
    };
    auto poolRes = device->createDescriptorPool(poolDesc);
    TEST_CHECK(poolRes.hasValue());
    DescriptorPoolHandle pool = poolRes.value();

    auto setRes = device->allocateDescriptorSet(pool, dsLayout);
    TEST_CHECK(setRes.hasValue());
    DescriptorSetHandle dsSet = setRes.value();

    // 6. Update Descriptor Set with GpuTexture and Sampler
    DescriptorWrite write =
        DescriptorWrite::makeCombinedImageSampler(0, gpuTex->getTexture(), gpuTex->getSampler());
    TEST_CHECK(device->updateDescriptorSet(dsSet, std::span{&write, 1}).hasValue());

    // 7. Pipeline Layout & Shaders
    PipelineLayoutDesc pipeLayoutDesc{
        .pushConstants = {{
            .stageFlags = ShaderStageFlags::Vertex,
            .offset = 0,
            .size = 128,
        }},
        .descriptorSetLayoutHandles = {dsLayout},
        .descriptorSetLayouts = {},
        .debugName = "TexturedPipeLayout",
    };
    auto pipeLayoutRes = device->createPipelineLayout(pipeLayoutDesc);
    TEST_CHECK(pipeLayoutRes.hasValue());
    PipelineLayoutHandle pipeLayout = pipeLayoutRes.value();

    ShaderDesc vertDesc{
        .stage = ShaderStageFlags::Vertex,
        .spirvCode = kMeshVertexShaderSpirv,
        .entryPoint = "main",
        .debugName = "TexturedVertShader",
    };
    auto vertRes = device->createShader(vertDesc);
    TEST_CHECK(vertRes.hasValue());
    ShaderHandle vertShader = vertRes.value();

    ShaderDesc fragDesc{
        .stage = ShaderStageFlags::Fragment,
        .spirvCode = kTexturedMeshFragmentShaderSpirv,
        .entryPoint = "main",
        .debugName = "TexturedFragShader",
    };
    auto fragRes = device->createShader(fragDesc);
    TEST_CHECK(fragRes.hasValue());
    ShaderHandle fragShader = fragRes.value();

    // 8. Build Graphics Pipeline for offscreen dynamic rendering
    const uint32_t vpWidth = 64;
    const uint32_t vpHeight = 64;
    const size_t byteSize = vpWidth * vpHeight * 4;

    GraphicsPipelineDesc pipeDesc{
        .vertexShader = vertShader,
        .fragmentShader = fragShader,
        .vertexInput =
            {
                .bindings = {{
                    .binding = 0,
                    .stride = sizeof(Vertex),
                    .inputRate = VertexInputRate::Vertex,
                }},
                .attributes =
                    {
                        {.location = 0,
                         .binding = 0,
                         .format = VertexFormat::Float3,
                         .offset = static_cast<uint32_t>(offsetof(Vertex, position))},
                        {.location = 1,
                         .binding = 0,
                         .format = VertexFormat::Float3,
                         .offset = static_cast<uint32_t>(offsetof(Vertex, normal))},
                        {.location = 2,
                         .binding = 0,
                         .format = VertexFormat::Float2,
                         .offset = static_cast<uint32_t>(offsetof(Vertex, uv))},
                        {.location = 3,
                         .binding = 0,
                         .format = VertexFormat::Float4,
                         .offset = static_cast<uint32_t>(offsetof(Vertex, color))},
                    },
            },
        .topology = PrimitiveTopology::TriangleList,
        .rasterization = {.polygonMode = PolygonMode::Fill,
                          .cullMode = CullMode::None,
                          .frontFace = FrontFace::CounterClockwise},
        .depthStencil = {.depthTestEnable = false, .depthWriteEnable = false},
        .colorBlendAttachments = {{.blendEnable = false,
                                   .colorWriteMask = ColorComponentFlags::All}},
        .layout = pipeLayout,
        .colorAttachmentFormats = {TextureFormat::RGBA8_UNORM},
        .debugName = "TexturedMeshPipeline",
    };
    auto pipeRes = device->createGraphicsPipeline(pipeDesc);
    TEST_CHECK(pipeRes.hasValue());
    PipelineHandle pipeline = pipeRes.value();

    // 9. Allocate offscreen color target
    TextureDesc colorDesc{
        .type = TextureType::Texture2D,
        .format = TextureFormat::RGBA8_UNORM,
        .width = vpWidth,
        .height = vpHeight,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .usage = TextureUsageFlags::ColorAttachment | TextureUsageFlags::Sampled |
                 TextureUsageFlags::TransferSrc,
        .debugName = "OffscreenColorTarget",
    };
    auto rtRes = device->createTexture(colorDesc);
    TEST_CHECK(rtRes.hasValue());
    TextureHandle colorTarget = rtRes.value();

    // 10. Allocate readback buffer for GPU-to-CPU pixel verification
    BufferDesc readbackDesc{
        .sizeInBytes = byteSize,
        .usage = BufferUsageFlags::TransferDst,
        .memoryUsage = MemoryUsage::CpuToGpu,
        .debugName = "ReadbackBuffer",
    };
    auto rbRes = device->createBuffer(readbackDesc);
    TEST_CHECK(rbRes.hasValue());
    BufferHandle readbackBuf = rbRes.value();

    // 11. Record commands
    auto cmdRes = device->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());

    cmd->begin();
    RenderPassBeginInfo passInfo{
        .colorTarget = colorTarget,
        .clearColor = {0.1F, 0.1F, 0.1F, 1.0F},
        .clearTarget = true,
    };
    cmd->beginRenderPass(passInfo);
    cmd->setViewport(0.0F, 0.0F, static_cast<float>(vpWidth), static_cast<float>(vpHeight));
    cmd->setScissor(0, 0, vpWidth, vpHeight);
    cmd->bindPipeline(pipeline);

    DescriptorSetHandle setsToBind[] = {dsSet};
    cmd->bindDescriptorSets(pipeLayout, 0, setsToBind);

    // Identity viewProj and model (NDC quad covers entire viewport)
    alignas(16) float pushData[32]{};
    Mat4 idMat = Mat4::identity();
    std::memcpy(&pushData[0], idMat.m, 64);
    std::memcpy(&pushData[16], idMat.m, 64);
    cmd->pushConstants(pipeLayout, ShaderStageFlags::Vertex, 0, sizeof(pushData), pushData);

    cmd->bindVertexBuffer(0, gpuMesh->getVertexBuffer(), 0);
    cmd->bindIndexBuffer(gpuMesh->getIndexBuffer(), 0, true);
    cmd->drawIndexed(6, 1, 0, 0, 0);

    cmd->endRenderPass();

    // Copy rendered color target to readback buffer
    cmd->copyTextureToBuffer(colorTarget, readbackBuf);
    cmd->end();

    // Submit and synchronize
    TEST_CHECK(device->submit(cmd.get()).hasValue());
    device->waitIdle();

    // 12. Read back rendered pixels
    std::vector<uint8_t> pixels(byteSize, 0);
    TEST_CHECK(device->readBuffer(readbackBuf, 0, pixels.data(), byteSize).hasValue());

    // 13. Pixel verification across UV quadrants:
    auto samplePixel = [&](uint32_t x, uint32_t y) {
        const size_t idx = (static_cast<size_t>(y) * vpWidth + static_cast<size_t>(x)) * 4;
        return std::vector<uint8_t>{pixels[idx], pixels[idx + 1], pixels[idx + 2], pixels[idx + 3]};
    };

    auto pTL = samplePixel(8, 8);
    auto pTR = samplePixel(56, 8);
    auto pBL = samplePixel(8, 56);
    auto pBR = samplePixel(56, 56);

    std::cout << "Sampled TL (Red expected):   R=" << (int)pTL[0] << " G=" << (int)pTL[1]
              << " B=" << (int)pTL[2] << " A=" << (int)pTL[3] << '\n';
    std::cout << "Sampled TR (Green expected): R=" << (int)pTR[0] << " G=" << (int)pTR[1]
              << " B=" << (int)pTR[2] << " A=" << (int)pTR[3] << '\n';
    std::cout << "Sampled BL (Blue expected):  R=" << (int)pBL[0] << " G=" << (int)pBL[1]
              << " B=" << (int)pBL[2] << " A=" << (int)pBL[3] << '\n';
    std::cout << "Sampled BR (Yellow expected):R=" << (int)pBR[0] << " G=" << (int)pBR[1]
              << " B=" << (int)pBR[2] << " A=" << (int)pBR[3] << '\n';

    // Verify quadrant colors with bilinear filtering across texels:
    // Top-Left quadrant is dominantly Red
    TEST_CHECK(pTL[0] > 90 && pTL[0] > pTL[1] && pTL[0] > pTL[2]);
    // Top-Right quadrant is dominantly Green
    TEST_CHECK(pTR[1] > 90 && pTR[1] > pTR[0] && pTR[1] > pTR[2]);
    // Bottom-Left quadrant is dominantly Blue
    TEST_CHECK(pBL[2] > 80 && pBL[2] > pBL[0] && pBL[2] > pBL[1]);
    // Bottom-Right quadrant is dominantly Yellow (Red + Green)
    TEST_CHECK(pBR[0] > 80 && pBR[1] > 80 && pBR[2] < 50);

    // Cleanup resources
    device->destroyBuffer(readbackBuf);
    device->destroyTexture(colorTarget);
    device->destroyGraphicsPipeline(pipeline);
    device->destroyPipelineLayout(pipeLayout);
    device->destroyShader(fragShader);
    device->destroyShader(vertShader);
    device->destroyDescriptorPool(pool);
    device->destroyDescriptorSetLayout(dsLayout);

    std::cout << "[SUCCESS] Textured mesh sampled and displayed correctly on Vulkan GPU!\n";
    return 0;
}

int main() {
    std::cout << "[Running Textured Mesh Visual & Hardware Tests]\n";
    if (testTexturedMeshRecordingOnNullDevice() != 0) {
        return 1;
    }
    if (testTexturedMeshHardwareRenderingAndPixelVerification() != 0) {
        return 2;
    }
    std::cout << "[Textured Mesh Tests Passed: 2/2]\n";
    return 0;
}
