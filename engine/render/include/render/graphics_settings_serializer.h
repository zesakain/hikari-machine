/// @file graphics_settings_serializer.h
/// @brief High-reliability transactional JSON serializer for GraphicsSettings.
///
/// Purpose:
/// Provides serialization, deserialization, versioning, and validation for native
/// graphics configuration files. Ensures corrupted, missing, or future-version settings
/// preserve verified safe defaults without altering active engine state.
///
/// Ownership and Lifetime:
/// Stateless utility class with static methods only.
///
/// Thread Safety:
/// Safe to call concurrently on separate files and memory buffers.
///
/// Performance:
/// Fast JSON DOM serialization with zero heap allocation in hot path. File operations
/// execute atomically via temporary-file-and-rename semantics.

#pragma once

#include "core/result.h"
#include "render/graphics_settings.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace eng::render {

/// @class GraphicsSettingsSerializer
/// @brief Primary engine service for saving and loading graphics configuration files.
class GraphicsSettingsSerializer {
public:
    static constexpr uint32_t kCurrentVersion = 1;

    /// Serializes GraphicsSettings into a formatted JSON string.
    [[nodiscard]] static Result<std::string> serializeToString(const GraphicsSettings& settings,
                                                               bool prettyPrint = true,
                                                               int indentSpaces = 2);

    /// Serializes GraphicsSettings into a file atomically (writes to .tmp and renames).
    [[nodiscard]] static Result<void> serializeToFile(const GraphicsSettings& settings,
                                                      const std::filesystem::path& path,
                                                      bool prettyPrint = true,
                                                      int indentSpaces = 2);

    /// Deserializes a JSON text string into a GraphicsSettings struct.
    /// Preserves defaults for missing fields and clamps invalid ranges safely.
    [[nodiscard]] static Result<GraphicsSettings> deserializeFromString(std::string_view jsonText);

    /// Loads and deserializes a GraphicsSettings configuration from disk.
    [[nodiscard]] static Result<GraphicsSettings>
    deserializeFromFile(const std::filesystem::path& path);
};

} // namespace eng::render
