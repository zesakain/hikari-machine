/// @file gltf_importer.h
/// @brief glTF 2.0 and GLB 3D asset importer for Hikari-Machine.
///
/// Purpose:
/// Imports individual 3D assets from glTF 2.0 (.gltf JSON + external buffers/URIs) and
/// binary glTF (.glb) files into eng::render::MeshData. Supports vertex positions, normals,
/// texture coordinates (UVs), vertex colors, triangle indices, multiple primitives,
/// multiple submeshes, PBR metallic-roughness materials, texture references, and node transform
/// hierarchies with negative scale and winding handling.
///
/// Ownership and Lifetime:
/// Pure parser functions returning a newly allocated, validated MeshData wrapped in eng::Result.
///
/// Thread Safety:
/// Completely thread-safe and re-entrant.

#pragma once

#include "core/error.h"
#include "core/result.h"
#include "render/mesh.h"

#include <string_view>

namespace eng::render {

/// @class GltfImporter
/// @brief Parser and importer for glTF 2.0 (.gltf, .glb) 3D assets.
class GltfImporter {
public:
    /// @brief Loads and parses a glTF or GLB file from disk.
    ///
    /// @param filePath Filesystem path to the .gltf or .glb file.
    /// @return Populated and validated MeshData on success, or Error on failure.
    [[nodiscard]] static Result<MeshData> importFromFile(std::string_view filePath);

    /// @brief Parses a glTF/GLB asset from an in-memory byte buffer.
    ///
    /// @param data Pointer to file bytes in memory.
    /// @param size Size of data buffer in bytes.
    /// @param baseDir Optional directory path used to resolve external buffer or image URIs.
    /// @param name Diagnostic name assigned to the resulting MeshData.
    /// @return Populated and validated MeshData on success, or Error on failure.
    [[nodiscard]] static Result<MeshData> importFromMemory(const void* data, size_t size,
                                                           std::string_view baseDir = "",
                                                           std::string_view name = "GltfMesh");
};

} // namespace eng::render
