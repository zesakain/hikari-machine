/// @file mesh.h
/// @brief CPU-side geometric mesh representation, vertex layout, and bounding volumes.
///
/// Purpose:
/// Defines hardware-independent 3D mesh geometry, submesh index intervals, and bounds.
/// Used by importers, offline asset cooking, physics collider generation, and GPU uploads.
///
/// Ownership and Lifetime:
/// MeshData owns its vertex and index buffers. Safe to move and copy.
///
/// Thread Safety:
/// Value type with const accessors; thread-safe for concurrent read access.

#pragma once

#include "core/math.h"
#include "core/result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eng::render {

/// @struct Vertex
/// @brief Interleaved vertex attribute format for standard 3D rendering.
struct Vertex {
    Vec3 position{Vec3::zero()};
    Vec3 normal{Vec3::unitY()};
    Vec2 uv{Vec2::zero()};
    Vec4 color{1.0F, 1.0F, 1.0F, 1.0F};

    constexpr Vertex() noexcept = default;
    constexpr Vertex(const Vec3& pos, const Vec3& norm, const Vec2& texCoord,
                     const Vec4& col = Vec4{1.0F, 1.0F, 1.0F, 1.0F}) noexcept
        : position(pos), normal(norm), uv(texCoord), color(col) {}

    [[nodiscard]] bool operator==(const Vertex& rhs) const noexcept {
        return position.isNearlyEqual(rhs.position) && normal.isNearlyEqual(rhs.normal) &&
               uv.isNearlyEqual(rhs.uv) && color.isNearlyEqual(rhs.color);
    }
};

/// @struct Submesh
/// @brief Partition of index buffer referencing a continuous surface range.
struct Submesh {
    std::string name{"DefaultSubmesh"};
    uint32_t firstIndex{0};
    uint32_t indexCount{0};
    uint32_t vertexOffset{0};
    AABB bounds{};
};

/// @struct MeshData
/// @brief Complete CPU-side polygon mesh representation with validation and bounding utilities.
struct MeshData {
    std::string name{"Mesh"};
    std::vector<Vertex> vertices{};
    std::vector<uint32_t> indices{};
    std::vector<Submesh> submeshes{};
    AABB bounds{};

    MeshData() = default;
    explicit MeshData(std::string inName) : name(std::move(inName)) {}

    [[nodiscard]] const std::string& getName() const noexcept { return name; }
    void setName(std::string inName) { name = std::move(inName); }

    [[nodiscard]] const std::vector<Vertex>& getVertices() const noexcept { return vertices; }
    [[nodiscard]] std::vector<Vertex>& getMutableVertices() noexcept { return vertices; }

    [[nodiscard]] const std::vector<uint32_t>& getIndices() const noexcept { return indices; }
    [[nodiscard]] std::vector<uint32_t>& getMutableIndices() noexcept { return indices; }

    [[nodiscard]] const std::vector<Submesh>& getSubmeshes() const noexcept { return submeshes; }
    [[nodiscard]] std::vector<Submesh>& getMutableSubmeshes() noexcept { return submeshes; }

    [[nodiscard]] const AABB& getBounds() const noexcept { return bounds; }

    [[nodiscard]] uint32_t getVertexCount() const noexcept {
        return static_cast<uint32_t>(vertices.size());
    }

    [[nodiscard]] uint32_t getIndexCount() const noexcept {
        return static_cast<uint32_t>(indices.size());
    }

    [[nodiscard]] uint32_t getTriangleCount() const noexcept {
        return static_cast<uint32_t>(indices.size() / 3);
    }

    /// Evaluates whether the mesh geometry is mathematically valid.
    ///
    /// Validates:
    /// - Non-empty vertices and indices.
    /// - Index count is a multiple of 3 (triangulated).
    /// - All indices point to valid vertex array offsets.
    /// - Submesh bounds and ranges are within indices bounds.
    [[nodiscard]] Result<void> validate() const noexcept;

    /// Recalculates exact local AABB bounds from vertex positions.
    void recalculateBounds() noexcept;

    /// Computes smooth area-weighted vertex normals from polygon faces if missing or zero.
    void generateNormals() noexcept;

    /// Clears all vertices, indices, and submeshes.
    void clear() noexcept {
        vertices.clear();
        indices.clear();
        submeshes.clear();
        bounds = AABB{};
    }
};

} // namespace eng::render
