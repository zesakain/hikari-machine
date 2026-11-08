/// @file scene_renderer.cpp
/// @brief Scene mesh rendering traversal and command recording implementation.

#include "render/scene_renderer.h"

#include "physics/physics_components.h"

namespace eng::render {

RenderStats SceneRenderer::renderScene(world::World& world, AssetManager& assetManager,
                                       rhi::RhiDevice& device, rhi::RhiCommandList& cmdList) {
    RenderStats stats{};
    auto& ecs = world.getRawWorld();

    auto query = ecs.query<const MeshComponent>();
    query.each([&](flecs::entity /*e*/, const MeshComponent& meshComp) {
        if (!meshComp.visible) {
            return;
        }

        AssetId meshId = meshComp.meshAsset;
        if (!meshId.isValid() && !meshComp.assetPath.empty()) {
            meshId = assetManager.findAssetByPath(meshComp.assetPath);
            if (!meshId.isValid()) {
                auto loadRes = assetManager.loadMesh(meshComp.assetPath);
                if (loadRes.hasValue()) {
                    meshId = loadRes.value();
                }
            }
        }

        if (!meshId.isValid()) {
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

        // Bind vertex buffer
        cmdList.bindVertexBuffer(0, gpuMesh->getVertexBuffer(), 0);

        // Bind index buffer if present
        if (gpuMesh->getIndexBuffer().isValid() && gpuMesh->getIndexCount() > 0) {
            cmdList.bindIndexBuffer(gpuMesh->getIndexBuffer(), 0, true);

            const auto& submeshes = gpuMesh->getSubmeshes();
            if (meshComp.submeshIndex < submeshes.size()) {
                const auto& sub = submeshes[meshComp.submeshIndex];
                cmdList.drawIndexed(sub.indexCount, 1, sub.firstIndex, sub.vertexOffset, 0);
                stats.drawCalls++;
                stats.triangles += sub.indexCount / 3;
            } else {
                cmdList.drawIndexed(gpuMesh->getIndexCount(), 1, 0, 0, 0);
                stats.drawCalls++;
                stats.triangles += gpuMesh->getIndexCount() / 3;
            }
        } else if (gpuMesh->getVertexCount() > 0) {
            cmdList.draw(gpuMesh->getVertexCount(), 1, 0, 0);
            stats.drawCalls++;
            stats.triangles += gpuMesh->getVertexCount() / 3;
        }

        stats.meshesRendered++;
    });

    return stats;
}

} // namespace eng::render
