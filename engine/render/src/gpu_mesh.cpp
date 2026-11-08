/// @file gpu_mesh.cpp
/// @brief Implementation of GpuMesh RAII buffer management.

#include "render/gpu_mesh.h"

#include <utility>

namespace eng::render {

GpuMesh::GpuMesh(rhi::RhiDevice& device, rhi::BufferHandle vb, rhi::BufferHandle ib,
                 uint32_t vCount, uint32_t iCount, std::vector<Submesh> submeshes,
                 const AABB& bounds) noexcept
    : device_(&device), vertexBuffer_(vb), indexBuffer_(ib), vertexCount_(vCount),
      indexCount_(iCount), submeshes_(std::move(submeshes)), bounds_(bounds) {}

GpuMesh::~GpuMesh() noexcept {
    if (device_ != nullptr) {
        if (vertexBuffer_.isValid()) {
            device_->destroyBuffer(vertexBuffer_);
            vertexBuffer_ = {};
        }
        if (indexBuffer_.isValid()) {
            device_->destroyBuffer(indexBuffer_);
            indexBuffer_ = {};
        }
    }
}

GpuMesh::GpuMesh(GpuMesh&& other) noexcept
    : device_(other.device_), vertexBuffer_(other.vertexBuffer_), indexBuffer_(other.indexBuffer_),
      vertexCount_(other.vertexCount_), indexCount_(other.indexCount_),
      submeshes_(std::move(other.submeshes_)), bounds_(other.bounds_) {
    other.device_ = nullptr;
    other.vertexBuffer_ = {};
    other.indexBuffer_ = {};
    other.vertexCount_ = 0;
    other.indexCount_ = 0;
}

GpuMesh& GpuMesh::operator=(GpuMesh&& other) noexcept {
    if (this != &other) {
        if (device_ != nullptr) {
            if (vertexBuffer_.isValid()) {
                device_->destroyBuffer(vertexBuffer_);
            }
            if (indexBuffer_.isValid()) {
                device_->destroyBuffer(indexBuffer_);
            }
        }

        device_ = other.device_;
        vertexBuffer_ = other.vertexBuffer_;
        indexBuffer_ = other.indexBuffer_;
        vertexCount_ = other.vertexCount_;
        indexCount_ = other.indexCount_;
        submeshes_ = std::move(other.submeshes_);
        bounds_ = other.bounds_;

        other.device_ = nullptr;
        other.vertexBuffer_ = {};
        other.indexBuffer_ = {};
        other.vertexCount_ = 0;
        other.indexCount_ = 0;
    }
    return *this;
}

Result<std::unique_ptr<GpuMesh>> GpuMesh::create(rhi::RhiDevice& device, const MeshData& meshData) {
    auto valRes = meshData.validate();
    if (!valRes.hasValue()) {
        return valRes.error();
    }

    const size_t vbBytes = meshData.vertices.size() * sizeof(Vertex);
    const rhi::BufferDesc vbDesc{
        .sizeInBytes = vbBytes,
        .usage = rhi::BufferUsageFlags::Vertex,
        .memoryUsage = rhi::MemoryUsage::CpuToGpu,
        .debugName = "GpuMesh_VertexBuffer",
    };

    auto vbRes = device.createBuffer(vbDesc);
    if (!vbRes.hasValue()) {
        return vbRes.error();
    }
    const rhi::BufferHandle vbHandle = vbRes.value();

    auto writeVbRes = device.writeBuffer(vbHandle, 0, meshData.vertices.data(), vbBytes);
    if (!writeVbRes.hasValue()) {
        device.destroyBuffer(vbHandle);
        return writeVbRes.error();
    }

    rhi::BufferHandle ibHandle{};
    if (!meshData.indices.empty()) {
        const size_t ibBytes = meshData.indices.size() * sizeof(uint32_t);
        const rhi::BufferDesc ibDesc{
            .sizeInBytes = ibBytes,
            .usage = rhi::BufferUsageFlags::Index,
            .memoryUsage = rhi::MemoryUsage::CpuToGpu,
            .debugName = "GpuMesh_IndexBuffer",
        };

        auto ibRes = device.createBuffer(ibDesc);
        if (!ibRes.hasValue()) {
            device.destroyBuffer(vbHandle);
            return ibRes.error();
        }
        ibHandle = ibRes.value();

        auto writeIbRes = device.writeBuffer(ibHandle, 0, meshData.indices.data(), ibBytes);
        if (!writeIbRes.hasValue()) {
            device.destroyBuffer(ibHandle);
            device.destroyBuffer(vbHandle);
            return writeIbRes.error();
        }
    }

    auto gpuMesh = std::unique_ptr<GpuMesh>(new GpuMesh(
        device, vbHandle, ibHandle, static_cast<uint32_t>(meshData.vertices.size()),
        static_cast<uint32_t>(meshData.indices.size()), meshData.submeshes, meshData.bounds));

    return Result<std::unique_ptr<GpuMesh>>::success(std::move(gpuMesh));
}

} // namespace eng::render
