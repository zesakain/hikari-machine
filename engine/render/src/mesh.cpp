/// @file mesh.cpp
/// @brief CPU mesh data validation, bounding box calculation, and normal generation.

#include "render/mesh.h"

#include <cmath>

namespace eng::render {

Result<void> MeshData::validate() const noexcept {
    if (vertices.empty()) {
        return Error{.code = ErrorCode::InvalidArgument, .message = "Mesh vertex buffer is empty"};
    }

    if (indices.empty()) {
        return Error{.code = ErrorCode::InvalidArgument, .message = "Mesh index buffer is empty"};
    }

    if ((indices.size() % 3) != 0) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Mesh index count is not a multiple of 3 (not triangulated)"};
    }

    const auto vertexCount = static_cast<uint32_t>(vertices.size());
    for (size_t i = 0; i < indices.size(); ++i) {
        if (indices[i] >= vertexCount) {
            return Error{.code = ErrorCode::OutOfBounds,
                         .message = "Mesh index references non-existent vertex"};
        }
    }

    const auto indexCount = static_cast<uint32_t>(indices.size());
    for (const auto& submesh : submeshes) {
        if (submesh.firstIndex + submesh.indexCount > indexCount) {
            return Error{.code = ErrorCode::OutOfBounds,
                         .message = "Submesh range exceeds mesh index buffer bounds"};
        }
    }

    return Result<void>::success();
}

void MeshData::recalculateBounds() noexcept {
    bounds = AABB{};
    for (const auto& v : vertices) {
        bounds.expand(v.position);
    }

    for (auto& submesh : submeshes) {
        submesh.bounds = AABB{};
        const uint32_t endIdx = submesh.firstIndex + submesh.indexCount;
        for (uint32_t i = submesh.firstIndex; i < endIdx && i < indices.size(); ++i) {
            const uint32_t vIdx = indices[i];
            if (vIdx < vertices.size()) {
                submesh.bounds.expand(vertices[vIdx].position);
            }
        }
    }
}

void MeshData::generateNormals() noexcept {
    if (vertices.empty() || indices.size() < 3) {
        return;
    }

    // Reset normals to zero
    for (auto& v : vertices) {
        v.normal = Vec3::zero();
    }

    // Accumulate face normals
    const size_t triCount = indices.size() / 3;
    for (size_t t = 0; t < triCount; ++t) {
        const uint32_t i0 = indices[(t * 3) + 0];
        const uint32_t i1 = indices[(t * 3) + 1];
        const uint32_t i2 = indices[(t * 3) + 2];

        if (i0 < vertices.size() && i1 < vertices.size() && i2 < vertices.size()) {
            const Vec3& p0 = vertices[i0].position;
            const Vec3& p1 = vertices[i1].position;
            const Vec3& p2 = vertices[i2].position;

            const Vec3 edge1 = p1 - p0;
            const Vec3 edge2 = p2 - p0;
            const Vec3 faceNormal = Vec3::cross(edge1, edge2);

            vertices[i0].normal += faceNormal;
            vertices[i1].normal += faceNormal;
            vertices[i2].normal += faceNormal;
        }
    }

    // Normalize
    for (auto& v : vertices) {
        const float lenSq = v.normal.lengthSq();
        if (lenSq > 1e-12F) {
            v.normal = v.normal.normalized();
        } else {
            v.normal = Vec3::unitY();
        }
    }
}

void MeshData::generateTangents() noexcept {
    if (vertices.empty() || indices.size() < 3) {
        return;
    }

    std::vector<Vec3> tan1(vertices.size(), Vec3::zero());
    std::vector<Vec3> tan2(vertices.size(), Vec3::zero());

    const size_t triCount = indices.size() / 3;
    for (size_t t = 0; t < triCount; ++t) {
        const uint32_t i0 = indices[(t * 3) + 0];
        const uint32_t i1 = indices[(t * 3) + 1];
        const uint32_t i2 = indices[(t * 3) + 2];

        if (i0 < vertices.size() && i1 < vertices.size() && i2 < vertices.size()) {
            const Vertex& v0 = vertices[i0];
            const Vertex& v1 = vertices[i1];
            const Vertex& v2 = vertices[i2];

            const Vec3 e1 = v1.position - v0.position;
            const Vec3 e2 = v2.position - v0.position;

            const float deltaU1 = v1.uv.x - v0.uv.x;
            const float deltaV1 = v1.uv.y - v0.uv.y;
            const float deltaU2 = v2.uv.x - v0.uv.x;
            const float deltaV2 = v2.uv.y - v0.uv.y;

            const float det = (deltaU1 * deltaV2) - (deltaU2 * deltaV1);
            if (std::abs(det) > 1e-12F) {
                const float invDet = 1.0F / det;
                const Vec3 sdir = ((e1 * deltaV2) - (e2 * deltaV1)) * invDet;
                const Vec3 tdir = ((e2 * deltaU1) - (e1 * deltaU2)) * invDet;

                tan1[i0] += sdir;
                tan1[i1] += sdir;
                tan1[i2] += sdir;

                tan2[i0] += tdir;
                tan2[i1] += tdir;
                tan2[i2] += tdir;
            }
        }
    }

    for (size_t i = 0; i < vertices.size(); ++i) {
        const Vec3& n = vertices[i].normal;
        const Vec3& t = tan1[i];

        if (t.lengthSq() > 1e-12F) {
            // Gram-Schmidt orthogonalization: t' = normalize(t - n * dot(n, t))
            Vec3 orthoT = (t - (n * Vec3::dot(n, t))).normalized();
            if (orthoT.lengthSq() <= 1e-12F) {
                Vec3 c1 = Vec3::cross(n, Vec3(0.0F, 0.0F, 1.0F));
                Vec3 c2 = Vec3::cross(n, Vec3(0.0F, 1.0F, 0.0F));
                orthoT = (c1.lengthSq() > c2.lengthSq()) ? c1.normalized() : c2.normalized();
            }
            // Calculate handedness (w): sign of dot(cross(n, t), tan2)
            const float w = (Vec3::dot(Vec3::cross(n, t), tan2[i]) < 0.0F) ? -1.0F : 1.0F;
            vertices[i].tangent = Vec4(orthoT.x, orthoT.y, orthoT.z, w);
        } else {
            // Fallback tangent perpendicular to normal
            Vec3 c1 = Vec3::cross(n, Vec3(0.0F, 0.0F, 1.0F));
            Vec3 c2 = Vec3::cross(n, Vec3(0.0F, 1.0F, 0.0F));
            Vec3 tDef = (c1.lengthSq() > c2.lengthSq()) ? c1.normalized() : c2.normalized();
            vertices[i].tangent = Vec4(tDef.x, tDef.y, tDef.z, 1.0F);
        }
    }
}

} // namespace eng::render
