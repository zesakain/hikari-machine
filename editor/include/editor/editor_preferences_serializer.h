/// @file editor_preferences_serializer.h
/// @brief Transactional JSON serializer for cross-project EditorPreferences.
///
/// Purpose:
/// Persists and restores user-level editor preferences to and from the operating system's
/// standard user configuration directory. Supports atomic file writes, schema versioning,
/// and fallback defaults upon file corruption.
///
/// Ownership and Lifetime:
/// Stateless utility class with static methods only.
///
/// Thread Safety:
/// Safe to call concurrently on distinct file paths.
///
/// Performance:
/// Fast JSON DOM serialization; atomic file writes via temporary file and rename.
/// Non-blocking, cold path execution.
///
/// Errors:
/// Returns Result<T> indicating success or Error with appropriate ErrorCode.

#pragma once

#include "core/result.h"
#include "editor/editor_preferences.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace eng::editor {

/// @class EditorPreferencesSerializer
/// @brief Serialization service for user preferences JSON persistence.
class EditorPreferencesSerializer {
public:
    static constexpr uint32_t kCurrentVersion = 1;

    /// Returns the OS-standard path for editor preferences JSON.
    ///
    /// Linux:   $XDG_CONFIG_HOME/hikari/editor_preferences.json (fallback: ~/.config/hikari/...)
    /// Windows: %APPDATA%/Hikari/editor_preferences.json
    /// macOS:   ~/Library/Application Support/Hikari/editor_preferences.json
    [[nodiscard]] static std::filesystem::path getDefaultPreferencesPath();

    /// Serializes EditorPreferences into formatted JSON text.
    [[nodiscard]] static Result<std::string> serializeToString(const EditorPreferences& prefs,
                                                               bool prettyPrint = true,
                                                               int indentSpaces = 2);

    /// Serializes EditorPreferences to disk atomically (writes to .tmp and renames).
    [[nodiscard]] static Result<void> serializeToFile(const EditorPreferences& prefs,
                                                      const std::filesystem::path& path,
                                                      bool prettyPrint = true,
                                                      int indentSpaces = 2);

    /// Deserializes EditorPreferences from a JSON string.
    /// Missing fields remain set to their canonical defaults.
    [[nodiscard]] static Result<EditorPreferences> deserializeFromString(std::string_view jsonText);

    /// Loads EditorPreferences from disk.
    /// If the file does not exist, returns default preferences.
    /// If corrupted, logs an error and returns default preferences without crashing.
    [[nodiscard]] static Result<EditorPreferences>
    deserializeFromFile(const std::filesystem::path& path);
};

} // namespace eng::editor
