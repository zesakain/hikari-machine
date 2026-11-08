/// @file obj_importer.cpp
/// @brief Self-contained Wavefront OBJ importer implementation.

#include "render/obj_importer.h"

#include "core/assert.h"
#include "core/log.h"

#include <cctype>
#include <charconv>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace eng::render {

namespace {

struct VertexKey {
    int32_t posIdx{-1};
    int32_t uvIdx{-1};
    int32_t normIdx{-1};

    bool operator==(const VertexKey& other) const noexcept {
        return posIdx == other.posIdx && uvIdx == other.uvIdx && normIdx == other.normIdx;
    }
};

struct VertexKeyHash {
    size_t operator()(const VertexKey& k) const noexcept {
        size_t h = static_cast<size_t>(k.posIdx);
        h = h * 31 + static_cast<size_t>(k.uvIdx);
        h = h * 31 + static_cast<size_t>(k.normIdx);
        return h;
    }
};

void skipWhitespace(std::string_view& sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r')) {
        sv.remove_prefix(1);
    }
}

std::string_view readToken(std::string_view& sv) {
    skipWhitespace(sv);
    const size_t start = 0;
    size_t len = 0;
    while (len < sv.size() && sv[len] != ' ' && sv[len] != '\t' && sv[len] != '\r' &&
           sv[len] != '\n') {
        ++len;
    }
    std::string_view token = sv.substr(start, len);
    sv.remove_prefix(len);
    return token;
}

bool parseFloat(std::string_view sv, float& outVal) {
    // Standard strtof handles scientific notation, signs, etc. reliably across platforms.
    char buffer[64];
    if (sv.empty() || sv.size() >= sizeof(buffer)) {
        return false;
    }
    for (size_t i = 0; i < sv.size(); ++i) {
        buffer[i] = sv[i];
    }
    buffer[sv.size()] = '\0';
    char* endPtr = nullptr;
    outVal = std::strtof(buffer, &endPtr);
    return endPtr != buffer;
}

int32_t resolveIndex(int32_t idx, size_t count) {
    if (idx > 0) {
        return idx - 1;
    }
    if (idx < 0) {
        return static_cast<int32_t>(count) + idx;
    }
    return -1;
}

bool parseFaceCorner(std::string_view token, size_t posCount, size_t uvCount, size_t normCount,
                     VertexKey& outKey) {
    outKey = VertexKey{};
    int32_t p = 0;
    int32_t t = 0;
    int32_t n = 0;

    // Pattern: v, v/vt, v//vn, v/vt/vn
    const size_t slash1 = token.find('/');
    if (slash1 == std::string_view::npos) {
        // Just v
        if (std::from_chars(token.data(), token.data() + token.size(), p).ec != std::errc()) {
            return false;
        }
        outKey.posIdx = resolveIndex(p, posCount);
        return outKey.posIdx >= 0 && static_cast<size_t>(outKey.posIdx) < posCount;
    }

    // Has at least one slash
    std::string_view pStr = token.substr(0, slash1);
    if (std::from_chars(pStr.data(), pStr.data() + pStr.size(), p).ec != std::errc()) {
        return false;
    }
    outKey.posIdx = resolveIndex(p, posCount);
    if (outKey.posIdx < 0 || static_cast<size_t>(outKey.posIdx) >= posCount) {
        return false;
    }

    const size_t slash2 = token.find('/', slash1 + 1);
    if (slash2 == std::string_view::npos) {
        // v/vt
        std::string_view tStr = token.substr(slash1 + 1);
        if (!tStr.empty()) {
            if (std::from_chars(tStr.data(), tStr.data() + tStr.size(), t).ec != std::errc()) {
                return false;
            }
            outKey.uvIdx = resolveIndex(t, uvCount);
        }
    } else {
        // v/vt/vn or v//vn
        std::string_view tStr = token.substr(slash1 + 1, slash2 - (slash1 + 1));
        if (!tStr.empty()) {
            if (std::from_chars(tStr.data(), tStr.data() + tStr.size(), t).ec != std::errc()) {
                return false;
            }
            outKey.uvIdx = resolveIndex(t, uvCount);
        }

        std::string_view nStr = token.substr(slash2 + 1);
        if (!nStr.empty()) {
            if (std::from_chars(nStr.data(), nStr.data() + nStr.size(), n).ec != std::errc()) {
                return false;
            }
            outKey.normIdx = resolveIndex(n, normCount);
        }
    }

    return true;
}

} // namespace

Result<MeshData> ObjImporter::importFromString(std::string_view objText, std::string_view name) {
    MeshData mesh{};
    mesh.name = std::string(name);

    std::vector<Vec3> rawPositions;
    std::vector<Vec2> rawUvs;
    std::vector<Vec3> rawNormals;

    // Reserve typical capacity to minimize reallocations
    rawPositions.reserve(1024);
    rawUvs.reserve(1024);
    rawNormals.reserve(1024);
    mesh.vertices.reserve(1024);
    mesh.indices.reserve(3072);

    std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vertexLookup;
    vertexLookup.reserve(2048);

    Submesh currentSubmesh{};
    currentSubmesh.name = "default";
    currentSubmesh.firstIndex = 0;
    currentSubmesh.vertexOffset = 0;

    size_t lineStart = 0;
    while (lineStart < objText.size()) {
        size_t lineEnd = objText.find('\n', lineStart);
        if (lineEnd == std::string_view::npos) {
            lineEnd = objText.size();
        }

        std::string_view line = objText.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;

        skipWhitespace(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }

        std::string_view prefix = readToken(line);
        if (prefix == "v") {
            Vec3 pos{};
            float val = 0.0F;
            if (parseFloat(readToken(line), val)) {
                pos.x = val;
            }
            if (parseFloat(readToken(line), val)) {
                pos.y = val;
            }
            if (parseFloat(readToken(line), val)) {
                pos.z = val;
            }
            rawPositions.push_back(pos);
        } else if (prefix == "vt") {
            Vec2 uv{};
            float val = 0.0F;
            if (parseFloat(readToken(line), val)) {
                uv.x = val;
            }
            if (parseFloat(readToken(line), val)) {
                uv.y = val;
            }
            rawUvs.push_back(uv);
        } else if (prefix == "vn") {
            Vec3 norm{};
            float val = 0.0F;
            if (parseFloat(readToken(line), val)) {
                norm.x = val;
            }
            if (parseFloat(readToken(line), val)) {
                norm.y = val;
            }
            if (parseFloat(readToken(line), val)) {
                norm.z = val;
            }
            rawNormals.push_back(norm);
        } else if (prefix == "o" || prefix == "g") {
            std::string_view groupName = readToken(line);
            if (!mesh.indices.empty() && mesh.indices.size() > currentSubmesh.firstIndex) {
                currentSubmesh.indexCount =
                    static_cast<uint32_t>(mesh.indices.size() - currentSubmesh.firstIndex);
                mesh.submeshes.push_back(currentSubmesh);
                currentSubmesh.name = groupName.empty() ? "submesh" : std::string(groupName);
                currentSubmesh.firstIndex = static_cast<uint32_t>(mesh.indices.size());
            } else if (!groupName.empty()) {
                currentSubmesh.name = std::string(groupName);
            }
        } else if (prefix == "f") {
            std::vector<uint32_t> faceIndices;
            while (true) {
                std::string_view cornerToken = readToken(line);
                if (cornerToken.empty()) {
                    break;
                }

                VertexKey key{};
                if (!parseFaceCorner(cornerToken, rawPositions.size(), rawUvs.size(),
                                     rawNormals.size(), key)) {
                    continue;
                }

                auto it = vertexLookup.find(key);
                uint32_t finalIndex = 0;
                if (it != vertexLookup.end()) {
                    finalIndex = it->second;
                } else {
                    Vertex v{};
                    if (key.posIdx >= 0 && static_cast<size_t>(key.posIdx) < rawPositions.size()) {
                        v.position = rawPositions[static_cast<size_t>(key.posIdx)];
                    }
                    if (key.uvIdx >= 0 && static_cast<size_t>(key.uvIdx) < rawUvs.size()) {
                        v.uv = rawUvs[static_cast<size_t>(key.uvIdx)];
                    }
                    if (key.normIdx >= 0 && static_cast<size_t>(key.normIdx) < rawNormals.size()) {
                        v.normal = rawNormals[static_cast<size_t>(key.normIdx)];
                    }
                    v.color = {1.0F, 1.0F, 1.0F, 1.0F};

                    finalIndex = static_cast<uint32_t>(mesh.vertices.size());
                    mesh.vertices.push_back(v);
                    vertexLookup.emplace(key, finalIndex);
                }
                faceIndices.push_back(finalIndex);
            }

            // Fan triangulation for arbitrary n-gons (n >= 3)
            if (faceIndices.size() >= 3) {
                for (size_t i = 1; i + 1 < faceIndices.size(); ++i) {
                    mesh.indices.push_back(faceIndices[0]);
                    mesh.indices.push_back(faceIndices[i]);
                    mesh.indices.push_back(faceIndices[i + 1]);
                }
            }
        }
    }

    if (mesh.vertices.empty()) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "OBJ data contains no valid vertex geometry"};
    }

    // Finalize submesh
    if (mesh.indices.size() > currentSubmesh.firstIndex) {
        currentSubmesh.indexCount =
            static_cast<uint32_t>(mesh.indices.size() - currentSubmesh.firstIndex);
        mesh.submeshes.push_back(currentSubmesh);
    } else if (mesh.submeshes.empty()) {
        currentSubmesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
        mesh.submeshes.push_back(currentSubmesh);
    }

    // If normals were completely missing, generate them automatically
    if (rawNormals.empty()) {
        mesh.generateNormals();
    }

    // Compute bounding boxes for the mesh and submeshes
    mesh.recalculateBounds();

    return Result<MeshData>::success(std::move(mesh));
}

Result<MeshData> ObjImporter::importFromFile(std::string_view filePath) {
    std::string pathStr(filePath);
    std::ifstream file(pathStr, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        // Fallback for binaries/tests executing within build/<preset>/ directory
        const std::string candidates[] = {
            "../../../" + pathStr,
            "../../" + pathStr,
            "../" + pathStr,
        };
        for (const auto& candidate : candidates) {
            file.open(candidate, std::ios::in | std::ios::binary);
            if (file.is_open()) {
                pathStr = candidate;
                break;
            }
        }
        if (!file.is_open()) {
            ENG_LOG_ERROR("ObjImporter", "Failed to open OBJ file");
            return Error{.code = ErrorCode::InvalidArgument, .message = "Failed to open OBJ file"};
        }
    }

    file.seekg(0, std::ios::end);
    const auto fileSize = file.tellg();
    if (fileSize <= 0) {
        return Error{.code = ErrorCode::InvalidArgument, .message = "OBJ file is empty"};
    }
    file.seekg(0, std::ios::beg);

    std::string content;
    content.resize(static_cast<size_t>(fileSize));
    file.read(content.data(), fileSize);

    // Extract filename as default mesh name
    std::string meshName = pathStr;
    const size_t slash = meshName.find_last_of("/\\");
    if (slash != std::string::npos) {
        meshName = meshName.substr(slash + 1);
    }

    return importFromString(content, meshName);
}

} // namespace eng::render
