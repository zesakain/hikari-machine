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

Result<MeshData> processGltfData(cgltf_data* data, std::string_view /*baseDir*/,
                                 std::string_view name) {
    if (data == nullptr) {
        return Error(ErrorCode::InvalidArgument, "Null glTF data pointer");
    }

    MeshData meshData{std::string(name)};
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

    return processGltfData(parsedData, baseDir, stem);
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
