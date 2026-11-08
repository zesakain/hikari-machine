/// @file obj_importer.h
/// @brief Clean, self-contained Wavefront OBJ importer for 3D meshes.
///
/// Purpose:
/// Loads Wavefront .obj text format geometry into eng::render::MeshData without
/// external library dependencies.
///
/// Ownership and Lifetime:
/// Returns a freshly allocated MeshData object wrapped in eng::Result.
///
/// Thread-safety:
/// Pure function. Completely thread-safe.
///
/// Performance:
/// Single-pass parser with pre-allocated buffers. Avoids dynamic allocation in hot loops.

#pragma once

#include "core/error.h"
#include "core/result.h"
#include "render/mesh.h"

#include <string_view>

namespace eng::render {

/// @class ObjImporter
/// @brief Parser for Wavefront .obj files.
class ObjImporter {
public:
    /// @brief Loads and parses an OBJ file from disk.
    ///
    /// @param filePath Filesystem path to the .obj file.
    /// @return Populated and validated MeshData on success, or Error on failure.
    [[nodiscard]] static Result<MeshData> importFromFile(std::string_view filePath);

    /// @brief Parses an OBJ file from an in-memory string.
    ///
    /// @param objText In-memory string containing OBJ format text.
    /// @param name Diagnostic name assigned to the resulting MeshData.
    /// @return Populated and validated MeshData on success, or Error on failure.
    [[nodiscard]] static Result<MeshData> importFromString(std::string_view objText,
                                                           std::string_view name = "ObjMesh");
};

} // namespace eng::render
