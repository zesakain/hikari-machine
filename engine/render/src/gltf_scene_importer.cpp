/// @file gltf_scene_importer.cpp
/// @brief Implementation of hierarchical glTF 2.0 scene importer.

#include "render/gltf_scene_importer.h"

#include "core/assert.h"
#include "core/error.h"
#include "core/log.h"
#include "core/math.h"
#include "render/asset_manager.h"
#include "render/asset_types.h"
#include "render/mesh.h"
#include "render/mesh_component.h"
#include "world/components.h"
#include "world/world.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include "cgltf/cgltf.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include "gltf_internal.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>

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

[[nodiscard]] Error mapCgltfResult(cgltf_result res) noexcept {
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

/// @brief Extracts a local Transform from a cgltf_node.
///
/// If matrix is specified, decomposes using polar/Gram-Schmidt decomposition.
/// If TRS attributes are present, populates directly to preserve numeric precision.
[[nodiscard]] Transform extractLocalTransform(const cgltf_node* node) noexcept {
    if (node->has_matrix) {
        float rawMat[16];
        cgltf_node_transform_local(node, rawMat);
        Mat4 localMat{};
        std::memcpy(localMat.m, rawMat, sizeof(rawMat));
        auto decomp = localMat.decompose();
        if (decomp.has_value()) {
            return decomp.value();
        }
        return Transform::identity();
    }

    Transform t = Transform::identity();
    if (node->has_translation) {
        t.position = Vec3{node->translation[0], node->translation[1], node->translation[2]};
    }
    if (node->has_rotation) {
        t.rotation =
            Quat{node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]};
    }
    if (node->has_scale) {
        t.scale = Vec3{node->scale[0], node->scale[1], node->scale[2]};
    }
    return t;
}

[[nodiscard]] MeshData extractMeshGeometry(const cgltf_data* data, const cgltf_mesh* mesh,
                                           const std::string& meshName,
                                           std::string_view baseDir = "",
                                           std::string_view sourcePath = "") {
    MeshData meshData{};
    meshData.name = meshName;
    meshData.materials = detail::extractAllMaterials(data, baseDir, sourcePath);

    bool anyMissingNormals = false;
    bool anyMissingTangents = false;

    for (cgltf_size pIdx = 0; pIdx < mesh->primitives_count; ++pIdx) {
        const auto& prim = mesh->primitives[pIdx];
        if (prim.type != cgltf_primitive_type_triangles) {
            continue;
        }

        const cgltf_attribute* posAttr = nullptr;
        const cgltf_attribute* normAttr = nullptr;
        const cgltf_attribute* uvAttr = nullptr;
        const cgltf_attribute* colAttr = nullptr;
        const cgltf_attribute* tanAttr = nullptr;

        for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
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
            continue;
        }

        const size_t vertCount = posAttr->data->count;
        if (vertCount == 0) {
            continue;
        }

        const uint32_t vertexBase = static_cast<uint32_t>(meshData.vertices.size());
        const uint32_t firstIndex = static_cast<uint32_t>(meshData.indices.size());

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

            Vec3 localNorm{0.0F, 1.0F, 0.0F};
            if (normAttr != nullptr && normAttr->data != nullptr) {
                float n[3] = {0.0F, 1.0F, 0.0F};
                cgltf_accessor_read_float(normAttr->data, v, n, 3);
                const float len = std::sqrt((n[0] * n[0]) + (n[1] * n[1]) + (n[2] * n[2]));
                localNorm =
                    (len > 1e-6F) ? Vec3{n[0] / len, n[1] / len, n[2] / len} : Vec3::unitY();
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
                col =
                    Vec4{c[0], c[1], c[2], (colAttr->data->type == cgltf_type_vec3 ? 1.0F : c[3])};
            }

            Vec4 tangent{1.0F, 0.0F, 0.0F, 1.0F};
            if (tanAttr != nullptr && tanAttr->data != nullptr) {
                float t[4] = {1.0F, 0.0F, 0.0F, 1.0F};
                cgltf_accessor_read_float(tanAttr->data, v, t, 4);
                const float len = std::sqrt((t[0] * t[0]) + (t[1] * t[1]) + (t[2] * t[2]));
                const Vec3 localTan =
                    (len > 1e-6F) ? Vec3{t[0] / len, t[1] / len, t[2] / len} : Vec3::unitX();
                tangent = Vec4{localTan.x, localTan.y, localTan.z, t[3]};
            }

            meshData.vertices.emplace_back(localPos, localNorm, uv, col, tangent);
        }

        if (prim.indices != nullptr) {
            const size_t idxCount = prim.indices->count;
            for (size_t i = 0; i < idxCount; ++i) {
                const uint32_t idx =
                    static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, i));
                meshData.indices.push_back(vertexBase + idx);
            }
        } else {
            for (size_t i = 0; i < vertCount; ++i) {
                meshData.indices.push_back(vertexBase + static_cast<uint32_t>(i));
            }
        }

        Submesh submesh{};
        submesh.name = meshName + "_Submesh_" + std::to_string(pIdx);
        submesh.firstIndex = firstIndex;
        submesh.indexCount = static_cast<uint32_t>(meshData.indices.size() - firstIndex);
        submesh.vertexOffset = 0;
        if (prim.material != nullptr) {
            submesh.materialIndex =
                static_cast<uint32_t>(cgltf_material_index(data, prim.material));
        } else {
            submesh.materialIndex = 0;
        }
        meshData.submeshes.push_back(std::move(submesh));
    }

    if (anyMissingNormals && !meshData.indices.empty()) {
        meshData.generateNormals();
    }
    if (anyMissingTangents && !meshData.indices.empty()) {
        meshData.generateTangents();
    }
    meshData.recalculateBounds();

    return meshData;
}

/// @brief Recursively traverses the glTF node hierarchy and instantiates entities in World.
void importNodeRecursive(const cgltf_data* data, const cgltf_node* node,
                         world::EntityHandle parentEntity, world::World& world,
                         const std::string& filePath, std::size_t& nodeIndex,
                         std::vector<GltfSceneNode>& outNodes) {
    std::string nodeName = (node->name != nullptr && node->name[0] != '\0')
                               ? std::string(node->name)
                               : ("Node_" + std::to_string(nodeIndex));
    ++nodeIndex;

    world::EntityHandle entity = world.createEntity(nodeName);
    world.setComponent(entity, world::TransformComponent{extractLocalTransform(node)});

    if (parentEntity.isValid()) {
        world.setParent(entity, parentEntity, false);
    }

    const bool hasMesh = (node->mesh != nullptr);
    if (!hasMesh) {
        // Preserved as pure transform entity (empty locator / pivot)
        outNodes.push_back(GltfSceneNode{nodeName, entity, false});
    } else {
        const cgltf_mesh* mesh = node->mesh;
        const cgltf_size meshIdx = cgltf_mesh_index(data, mesh);
        const std::string meshUri = filePath + "#mesh_" + std::to_string(meshIdx);
        const AssetId meshAssetId = makeAssetIdFromPath(meshUri);

        if (mesh->primitives_count <= 1) {
            // Single primitive mesh attached directly to the node entity
            MeshComponent mc{};
            mc.meshAsset = meshAssetId;
            mc.assetPath = meshUri;
            mc.submeshIndex = 0;
            mc.visible = true;
            mc.castShadows = true;
            world.setComponent(entity, mc);

            outNodes.push_back(GltfSceneNode{nodeName, entity, true});
        } else {
            // Multi-primitive mesh: node acts as parent locator, spawning child primitive entities
            outNodes.push_back(GltfSceneNode{nodeName, entity, false});

            for (cgltf_size p = 0; p < mesh->primitives_count; ++p) {
                const std::string primName = nodeName + "_Primitive_" + std::to_string(p);
                world::EntityHandle primEntity = world.createEntity(primName);
                world.setComponent(primEntity, world::TransformComponent{Transform::identity()});
                world.setParent(primEntity, entity, false);

                MeshComponent mc{};
                mc.meshAsset = meshAssetId;
                mc.assetPath = meshUri;
                mc.submeshIndex = static_cast<uint32_t>(p);
                mc.visible = true;
                mc.castShadows = true;
                world.setComponent(primEntity, mc);

                outNodes.push_back(GltfSceneNode{primName, primEntity, true});
            }
        }
    }

    // Traverse glTF children
    for (cgltf_size c = 0; c < node->children_count; ++c) {
        importNodeRecursive(data, node->children[c], entity, world, filePath, nodeIndex, outNodes);
    }
}

} // namespace

GltfSceneImportResult GltfSceneImporter::importScene(const std::string& filePath,
                                                     world::World& world,
                                                     AssetManager* assetManager) noexcept {
    if (filePath.empty()) {
        return GltfSceneImportResult{.success = false, .errorMessage = "Empty glTF file path"};
    }

    cgltf_options options{};
    cgltf_data* parsedData = nullptr;

    const cgltf_result parseRes = cgltf_parse_file(&options, filePath.c_str(), &parsedData);
    if (parseRes != cgltf_result_success) {
        const Error err = mapCgltfResult(parseRes);
        return GltfSceneImportResult{.success = false, .errorMessage = std::string(err.message)};
    }

    CgltfGuard guard{parsedData};

    const cgltf_result loadRes = cgltf_load_buffers(&options, parsedData, filePath.c_str());
    if (loadRes != cgltf_result_success) {
        const Error err = mapCgltfResult(loadRes);
        return GltfSceneImportResult{.success = false, .errorMessage = std::string(err.message)};
    }

    const cgltf_result valRes = cgltf_validate(parsedData);
    if (valRes != cgltf_result_success) {
        return GltfSceneImportResult{
            .success = false,
            .errorMessage = "glTF validation failed: invalid accessor ranges or corrupt mesh data"};
    }

    const std::string baseDir = std::filesystem::path(filePath).parent_path().string();

    // Extract and register all meshes if an AssetManager was supplied
    for (cgltf_size m = 0; m < parsedData->meshes_count; ++m) {
        const auto* mesh = &parsedData->meshes[m];
        const std::string meshName = (mesh->name != nullptr && mesh->name[0] != '\0')
                                         ? std::string(mesh->name)
                                         : ("Mesh_" + std::to_string(m));
        const std::string meshUri = filePath + "#mesh_" + std::to_string(m);
        MeshData meshData = extractMeshGeometry(parsedData, mesh, meshName, baseDir, filePath);

        if (assetManager != nullptr) {
            (void)assetManager->registerMesh(meshUri, std::move(meshData));
        }
    }

    if (assetManager != nullptr && parsedData->images_count > 0) {
        for (cgltf_size i = 0; i < parsedData->images_count; ++i) {
            if (parsedData->images[i].buffer_view != nullptr) {
                auto srgbRes = detail::extractEmbeddedImage(parsedData, i, true);
                if (srgbRes.hasValue()) {
                    const std::string srgbUri = filePath + "#image_" + std::to_string(i) + "#srgb";
                    (void)assetManager->registerTexture(srgbUri, std::move(srgbRes.value()));
                }
                auto linearRes = detail::extractEmbeddedImage(parsedData, i, false);
                if (linearRes.hasValue()) {
                    const std::string linearUri =
                        filePath + "#image_" + std::to_string(i) + "#linear";
                    (void)assetManager->registerTexture(linearUri, std::move(linearRes.value()));
                }
            }
        }
    }

    GltfSceneImportResult result{};
    result.success = true;

    std::size_t nodeIndex = 0;
    world::EntityHandle firstRootEntity{};

    // Select active scene per Section 6.2 item 1
    const cgltf_scene* targetScene =
        parsedData->scene != nullptr
            ? parsedData->scene
            : (parsedData->scenes_count > 0 ? &parsedData->scenes[0] : nullptr);

    if (targetScene != nullptr) {
        for (cgltf_size ri = 0; ri < targetScene->nodes_count; ++ri) {
            const cgltf_node* rootNode = targetScene->nodes[ri];
            importNodeRecursive(parsedData, rootNode, world::EntityHandle{}, world, filePath,
                                nodeIndex, result.nodes);
            if (!firstRootEntity.isValid() && !result.nodes.empty()) {
                firstRootEntity = result.nodes.front().entity;
            }
        }
    } else {
        // Fallback: no scene graph defined; import all top-level nodes
        for (cgltf_size n = 0; n < parsedData->nodes_count; ++n) {
            const cgltf_node* node = &parsedData->nodes[n];
            if (node->parent == nullptr) {
                importNodeRecursive(parsedData, node, world::EntityHandle{}, world, filePath,
                                    nodeIndex, result.nodes);
                if (!firstRootEntity.isValid() && !result.nodes.empty()) {
                    firstRootEntity = result.nodes.front().entity;
                }
            }
        }
    }

    result.rootEntity = firstRootEntity;
    result.nodeCount = result.nodes.size();

    return result;
}

} // namespace eng::render
