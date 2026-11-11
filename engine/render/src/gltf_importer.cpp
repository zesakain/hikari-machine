/// @file gltf_importer.cpp
/// @brief Implementation of glTF 2.0 (.gltf, .glb) 3D asset importer using cgltf.

#include "render/gltf_importer.h"

#include "core/log.h"
#include "core/math.h"
#include "render/material.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include "gltf_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace eng::render {

namespace {

struct CgltfGuard {
    cgltf_data* data{nullptr};
    ~CgltfGuard() noexcept {
        if (data != nullptr) {
            cgltf_free(data);
        }
    }
};

struct Mat3 {
    float m[9]{1, 0, 0, 0, 1, 0, 0, 0, 1};

    [[nodiscard]] float determinant() const noexcept {
        return (m[0] * ((m[4] * m[8]) - (m[5] * m[7]))) - (m[3] * ((m[1] * m[8]) - (m[2] * m[7]))) +
               (m[6] * ((m[1] * m[5]) - (m[2] * m[4])));
    }

    [[nodiscard]] Mat3 inverseTranspose() const noexcept {
        const float det = determinant();
        if (std::abs(det) < 1e-12F) {
            return *this;
        }
        const float invDet = 1.0F / det;
        Mat3 res;
        // In column-major order:
        res.m[0] = ((m[4] * m[8]) - (m[5] * m[7])) * invDet;
        res.m[1] = -((m[3] * m[8]) - (m[5] * m[6])) * invDet;
        res.m[2] = ((m[3] * m[7]) - (m[4] * m[6])) * invDet;

        res.m[3] = -((m[1] * m[8]) - (m[2] * m[7])) * invDet;
        res.m[4] = ((m[0] * m[8]) - (m[2] * m[6])) * invDet;
        res.m[5] = -((m[0] * m[7]) - (m[1] * m[6])) * invDet;

        res.m[6] = ((m[1] * m[5]) - (m[2] * m[4])) * invDet;
        res.m[7] = -((m[0] * m[5]) - (m[2] * m[3])) * invDet;
        res.m[8] = ((m[0] * m[4]) - (m[1] * m[3])) * invDet;
        return res;
    }

    [[nodiscard]] Vec3 transformVector(const Vec3& v) const noexcept {
        return Vec3{(m[0] * v.x) + (m[3] * v.y) + (m[6] * v.z),
                    (m[1] * v.x) + (m[4] * v.y) + (m[7] * v.z),
                    (m[2] * v.x) + (m[5] * v.y) + (m[8] * v.z)};
    }
};

[[nodiscard]] Mat3 extractMat3(const Mat4& mat) noexcept {
    Mat3 res;
    // Upper-left 3x3 of column-major Mat4
    res.m[0] = mat.at(0, 0);
    res.m[1] = mat.at(1, 0);
    res.m[2] = mat.at(2, 0);

    res.m[3] = mat.at(0, 1);
    res.m[4] = mat.at(1, 1);
    res.m[5] = mat.at(2, 1);

    res.m[6] = mat.at(0, 2);
    res.m[7] = mat.at(1, 2);
    res.m[8] = mat.at(2, 2);
    return res;
}

Error mapCgltfResult(cgltf_result res) {
    switch (res) {
    case cgltf_result_file_not_found:
        return Error(ErrorCode::InvalidArgument, "glTF file or external buffer not found");
    case cgltf_result_io_error:
        return Error(ErrorCode::InvalidArgument, "I/O error reading glTF or buffer data");
    case cgltf_result_invalid_json:
        return Error(ErrorCode::InvalidArgument, "Invalid glTF JSON format");
    case cgltf_result_invalid_gltf:
        return Error(ErrorCode::InvalidArgument, "Corrupt or invalid glTF structure");
    case cgltf_result_legacy_gltf:
        return Error(ErrorCode::InvalidArgument,
                     "Unsupported glTF version (only glTF 2.0 supported)");
    case cgltf_result_data_too_short:
        return Error(ErrorCode::InvalidArgument, "Invalid GLB header or truncated data buffer");
    case cgltf_result_out_of_memory:
        return Error(ErrorCode::OutOfMemory, "Out of memory parsing glTF data");
    case cgltf_result_unknown_format:
    default:
        return Error(ErrorCode::InvalidArgument,
                     "Unrecognized or unsupported glTF/GLB file format");
    }
}

Result<MeshData> processGltfData(cgltf_data* data, std::string_view baseDir, std::string_view name,
                                 std::string_view sourcePath = "") {
    if (data == nullptr) {
        return Error(ErrorCode::InvalidArgument, "Null glTF data pointer");
    }

    MeshData meshData{std::string(name)};

    // 1. Extract Materials
    meshData.materials = detail::extractAllMaterials(data, baseDir, sourcePath);

    // 2. Discover Mesh Nodes and Transforms
    struct NodeMeshPair {
        const cgltf_mesh* mesh{nullptr};
        Mat4 worldTransform{Mat4::identity()};
    };
    std::vector<NodeMeshPair> meshInstances;

    if (data->nodes_count > 0) {
        for (size_t n = 0; n < data->nodes_count; ++n) {
            const auto* node = &data->nodes[n];
            if (node->mesh != nullptr) {
                float rawMat[16];
                cgltf_node_transform_world(node, rawMat);
                Mat4 worldMat{};
                std::memcpy(worldMat.m, rawMat, sizeof(rawMat));
                meshInstances.push_back(
                    NodeMeshPair{.mesh = node->mesh, .worldTransform = worldMat});
            }
        }
    }

    // Fallback: If no node references any mesh, use all meshes with identity transform
    if (meshInstances.empty()) {
        for (size_t m = 0; m < data->meshes_count; ++m) {
            meshInstances.push_back(
                NodeMeshPair{.mesh = &data->meshes[m], .worldTransform = Mat4::identity()});
        }
    }

    if (meshInstances.empty()) {
        return Error(ErrorCode::InvalidArgument, "glTF asset contains no meshes or geometry");
    }

    // 3. Process Primitives and Geometry
    bool anyMissingNormals = false;
    bool anyMissingTangents = false;

    for (const auto& inst : meshInstances) {
        const auto* mesh = inst.mesh;
        const Mat4& worldMat = inst.worldTransform;
        const Mat3 m33 = extractMat3(worldMat);
        const float det = m33.determinant();
        const Mat3 normMat = m33.inverseTranspose();
        const bool isMirrored = (det < 0.0F);

        for (size_t pIdx = 0; pIdx < mesh->primitives_count; ++pIdx) {
            const auto& prim = mesh->primitives[pIdx];

            if (prim.type != cgltf_primitive_type_triangles) {
                return Error(ErrorCode::InvalidArgument,
                             "Unsupported primitive mode: only triangles (mode 4) are supported");
            }

            const cgltf_attribute* posAttr = nullptr;
            const cgltf_attribute* normAttr = nullptr;
            const cgltf_attribute* uvAttr = nullptr;
            const cgltf_attribute* colAttr = nullptr;
            const cgltf_attribute* tanAttr = nullptr;

            for (size_t a = 0; a < prim.attributes_count; ++a) {
                const auto& attr = prim.attributes[a];
                if (attr.type == cgltf_attribute_type_position) {
                    posAttr = &attr;
                } else if (attr.type == cgltf_attribute_type_normal) {
                    normAttr = &attr;
                } else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) {
                    uvAttr = &attr;
                } else if (attr.type == cgltf_attribute_type_color && attr.index == 0) {
                    colAttr = &attr;
                } else if (attr.type == cgltf_attribute_type_tangent && attr.index == 0) {
                    tanAttr = &attr;
                }
            }

            if (posAttr == nullptr || posAttr->data == nullptr) {
                return Error(ErrorCode::InvalidArgument,
                             "glTF primitive missing required POSITION attribute");
            }

            const size_t vertCount = posAttr->data->count;
            if (vertCount == 0) {
                continue;
            }

            const uint32_t vertexBase = static_cast<uint32_t>(meshData.vertices.size());
            meshData.vertices.reserve(meshData.vertices.size() + vertCount);

            if (normAttr == nullptr) {
                anyMissingNormals = true;
            }
            if (tanAttr == nullptr) {
                anyMissingTangents = true;
            }

            for (size_t v = 0; v < vertCount; ++v) {
                float p[3] = {0.0F, 0.0F, 0.0F};
                cgltf_accessor_read_float(posAttr->data, v, p, 3);
                const Vec3 localPos{p[0], p[1], p[2]};
                const Vec3 worldPos = worldMat.transformPoint(localPos);

                Vec3 worldNorm{0.0F, 1.0F, 0.0F};
                if (normAttr != nullptr && normAttr->data != nullptr) {
                    float n[3] = {0.0F, 1.0F, 0.0F};
                    cgltf_accessor_read_float(normAttr->data, v, n, 3);
                    const Vec3 localNorm{n[0], n[1], n[2]};
                    const Vec3 transformedNorm = normMat.transformVector(localNorm);
                    const float len = transformedNorm.length();
                    worldNorm = (len > 1e-6F) ? (transformedNorm / len) : Vec3::unitY();
                }

                Vec2 uv{0.0F, 0.0F};
                if (uvAttr != nullptr && uvAttr->data != nullptr) {
                    float t[2] = {0.0F, 0.0F};
                    cgltf_accessor_read_float(uvAttr->data, v, t, 2);
                    uv = Vec2{t[0], t[1]};
                }

                Vec4 col{1.0F, 1.0F, 1.0F, 1.0F};
                if (colAttr != nullptr && colAttr->data != nullptr) {
                    float c[4] = {1.0F, 1.0F, 1.0F, 1.0F};
                    cgltf_accessor_read_float(colAttr->data, v, c, 4);
                    col = Vec4{c[0], c[1], c[2],
                               (colAttr->data->type == cgltf_type_vec3 ? 1.0F : c[3])};
                }

                Vec4 tangent{1.0F, 0.0F, 0.0F, 1.0F};
                if (tanAttr != nullptr && tanAttr->data != nullptr) {
                    float t[4] = {1.0F, 0.0F, 0.0F, 1.0F};
                    cgltf_accessor_read_float(tanAttr->data, v, t, 4);
                    const Vec3 localTan{t[0], t[1], t[2]};
                    const Vec3 transformedTan = worldMat.transformVector(localTan);
                    const float len = transformedTan.length();
                    const Vec3 worldTan = (len > 1e-6F) ? (transformedTan / len) : Vec3::unitX();
                    tangent = Vec4{worldTan.x, worldTan.y, worldTan.z, t[3]};
                }

                meshData.vertices.emplace_back(worldPos, worldNorm, uv, col, tangent);
            }

            Submesh submesh{};
            submesh.name = (prim.material != nullptr && prim.material->name != nullptr)
                               ? prim.material->name
                               : ((mesh->name != nullptr) ? mesh->name : "Submesh");
            submesh.firstIndex = static_cast<uint32_t>(meshData.indices.size());
            submesh.vertexOffset = vertexBase;

            if (prim.material != nullptr) {
                const auto diff = prim.material - data->materials;
                if (diff >= 0 && diff < static_cast<ptrdiff_t>(data->materials_count)) {
                    submesh.materialIndex = static_cast<uint32_t>(diff);
                }
            }

            if (prim.indices != nullptr) {
                const size_t idxCount = prim.indices->count;
                if ((idxCount % 3) != 0) {
                    return Error(ErrorCode::InvalidArgument,
                                 "glTF primitive index count is not a multiple of 3");
                }

                meshData.indices.reserve(meshData.indices.size() + idxCount);

                for (size_t t = 0; t < idxCount / 3; ++t) {
                    const uint32_t i0 =
                        vertexBase +
                        static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, (t * 3) + 0));
                    const uint32_t i1 =
                        vertexBase +
                        static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, (t * 3) + 1));
                    const uint32_t i2 =
                        vertexBase +
                        static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, (t * 3) + 2));

                    if (i0 >= meshData.vertices.size() || i1 >= meshData.vertices.size() ||
                        i2 >= meshData.vertices.size()) {
                        return Error(ErrorCode::OutOfBounds,
                                     "glTF index references non-existent vertex");
                    }

                    if (isMirrored) {
                        meshData.indices.push_back(i0);
                        meshData.indices.push_back(i2);
                        meshData.indices.push_back(i1);
                    } else {
                        meshData.indices.push_back(i0);
                        meshData.indices.push_back(i1);
                        meshData.indices.push_back(i2);
                    }
                }
            } else {
                // Non-indexed primitive
                if ((vertCount % 3) != 0) {
                    return Error(ErrorCode::InvalidArgument,
                                 "Non-indexed glTF primitive vertex count is not a multiple of 3");
                }

                meshData.indices.reserve(meshData.indices.size() + vertCount);

                for (size_t t = 0; t < vertCount / 3; ++t) {
                    const uint32_t i0 = vertexBase + static_cast<uint32_t>((t * 3) + 0);
                    const uint32_t i1 = vertexBase + static_cast<uint32_t>((t * 3) + 1);
                    const uint32_t i2 = vertexBase + static_cast<uint32_t>((t * 3) + 2);

                    if (isMirrored) {
                        meshData.indices.push_back(i0);
                        meshData.indices.push_back(i2);
                        meshData.indices.push_back(i1);
                    } else {
                        meshData.indices.push_back(i0);
                        meshData.indices.push_back(i1);
                        meshData.indices.push_back(i2);
                    }
                }
            }

            submesh.indexCount =
                static_cast<uint32_t>(meshData.indices.size() - submesh.firstIndex);
            meshData.submeshes.push_back(std::move(submesh));
        }
    }

    if (anyMissingNormals) {
        meshData.generateNormals();
    }
    if (anyMissingTangents) {
        meshData.generateTangents();
    }

    meshData.recalculateBounds();

    auto valRes = meshData.validate();
    if (!valRes.hasValue()) {
        return valRes.error();
    }

    return Result<MeshData>::success(std::move(meshData));
}

} // namespace

Result<MeshData> GltfImporter::importFromFile(std::string_view filePath) {
    if (filePath.empty()) {
        return Error(ErrorCode::InvalidArgument, "Empty glTF file path");
    }

    cgltf_options options{};
    cgltf_data* parsedData = nullptr;
    const std::string pathStr(filePath);

    const cgltf_result parseRes = cgltf_parse_file(&options, pathStr.c_str(), &parsedData);
    if (parseRes != cgltf_result_success) {
        return mapCgltfResult(parseRes);
    }

    CgltfGuard guard{parsedData};

    const cgltf_result loadRes = cgltf_load_buffers(&options, parsedData, pathStr.c_str());
    if (loadRes != cgltf_result_success) {
        return mapCgltfResult(loadRes);
    }

    const cgltf_result valRes = cgltf_validate(parsedData);
    if (valRes != cgltf_result_success) {
        return Error(ErrorCode::InvalidArgument,
                     "glTF validation failed: invalid accessor ranges or corrupt mesh data");
    }

    const std::filesystem::path p(filePath);
    const std::string baseDir = p.parent_path().string();
    const std::string stem = p.stem().string();

    return processGltfData(parsedData, baseDir, stem, filePath);
}

Result<MeshData> GltfImporter::importFromMemory(const void* data, size_t size,
                                                std::string_view baseDir, std::string_view name) {
    if (data == nullptr || size == 0) {
        return Error(ErrorCode::InvalidArgument, "Empty or null glTF memory buffer");
    }

    cgltf_options options{};
    cgltf_data* parsedData = nullptr;

    const cgltf_result parseRes = cgltf_parse(&options, data, size, &parsedData);
    if (parseRes != cgltf_result_success) {
        return mapCgltfResult(parseRes);
    }

    CgltfGuard guard{parsedData};

    const std::string baseDirStr(baseDir);
    const char* pathForBuffers = baseDir.empty() ? nullptr : baseDirStr.c_str();
    const cgltf_result loadRes = cgltf_load_buffers(&options, parsedData, pathForBuffers);
    if (loadRes != cgltf_result_success) {
        return mapCgltfResult(loadRes);
    }

    const cgltf_result valRes = cgltf_validate(parsedData);
    if (valRes != cgltf_result_success) {
        return Error(ErrorCode::InvalidArgument,
                     "glTF validation failed: invalid accessor ranges or corrupt mesh data");
    }

    return processGltfData(parsedData, baseDir, name);
}

} // namespace eng::render
