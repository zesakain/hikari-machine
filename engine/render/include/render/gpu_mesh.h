/// @file gpu_mesh.h
/// @brief GPU buffer representation and lifecycle management for mesh geometry.
///
/// Purpose:
/// Owns and manages the GPU vertex and index buffers allocated for a MeshData object.
/// Provides RAII destruction via the owning RhiDevice.
///
/// Ownership and Lifetime:
/// Created by GpuMesh::create(RhiDevice&, const MeshData&).
/// Owned typically by AssetManager via std::unique_ptr<GpuMesh>.
/// Automatically destroys GPU buffers on destruction.
///
/// Thread-safety:
/// Main thread / render thread only. Not safe for concurrent mutation.
///
/// Performance:
/// GPU buffers are allocated in host-visible memory (MemoryUsage::CpuToGpu) for easy upload.
/// Zero per-frame allocations during binding and drawing.

#pragma once

#include "core/error.h"
#include "core/math.h"
#include "core/result.h"
#include "render/mesh.h"
#include "rhi/buffer.h"
#include "rhi/rhi_device.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace eng::render {

/// @class GpuMesh
/// @brief RAII wrapper for GPU vertex and index buffers representing a 3D mesh.
class GpuMesh {
public:
    /// @brief Factory function creating GPU buffers and uploading mesh vertex and index data.
    ///
    /// @param device The RHI device used to allocate and upload buffers.
    /// @param meshData Validated CPU mesh geometry data.
    /// @return Pointer to new GpuMesh on success, or Error on failure.
    [[nodiscard]] static Result<std::unique_ptr<GpuMesh>> create(rhi::RhiDevice& device,
                                                                 const MeshData& meshData);

    ~GpuMesh() noexcept;

    GpuMesh(const GpuMesh&) = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;
    GpuMesh(GpuMesh&& other) noexcept;
    GpuMesh& operator=(GpuMesh&& other) noexcept;

    [[nodiscard]] rhi::BufferHandle getVertexBuffer() const noexcept { return vertexBuffer_; }
    [[nodiscard]] rhi::BufferHandle getIndexBuffer() const noexcept { return indexBuffer_; }
    [[nodiscard]] uint32_t getVertexCount() const noexcept { return vertexCount_; }
    [[nodiscard]] uint32_t getIndexCount() const noexcept { return indexCount_; }
    [[nodiscard]] const std::vector<Submesh>& getSubmeshes() const noexcept { return submeshes_; }
    [[nodiscard]] const AABB& getBounds() const noexcept { return bounds_; }
    [[nodiscard]] bool isValid() const noexcept { return vertexBuffer_.isValid(); }

private:
    GpuMesh(rhi::RhiDevice& device, rhi::BufferHandle vb, rhi::BufferHandle ib, uint32_t vCount,
            uint32_t iCount, std::vector<Submesh> submeshes, const AABB& bounds) noexcept;

    rhi::RhiDevice* device_{nullptr};
    rhi::BufferHandle vertexBuffer_{};
    rhi::BufferHandle indexBuffer_{};
    uint32_t vertexCount_{0};
    uint32_t indexCount_{0};
    std::vector<Submesh> submeshes_{};
    AABB bounds_{};
};

} // namespace eng::render
