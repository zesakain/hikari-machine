/// @file scene_serializer.h
/// @brief High-reliability transactional serializer and deserializer for native .hikari scene
/// files.
///
/// Purpose:
/// Implements transactional serialization between Hikari's Shared World and .hikari native scene
/// files. Guarantees that a corrupted, missing, or invalid scene file will NEVER alter or corrupt
/// the active simulation World.
///
/// Ownership and Lifetime:
/// SceneSerializer is a stateless static utility. Does not own World or entities.
///
/// Thread Safety:
/// Safe to call concurrently on distinct World instances. Deserializing into a shared World must
/// occur on the main simulation thread.
///
/// Performance:
/// Fast JSON DOM serialization and validation. File writes use atomic temporary-file-and-rename
/// semantics to prevent corrupted or truncated scene saves.

#pragma once

#include "core/math.h"
#include "core/result.h"
#include "framework/scene.h"
#include "render/post_process_types.h"
#include "world/world.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eng::framework {

/// @struct SerializationOptions
/// @brief Options controlling JSON formatting and indentation.
struct SerializationOptions {
    bool prettyPrint{true};
    int indentSpaces{2};
};

/// @struct DeserializationResult
/// @brief Diagnostic outcomes resulting from scene deserialization.
struct DeserializationResult {
    size_t entitiesLoaded{0};
    std::vector<std::string> warnings;
    SceneMetadata metadata;
    std::optional<render::PostProcessSettings> postProcessSettings;
    std::optional<Vec3> cameraPosition;
    std::optional<Vec3> cameraTarget;
};

/// @class SceneSerializer
/// @brief Primary engine service for saving and loading .hikari native scene files.
class SceneSerializer {
public:
    static constexpr uint32_t kSupportedFormatVersion = 1;

    /// Serializes active entities and persistent components from World into a .hikari JSON string.
    ///
    /// @param world Source World containing active simulation entities.
    /// @param metadata Header metadata to embed in the scene file.
    /// @param options Formatting options.
    /// @param postProcess Optional scene post-processing settings.
    /// @param cameraPosition Optional default camera world position.
    /// @param cameraTarget Optional default camera look-at target.
    /// @return Result with serialized JSON string, or Error on failure.
    [[nodiscard]] static Result<std::string>
    serializeToString(world::World& world, const SceneMetadata& metadata = {},
                      const SerializationOptions& options = {},
                      const std::optional<render::PostProcessSettings>& postProcess = std::nullopt,
                      const std::optional<Vec3>& cameraPosition = std::nullopt,
                      const std::optional<Vec3>& cameraTarget = std::nullopt);

    /// Serializes active entities and persistent components from World into a .hikari file on disk.
    /// Uses an atomic temporary-file-and-rename pattern to ensure existing files are never
    /// corrupted.
    ///
    /// @param world Source World containing active simulation entities.
    /// @param path Target filesystem path for the .hikari file.
    /// @param metadata Header metadata to embed in the scene file.
    /// @param options Formatting options.
    /// @param postProcess Optional scene post-processing settings.
    /// @param cameraPosition Optional default camera world position.
    /// @param cameraTarget Optional default camera look-at target.
    /// @return Result<void> on success, or Error on file write failure.
    [[nodiscard]] static Result<void>
    serializeToFile(world::World& world, const std::filesystem::path& path,
                    const SceneMetadata& metadata = {}, const SerializationOptions& options = {},
                    const std::optional<render::PostProcessSettings>& postProcess = std::nullopt,
                    const std::optional<Vec3>& cameraPosition = std::nullopt,
                    const std::optional<Vec3>& cameraTarget = std::nullopt);

    /// Deserializes a .hikari JSON text buffer into the target World.
    /// Transactional: if validation fails, the target World is completely untouched.
    ///
    /// @param world Target simulation World.
    /// @param jsonText JSON string containing scene data.
    /// @param clearWorldFirst Whether to clear existing user entities prior to spawning scene
    /// entities.
    /// @return Result with DeserializationResult, or Error on validation failure.
    [[nodiscard]] static Result<DeserializationResult>
    deserializeFromString(world::World& world, std::string_view jsonText,
                          bool clearWorldFirst = true);

    /// Deserializes a .hikari file from disk into the target World.
    /// Transactional: if the file is missing, corrupted, or invalid, the target World is completely
    /// untouched.
    ///
    /// @param world Target simulation World.
    /// @param path Filesystem path to the .hikari file.
    /// @param clearWorldFirst Whether to clear existing user entities prior to spawning scene
    /// entities.
    /// @return Result with DeserializationResult, or Error on failure.
    [[nodiscard]] static Result<DeserializationResult>
    deserializeFromFile(world::World& world, const std::filesystem::path& path,
                        bool clearWorldFirst = true);
};

} // namespace eng::framework
