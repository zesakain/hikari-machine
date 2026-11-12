/// @file render_graph_test.cpp
/// @brief Unit tests for Render Graph DAG compilation, cycle detection, and headless execution.

#include "render/asset_manager.h"
#include "render/render_graph.h"
#include "render/scene_renderer.h"
#include "rhi/rhi_device.h"
#include "world/world.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using namespace eng;
using namespace eng::render;
using namespace eng::rhi;

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << '\n'; \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

static int testLinearOrdering() {
    RenderGraph graph;

    auto texA = graph.createTransientTexture("TexA", TextureDesc{.width = 256, .height = 256});
    auto texB = graph.createTransientTexture("TexB", TextureDesc{.width = 256, .height = 256});

    std::vector<std::string> executedPasses;

    // Declare passes in reverse chronological order intentionally
    graph.addPass(
        "Pass3",
        [&](RenderPassBuilder& builder) { builder.readTexture(texB, TextureUsageFlags::Sampled); },
        [&](RhiCommandList& /*cmd*/) { executedPasses.emplace_back("Pass3"); });

    graph.addPass(
        "Pass2",
        [&](RenderPassBuilder& builder) {
            builder.readTexture(texA, TextureUsageFlags::Sampled);
            builder.writeTexture(texB, TextureUsageFlags::ColorAttachment);
        },
        [&](RhiCommandList& /*cmd*/) { executedPasses.emplace_back("Pass2"); });

    graph.addPass(
        "Pass1",
        [&](RenderPassBuilder& builder) {
            builder.writeTexture(texA, TextureUsageFlags::ColorAttachment);
        },
        [&](RhiCommandList& /*cmd*/) { executedPasses.emplace_back("Pass1"); });

    auto compileRes = graph.compile();
    TEST_CHECK(compileRes.hasValue());
    TEST_CHECK(graph.isCompiled());

    const auto& order = graph.getExecutionOrder();
    TEST_CHECK(order.size() == 3);
    // Index 2 is Pass1, Index 1 is Pass2, Index 0 is Pass3
    TEST_CHECK(order[0] == 2); // Pass1
    TEST_CHECK(order[1] == 1); // Pass2
    TEST_CHECK(order[2] == 0); // Pass3

    // Execute with NullDevice
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());

    auto cmdRes = device->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());

    auto execRes = graph.execute(*device, *cmd);
    TEST_CHECK(execRes.hasValue());

    TEST_CHECK(executedPasses.size() == 3);
    TEST_CHECK(executedPasses[0] == "Pass1");
    TEST_CHECK(executedPasses[1] == "Pass2");
    TEST_CHECK(executedPasses[2] == "Pass3");

    return 0;
}

static int testDiamondDependencyOrdering() {
    RenderGraph graph;

    auto texRoot = graph.createTransientTexture("Root", TextureDesc{.width = 128, .height = 128});
    auto texBranchA =
        graph.createTransientTexture("BranchA", TextureDesc{.width = 128, .height = 128});
    auto texBranchB =
        graph.createTransientTexture("BranchB", TextureDesc{.width = 128, .height = 128});
    auto texSink = graph.createTransientTexture("Sink", TextureDesc{.width = 128, .height = 128});

    std::vector<std::string> executedPasses;

    graph.addPass(
        "SinkPass",
        [&](RenderPassBuilder& builder) {
            builder.readTexture(texBranchA, TextureUsageFlags::Sampled);
            builder.readTexture(texBranchB, TextureUsageFlags::Sampled);
            builder.writeTexture(texSink, TextureUsageFlags::ColorAttachment);
        },
        [&](RhiCommandList& /*cmd*/) { executedPasses.emplace_back("SinkPass"); });

    graph.addPass(
        "BranchAPass",
        [&](RenderPassBuilder& builder) {
            builder.readTexture(texRoot, TextureUsageFlags::Sampled);
            builder.writeTexture(texBranchA, TextureUsageFlags::ColorAttachment);
        },
        [&](RhiCommandList& /*cmd*/) { executedPasses.emplace_back("BranchAPass"); });

    graph.addPass(
        "BranchBPass",
        [&](RenderPassBuilder& builder) {
            builder.readTexture(texRoot, TextureUsageFlags::Sampled);
            builder.writeTexture(texBranchB, TextureUsageFlags::ColorAttachment);
        },
        [&](RhiCommandList& /*cmd*/) { executedPasses.emplace_back("BranchBPass"); });

    graph.addPass(
        "RootPass",
        [&](RenderPassBuilder& builder) {
            builder.writeTexture(texRoot, TextureUsageFlags::ColorAttachment);
        },
        [&](RhiCommandList& /*cmd*/) { executedPasses.emplace_back("RootPass"); });

    auto compileRes = graph.compile();
    TEST_CHECK(compileRes.hasValue());

    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());

    auto cmdRes = device->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());

    auto execRes = graph.execute(*device, *cmd);
    TEST_CHECK(execRes.hasValue());

    TEST_CHECK(executedPasses.size() == 4);
    TEST_CHECK(executedPasses.front() == "RootPass");
    TEST_CHECK(executedPasses.back() == "SinkPass");

    return 0;
}

static int testCycleDetection() {
    RenderGraph graph;

    auto tex1 = graph.createTransientTexture("Tex1", TextureDesc{.width = 64, .height = 64});
    auto tex2 = graph.createTransientTexture("Tex2", TextureDesc{.width = 64, .height = 64});

    // PassA reads Tex2, writes Tex1
    graph.addPass(
        "PassA",
        [&](RenderPassBuilder& builder) {
            builder.readTexture(tex2, TextureUsageFlags::Sampled);
            builder.writeTexture(tex1, TextureUsageFlags::ColorAttachment);
        },
        [](RhiCommandList& /*cmd*/) {});

    // PassB reads Tex1, writes Tex2 (creates cyclic dependency: PassA -> PassB -> PassA)
    graph.addPass(
        "PassB",
        [&](RenderPassBuilder& builder) {
            builder.readTexture(tex1, TextureUsageFlags::Sampled);
            builder.writeTexture(tex2, TextureUsageFlags::ColorAttachment);
        },
        [](RhiCommandList& /*cmd*/) {});

    auto compileRes = graph.compile();
    TEST_CHECK(!compileRes.hasValue());
    TEST_CHECK(compileRes.error().code == ErrorCode::RenderGraphCompilationFailed);
    TEST_CHECK(!graph.isCompiled());

    return 0;
}

static int testResourceLifetime() {
    RenderGraph graph;

    auto texA = graph.createTransientTexture("TexA", TextureDesc{.width = 64, .height = 64});
    auto texB = graph.createTransientTexture("TexB", TextureDesc{.width = 64, .height = 64});
    auto texC = graph.createTransientTexture("TexC", TextureDesc{.width = 64, .height = 64});

    // Step 0
    graph.addPass(
        "Pass0",
        [&](RenderPassBuilder& builder) {
            builder.writeTexture(texA, TextureUsageFlags::ColorAttachment);
        },
        [](RhiCommandList& /*cmd*/) {});

    // Step 1
    graph.addPass(
        "Pass1",
        [&](RenderPassBuilder& builder) {
            builder.readTexture(texA, TextureUsageFlags::Sampled);
            builder.writeTexture(texB, TextureUsageFlags::ColorAttachment);
        },
        [](RhiCommandList& /*cmd*/) {});

    // Step 2
    graph.addPass(
        "Pass2",
        [&](RenderPassBuilder& builder) {
            builder.readTexture(texB, TextureUsageFlags::Sampled);
            builder.writeTexture(texC, TextureUsageFlags::ColorAttachment);
        },
        [](RhiCommandList& /*cmd*/) {});

    auto compileRes = graph.compile();
    TEST_CHECK(compileRes.hasValue());

    auto lifeA = graph.getTextureLifetime(texA);
    TEST_CHECK(lifeA.firstPass == 0);
    TEST_CHECK(lifeA.lastPass == 1);

    auto lifeC = graph.getTextureLifetime(texC);
    TEST_CHECK(lifeC.firstPass == 2);
    TEST_CHECK(lifeC.lastPass == 2);

    // Lifetimes of A (0..1) and C (2..2) do not overlap
    TEST_CHECK(!lifeA.overlaps(lifeC));

    return 0;
}

static int testGraphExecutionWithNullDevice() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());

    auto cmdRes = device->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmdList = std::move(cmdRes.value());

    RenderGraph graph;
    auto texColor = graph.createTransientTexture(
        "ColorTarget",
        TextureDesc{.type = TextureType::Texture2D,
                    .format = TextureFormat::RGBA8_UNORM,
                    .width = 1920,
                    .height = 1080,
                    .usage = TextureUsageFlags::ColorAttachment | TextureUsageFlags::Sampled});

    bool pass1Ran = false;
    bool pass2Ran = false;

    graph.addPass(
        "RenderScene",
        [&](RenderPassBuilder& builder) {
            builder.writeTexture(texColor, TextureUsageFlags::ColorAttachment);
        },
        [&](RhiCommandList& cmd) {
            pass1Ran = true;
            cmd.setViewport(0.0F, 0.0F, 1920.0F, 1080.0F);
        });

    graph.addPass(
        "PostProcess",
        [&](RenderPassBuilder& builder) {
            builder.readTexture(texColor, TextureUsageFlags::Sampled);
        },
        [&](RhiCommandList& cmd) {
            pass2Ran = true;
            cmd.setScissor(0, 0, 1920, 1080);
        });

    auto compileRes = graph.compile();
    TEST_CHECK(compileRes.hasValue());

    cmdList->begin();
    auto execRes = graph.execute(*device, *cmdList);
    TEST_CHECK(execRes.hasValue());
    cmdList->end();

    TEST_CHECK(pass1Ran);
    TEST_CHECK(pass2Ran);

    auto submitRes = device->submit(cmdList.get());
    TEST_CHECK(submitRes.hasValue());
    device->waitIdle();

    return 0;
}

static int testSceneRendererRenderGraphIntegration() {
    auto devRes = createNullDevice();
    TEST_CHECK(devRes.hasValue());
    auto device = std::move(devRes.value());

    SceneRenderer renderer;
    auto initRes = renderer.init(*device, 640, 480);
    TEST_CHECK(initRes.hasValue());

    // 1. Verify Scene RenderGraph compilation & pass count
    const auto& sceneGraph = renderer.getRenderGraph();
    TEST_CHECK(sceneGraph.isCompiled());
    TEST_CHECK(sceneGraph.getPassCount() == 2); // DirectionalShadowPass, MainPbrPass

    const auto& sceneOrder = sceneGraph.getExecutionOrder();
    TEST_CHECK(sceneOrder.size() == 2);
    // Index 0 should be DirectionalShadowPass, Index 1 MainPbrPass
    TEST_CHECK(sceneOrder[0] == 0);
    TEST_CHECK(sceneOrder[1] == 1);

    // 2. Verify PostProcess RenderGraph compilation & pass count when enabled
    renderer.setPostProcessingEnabled(true);
    const auto& ppGraph = renderer.getPostProcessRenderGraph();
    TEST_CHECK(ppGraph.isCompiled());
    TEST_CHECK(ppGraph.getPassCount() == 8); // 4 downsample + 3 upsample + 1 tone mapping

    // 3. Test dynamically disabling shadow pass alters RenderGraph structure
    GraphicsSettings settings = renderer.getGraphicsSettings();
    settings.shadows.enabled = false;
    auto applyRes = renderer.applyGraphicsSettings(settings);
    TEST_CHECK(applyRes.hasValue());

    TEST_CHECK(renderer.getRenderGraph().isCompiled());
    TEST_CHECK(renderer.getRenderGraph().getPassCount() == 1); // MainPbrPass only

    // 4. Test headless execution through RenderGraph
    world::World world;
    registerRenderComponents(world);
    AssetManager assetMgr;

    auto cmdRes = device->createCommandList();
    TEST_CHECK(cmdRes.hasValue());
    auto cmd = std::move(cmdRes.value());

    cmd->begin();
    RenderStats stats = renderer.renderScene(world, assetMgr, *device, *cmd);
    renderer.renderPostProcess(*cmd);
    cmd->end();

    TEST_CHECK(stats.drawCalls == 0);
    TEST_CHECK(stats.meshesRendered == 0);

    renderer.shutdown();
    std::cout << "SceneRenderer live RenderGraph integration verified!\n";
    return 0;
}

int main() {
    if (testLinearOrdering() != 0) {
        return 1;
    }
    if (testDiamondDependencyOrdering() != 0) {
        return 1;
    }
    if (testCycleDetection() != 0) {
        return 1;
    }
    if (testResourceLifetime() != 0) {
        return 1;
    }
    if (testGraphExecutionWithNullDevice() != 0) {
        return 1;
    }
    if (testSceneRendererRenderGraphIntegration() != 0) {
        return 1;
    }
    std::cout << "All Render Graph unit tests passed successfully.\n";
    return 0;
}
