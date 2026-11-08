/// @file content_browser_controller.h
/// @brief Headless controller governing Content Browser filesystem operations and navigation.
///
/// Purpose:
/// Decouples directory navigation, history stacks, breadcrumb generation, filesystem enumeration,
/// path containment, and asset management operations from Dear ImGui rendering.
/// Enables comprehensive headless unit testing without requiring an active GUI runtime.
///
/// Ownership and Lifetime:
/// Owned by ContentBrowserPanel or EditorEngine. Operates on std::filesystem paths.
///
/// Thread-safety:
/// Designed for main thread execution.
///
/// Error Handling:
/// Uses eng::Result and eng::Error with -fno-exceptions and -fno-rtti compatibility.

#pragma once

#include "core/error.h"
#include "core/result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace eng::editor {

/// @enum ContentItemType
/// @brief Category of item discovered in the project filesystem.
enum class ContentItemType : uint8_t {
    Directory,
    Mesh,
    Texture,
    Audio,
    Script,
    Scene,
    Data,
    Other
};

/// @enum ContentSortField
/// @brief Field used to sort directory entries.
enum class ContentSortField : uint8_t {
    Name,
    Type,
    Size,
    Date
};

/// @enum ContentSortDirection
/// @brief Ascending or descending sort order.
enum class ContentSortDirection : uint8_t {
    Ascending,
    Descending
};

/// @enum ContentFilterCategory
/// @brief Filter category for displayed assets.
enum class ContentFilterCategory : uint8_t {
    All,
    Meshes,
    Textures,
    Audio,
    Scripts,
    Scenes
};

/// @enum ContentViewMode
/// @brief Visual presentation mode in the Content Browser.
enum class ContentViewMode : uint8_t {
    List,
    Grid
};

/// @enum ContentFileTemplate
/// @brief Supported template types for new asset creation.
enum class ContentFileTemplate : uint8_t {
    EmptyFile,
    TextFile,
    LuaScript,
    HikariScene
};

/// @struct BreadcrumbSegment
/// @brief Represents one clickable navigation segment along the active path.
struct BreadcrumbSegment {
    std::string name;
    std::filesystem::path path;
};

/// @struct ContentBrowserItem
/// @brief Detailed information for a single file or directory item.
struct ContentBrowserItem {
    std::string name;
    std::string extension;
    std::filesystem::path fullPath;
    std::string relativePath;
    uintmax_t sizeBytes{0};
    std::string sizeFormatted;
    std::string lastModifiedFormatted;
    uint64_t lastModifiedSeconds{0};
    ContentItemType type{ContentItemType::Other};
    const char* typeBadge{"[ASSET]"};
    uint32_t badgeColorRgba{0xB3B3B3FF};
    bool isDirectory{false};
    bool isMesh{false};
};

/// @class ContentBrowserController
/// @brief Headless manager for filesystem navigation, path security, and asset operations.
class ContentBrowserController {
public:
    ContentBrowserController();
    explicit ContentBrowserController(std::filesystem::path rootDirectory);
    ~ContentBrowserController() = default;

    ContentBrowserController(const ContentBrowserController&) = default;
    ContentBrowserController& operator=(const ContentBrowserController&) = default;
    ContentBrowserController(ContentBrowserController&&) noexcept = default;
    ContentBrowserController& operator=(ContentBrowserController&&) noexcept = default;

    /// Configures the base project asset root. Clamps current directory if outside.
    bool setRootDirectory(const std::filesystem::path& rootPath);

    /// Retrieves the current project asset root path.
    [[nodiscard]] const std::filesystem::path& getRootDirectory() const noexcept {
        return rootDirectory_;
    }

    /// Retrieves the current browsing directory.
    [[nodiscard]] const std::filesystem::path& getCurrentDirectory() const noexcept {
        return currentDirectory_;
    }

    /// Navigates to a target directory, verifying it exists and is contained within root.
    bool navigateTo(const std::filesystem::path& targetPath);

    /// Navigates up to the parent directory if currently within root.
    bool navigateUp();

    /// Navigates backward in history.
    bool navigateBack();

    /// Navigates forward in history.
    bool navigateForward();

    /// Checks if backward navigation is possible.
    [[nodiscard]] bool canNavigateBack() const noexcept;

    /// Checks if forward navigation is possible.
    [[nodiscard]] bool canNavigateForward() const noexcept;

    /// Checks if navigating up to parent directory is permitted.
    [[nodiscard]] bool canNavigateUp() const noexcept;

    /// Refreshes directory contents from disk.
    void refresh();

    /// Verifies whether candidatePath resides strictly within rootDirectory_ and prevents traversal.
    [[nodiscard]] bool isPathWithinRoot(const std::filesystem::path& candidatePath) const;

    /// Returns breadcrumb segments leading from project root down to currentDirectory_.
    [[nodiscard]] std::vector<BreadcrumbSegment> getBreadcrumbs() const;

    /// Retrieves the filtered and sorted list of items in the current directory.
    [[nodiscard]] const std::vector<ContentBrowserItem>& getItems() const noexcept {
        return displayedItems_;
    }

    /// Sets view mode (Grid or List).
    void setViewMode(ContentViewMode mode) noexcept { viewMode_ = mode; }
    [[nodiscard]] ContentViewMode getViewMode() const noexcept { return viewMode_; }

    /// Sets sorting parameters.
    void setSort(ContentSortField field, ContentSortDirection direction);
    [[nodiscard]] ContentSortField getSortField() const noexcept { return sortField_; }
    [[nodiscard]] ContentSortDirection getSortDirection() const noexcept { return sortDirection_; }

    /// Sets asset category filter.
    void setFilterCategory(ContentFilterCategory category);
    [[nodiscard]] ContentFilterCategory getFilterCategory() const noexcept {
        return filterCategory_;
    }

    /// Sets search query substring filter.
    void setSearchFilter(std::string_view query);
    [[nodiscard]] std::string_view getSearchFilter() const noexcept { return searchFilter_; }

    /// Selection indexing.
    void setSelectedItemIndex(int index) noexcept { selectedIndex_ = index; }
    [[nodiscard]] int getSelectedItemIndex() const noexcept { return selectedIndex_; }
    [[nodiscard]] const ContentBrowserItem* getSelectedItem() const noexcept;

    /// Creates a new subfolder in the current directory.
    Result<std::filesystem::path> createFolder(std::string_view folderName);

    /// Creates a new file in the current directory using the specified template.
    Result<std::filesystem::path> createFile(std::string_view fileName,
                                             ContentFileTemplate templateType);

    /// Renames an existing file or directory.
    Result<std::filesystem::path> renameItem(const std::filesystem::path& targetPath,
                                             std::string_view newName);

    /// Deletes a file or directory. Non-empty directories require recursive=true.
    Result<void> deleteItem(const std::filesystem::path& targetPath, bool recursive = false);

    /// Duplicates an existing file with an auto-generated unique suffix.
    Result<std::filesystem::path> duplicateFile(const std::filesystem::path& targetPath);

    /// Checks if a directory contains any files or subdirectories.
    [[nodiscard]] bool isDirectoryEmpty(const std::filesystem::path& dirPath) const;

    /// Validates whether a file/folder name contains only legal filesystem characters.
    [[nodiscard]] static bool isValidFileName(std::string_view name) noexcept;

    /// Normalizes path separators to forward slashes.
    [[nodiscard]] static std::string normalizeSeparators(std::string_view path);

    /// Formats file size in B, KB, or MB.
    [[nodiscard]] static std::string formatBytes(uintmax_t bytes);

private:
    void rebuildDisplayedItems();
    void pushHistory(const std::filesystem::path& path);

    std::filesystem::path rootDirectory_{"assets"};
    std::filesystem::path currentDirectory_{"assets"};

    std::vector<std::filesystem::path> history_{};
    size_t historyIndex_{0};

    std::vector<ContentBrowserItem> allDiscoveredItems_{};
    std::vector<ContentBrowserItem> displayedItems_{};

    ContentViewMode viewMode_{ContentViewMode::List};
    ContentSortField sortField_{ContentSortField::Name};
    ContentSortDirection sortDirection_{ContentSortDirection::Ascending};
    ContentFilterCategory filterCategory_{ContentFilterCategory::All};
    std::string searchFilter_{};
    int selectedIndex_{-1};
};

} // namespace eng::editor
