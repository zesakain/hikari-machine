/// @file content_browser_controller.cpp
/// @brief Implementation of headless Content Browser controller and filesystem navigator.

#include "editor/content_browser_controller.h"

#include "core/log.h"
#include "render/gltf_importer.h"
#include "render/obj_importer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

namespace eng::editor {

namespace {

ContentItemType determineItemType(const std::filesystem::path& path, bool isDir) noexcept {
    if (isDir) {
        return ContentItemType::Directory;
    }
    const std::string ext = path.extension().string();
    std::string lowerExt = ext;
    std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lowerExt == ".obj" || lowerExt == ".gltf" || lowerExt == ".glb") {
        return ContentItemType::Mesh;
    }
    if (lowerExt == ".png" || lowerExt == ".jpg" || lowerExt == ".jpeg" || lowerExt == ".tga" ||
        lowerExt == ".bmp") {
        return ContentItemType::Texture;
    }
    if (lowerExt == ".wav" || lowerExt == ".ogg" || lowerExt == ".mp3" || lowerExt == ".flac") {
        return ContentItemType::Audio;
    }
    if (lowerExt == ".lua") {
        return ContentItemType::Script;
    }
    if (lowerExt == ".hikari") {
        return ContentItemType::Scene;
    }
    if (lowerExt == ".json" || lowerExt == ".toml" || lowerExt == ".txt" || lowerExt == ".ini" ||
        lowerExt == ".cfg" || lowerExt == ".mtl") {
        return ContentItemType::Data;
    }
    return ContentItemType::Other;
}

const char* getTypeBadge(ContentItemType type, const std::string& ext) noexcept {
    switch (type) {
    case ContentItemType::Directory:
        return "[DIR]";
    case ContentItemType::Mesh:
        if (ext == ".glb" || ext == ".GLB") {
            return "[GLB]";
        }
        if (ext == ".gltf" || ext == ".GLTF") {
            return "[GLTF]";
        }
        return "[OBJ]";
    case ContentItemType::Texture:
        return "[TEX]";
    case ContentItemType::Audio:
        return "[AUD]";
    case ContentItemType::Script:
        return "[LUA]";
    case ContentItemType::Scene:
        return "[SCENE]";
    case ContentItemType::Data:
        return "[DATA]";
    case ContentItemType::Other:
    default:
        return "[FILE]";
    }
}

uint32_t getBadgeColorRgba(ContentItemType type, const std::string& ext) noexcept {
    switch (type) {
    case ContentItemType::Directory:
        return 0xD9A74AFF; // Warm amber/folder gold
    case ContentItemType::Mesh:
        if (ext == ".glb" || ext == ".GLB") {
            return 0x33D973FF; // Soft cyan/emerald
        }
        if (ext == ".gltf" || ext == ".GLTF") {
            return 0xF28C33FF; // Amber
        }
        return 0x33B2F2FF; // Periwinkle/blue
    case ContentItemType::Texture:
        return 0x21C45EFF; // Bright emerald green
    case ContentItemType::Audio:
        return 0xF59E0BFF; // Amber
    case ContentItemType::Script:
        return 0xEB4D99FF; // Vibrant pink
    case ContentItemType::Scene:
        return 0x6366F1FF; // Indigo
    case ContentItemType::Data:
        return 0xB366E6FF; // Purple
    case ContentItemType::Other:
    default:
        return 0xB3B3B3FF; // Muted gray
    }
}

std::string formatFileTime(std::filesystem::file_time_type ftime) {
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
    std::tm* gmt = std::localtime(&tt);
    if (gmt == nullptr) {
        return "-";
    }
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", gmt);
    return buf;
}

} // namespace

std::string ContentBrowserController::normalizeSeparators(std::string_view path) {
    std::string s(path);
    std::replace(s.begin(), s.end(), '\\', '/');
    while (!s.empty() && s.back() == '/' && s.size() > 1) {
        s.pop_back();
    }
    return s;
}

std::string ContentBrowserController::formatBytes(uintmax_t bytes) {
    if (bytes >= 1024 * 1024) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        return buf;
    }
    if (bytes >= 1024) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f KB", static_cast<double>(bytes) / 1024.0);
        return buf;
    }
    return std::to_string(bytes) + " B";
}

bool ContentBrowserController::isValidFileName(std::string_view name) noexcept {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    for (char c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|' || static_cast<unsigned char>(c) < 32) {
            return false;
        }
    }
    return true;
}

ContentBrowserController::ContentBrowserController() {
    std::error_code ec;
    if (!std::filesystem::exists(rootDirectory_, ec)) {
        std::filesystem::create_directories(rootDirectory_, ec);
    }
    currentDirectory_ = rootDirectory_;
    pushHistory(currentDirectory_);
    refresh();
}

ContentBrowserController::ContentBrowserController(std::filesystem::path rootDirectory)
    : rootDirectory_(std::move(rootDirectory)) {
    std::error_code ec;
    if (!std::filesystem::exists(rootDirectory_, ec)) {
        std::filesystem::create_directories(rootDirectory_, ec);
    }
    currentDirectory_ = rootDirectory_;
    pushHistory(currentDirectory_);
    refresh();
}

bool ContentBrowserController::setRootDirectory(const std::filesystem::path& rootPath) {
    std::error_code ec;
    if (!std::filesystem::exists(rootPath, ec)) {
        std::filesystem::create_directories(rootPath, ec);
    }
    rootDirectory_ = rootPath;
    if (!isPathWithinRoot(currentDirectory_)) {
        currentDirectory_ = rootDirectory_;
        history_.clear();
        historyIndex_ = 0;
        pushHistory(currentDirectory_);
    }
    refresh();
    return true;
}

bool ContentBrowserController::isPathWithinRoot(const std::filesystem::path& candidatePath) const {
    std::error_code ec;
    std::filesystem::path normRoot = std::filesystem::weakly_canonical(rootDirectory_, ec);
    if (ec) {
        normRoot = rootDirectory_.lexically_normal();
    }
    std::filesystem::path normTarget = std::filesystem::weakly_canonical(candidatePath, ec);
    if (ec) {
        normTarget = candidatePath.lexically_normal();
    }

    std::string rootStr = normalizeSeparators(normRoot.string());
    std::string targetStr = normalizeSeparators(normTarget.string());

    if (targetStr == rootStr) {
        return true;
    }
    if (targetStr.size() > rootStr.size() && targetStr.rfind(rootStr + "/", 0) == 0) {
        return true;
    }
    return false;
}

void ContentBrowserController::pushHistory(const std::filesystem::path& path) {
    if (history_.empty()) {
        history_.push_back(path);
        historyIndex_ = 0;
        return;
    }
    if (history_[historyIndex_] == path) {
        return;
    }
    // Truncate any forward history after current index
    if (historyIndex_ + 1 < history_.size()) {
        history_.erase(history_.begin() + static_cast<ptrdiff_t>(historyIndex_ + 1),
                       history_.end());
    }
    history_.push_back(path);
    historyIndex_ = history_.size() - 1;
}

bool ContentBrowserController::navigateTo(const std::filesystem::path& targetPath) {
    std::error_code ec;
    if (!std::filesystem::exists(targetPath, ec) ||
        !std::filesystem::is_directory(targetPath, ec)) {
        return false;
    }
    if (!isPathWithinRoot(targetPath)) {
        return false;
    }

    currentDirectory_ = targetPath;
    pushHistory(currentDirectory_);
    refresh();
    return true;
}

bool ContentBrowserController::canNavigateBack() const noexcept {
    return historyIndex_ > 0;
}

bool ContentBrowserController::canNavigateForward() const noexcept {
    return (historyIndex_ + 1) < history_.size();
}

bool ContentBrowserController::canNavigateUp() const noexcept {
    std::error_code ec;
    std::filesystem::path canonCur = std::filesystem::weakly_canonical(currentDirectory_, ec);
    std::filesystem::path canonRoot = std::filesystem::weakly_canonical(rootDirectory_, ec);
    return normalizeSeparators(canonCur.string()) != normalizeSeparators(canonRoot.string());
}

bool ContentBrowserController::navigateBack() {
    if (!canNavigateBack()) {
        return false;
    }
    --historyIndex_;
    currentDirectory_ = history_[historyIndex_];
    refresh();
    return true;
}

bool ContentBrowserController::navigateForward() {
    if (!canNavigateForward()) {
        return false;
    }
    ++historyIndex_;
    currentDirectory_ = history_[historyIndex_];
    refresh();
    return true;
}

bool ContentBrowserController::navigateUp() {
    if (!canNavigateUp()) {
        return false;
    }
    std::filesystem::path parent = currentDirectory_.parent_path();
    if (parent.empty() || !isPathWithinRoot(parent)) {
        parent = rootDirectory_;
    }
    return navigateTo(parent);
}

std::vector<BreadcrumbSegment> ContentBrowserController::getBreadcrumbs() const {
    std::vector<BreadcrumbSegment> breadcrumbs;

    std::filesystem::path rel;
    std::error_code ec;
    std::filesystem::path canonCur = std::filesystem::weakly_canonical(currentDirectory_, ec);
    std::filesystem::path canonRoot = std::filesystem::weakly_canonical(rootDirectory_, ec);

    rel = canonCur.lexically_relative(canonRoot);

    std::string rootName = rootDirectory_.filename().string();
    if (rootName.empty()) {
        rootName = "Root";
    }
    breadcrumbs.push_back(BreadcrumbSegment{.name = rootName, .path = rootDirectory_});

    if (rel != "." && !rel.empty()) {
        std::filesystem::path accumulated = rootDirectory_;
        for (const auto& part : rel) {
            accumulated /= part;
            breadcrumbs.push_back(BreadcrumbSegment{.name = part.string(), .path = accumulated});
        }
    }
    return breadcrumbs;
}

void ContentBrowserController::refresh() {
    allDiscoveredItems_.clear();
    selectedIndex_ = -1;

    std::error_code ec;
    if (!std::filesystem::exists(currentDirectory_, ec) ||
        !std::filesystem::is_directory(currentDirectory_, ec)) {
        currentDirectory_ = rootDirectory_;
    }

    if (!std::filesystem::exists(currentDirectory_, ec)) {
        rebuildDisplayedItems();
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(
             currentDirectory_, std::filesystem::directory_options::skip_permission_denied, ec)) {
        std::error_code statEc;
        const auto& path = entry.path();
        const bool isDir = entry.is_directory(statEc);

        ContentBrowserItem item{};
        item.name = path.filename().string();
        item.extension = isDir ? "" : path.extension().string();
        item.fullPath = path;
        item.relativePath = normalizeSeparators(path.string());
        item.isDirectory = isDir;
        item.type = determineItemType(path, isDir);
        item.isMesh = (item.type == ContentItemType::Mesh);
        item.typeBadge = getTypeBadge(item.type, item.extension);
        item.badgeColorRgba = getBadgeColorRgba(item.type, item.extension);

        if (isDir) {
            item.sizeBytes = 0;
            item.sizeFormatted = "<DIR>";
        } else {
            uintmax_t size = entry.file_size(statEc);
            item.sizeBytes = statEc ? 0 : size;
            item.sizeFormatted = formatBytes(item.sizeBytes);
        }

        auto lastTime = entry.last_write_time(statEc);
        if (!statEc) {
            item.lastModifiedFormatted = formatFileTime(lastTime);
            auto dur = lastTime.time_since_epoch();
            item.lastModifiedSeconds = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(dur).count());
        } else {
            item.lastModifiedFormatted = "-";
            item.lastModifiedSeconds = 0;
        }

        allDiscoveredItems_.push_back(std::move(item));
    }

    rebuildDisplayedItems();
}

void ContentBrowserController::setSort(ContentSortField field, ContentSortDirection direction) {
    sortField_ = field;
    sortDirection_ = direction;
    rebuildDisplayedItems();
}

void ContentBrowserController::setFilterCategory(ContentFilterCategory category) {
    filterCategory_ = category;
    rebuildDisplayedItems();
}

void ContentBrowserController::setSearchFilter(std::string_view query) {
    searchFilter_ = query;
    rebuildDisplayedItems();
}

const ContentBrowserItem* ContentBrowserController::getSelectedItem() const noexcept {
    if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(displayedItems_.size())) {
        return &displayedItems_[selectedIndex_];
    }
    return nullptr;
}

void ContentBrowserController::rebuildDisplayedItems() {
    displayedItems_.clear();
    displayedItems_.reserve(allDiscoveredItems_.size());

    // 1. Filter
    std::string lowerFilter = searchFilter_;
    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (const auto& item : allDiscoveredItems_) {
        // Search filter
        if (!lowerFilter.empty()) {
            std::string lowerName = item.name;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lowerName.find(lowerFilter) == std::string::npos) {
                continue;
            }
        }

        // Category filter
        if (!item.isDirectory) {
            if (filterCategory_ == ContentFilterCategory::Meshes &&
                item.type != ContentItemType::Mesh) {
                continue;
            }
            if (filterCategory_ == ContentFilterCategory::Textures &&
                item.type != ContentItemType::Texture) {
                continue;
            }
            if (filterCategory_ == ContentFilterCategory::Audio &&
                item.type != ContentItemType::Audio) {
                continue;
            }
            if (filterCategory_ == ContentFilterCategory::Scripts &&
                item.type != ContentItemType::Script) {
                continue;
            }
            if (filterCategory_ == ContentFilterCategory::Scenes &&
                item.type != ContentItemType::Scene) {
                continue;
            }
        }

        displayedItems_.push_back(item);
    }

    // 2. Sort: Directories first, then by field
    auto comp = [this](const ContentBrowserItem& a, const ContentBrowserItem& b) {
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory > b.isDirectory; // Directories always on top
        }

        bool result = false;
        switch (sortField_) {
        case ContentSortField::Name: {
            std::string la = a.name;
            std::string lb = b.name;
            std::transform(la.begin(), la.end(), la.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::transform(lb.begin(), lb.end(), lb.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            result = (la < lb);
            break;
        }
        case ContentSortField::Type:
            result = (static_cast<uint8_t>(a.type) < static_cast<uint8_t>(b.type));
            if (a.type == b.type) {
                result = (a.name < b.name);
            }
            break;
        case ContentSortField::Size:
            result = (a.sizeBytes < b.sizeBytes);
            if (a.sizeBytes == b.sizeBytes) {
                result = (a.name < b.name);
            }
            break;
        case ContentSortField::Date:
            result = (a.lastModifiedSeconds < b.lastModifiedSeconds);
            if (a.lastModifiedSeconds == b.lastModifiedSeconds) {
                result = (a.name < b.name);
            }
            break;
        }

        return (sortDirection_ == ContentSortDirection::Ascending) ? result : !result;
    };

    std::sort(displayedItems_.begin(), displayedItems_.end(), comp);
}

Result<std::filesystem::path> ContentBrowserController::createFolder(std::string_view folderName) {
    if (!isValidFileName(folderName)) {
        return Result<std::filesystem::path>::failure(
            Error(ErrorCode::InvalidArgument, "Invalid folder name"));
    }

    std::filesystem::path newPath = currentDirectory_ / folderName;
    if (!isPathWithinRoot(newPath)) {
        return Result<std::filesystem::path>::failure(
            Error(ErrorCode::InvalidArgument, "Folder path escapes project asset root"));
    }

    std::error_code ec;
    if (std::filesystem::exists(newPath, ec)) {
        return Result<std::filesystem::path>::failure(
            Error(ErrorCode::InvalidArgument, "A file or folder with that name already exists"));
    }

    if (!std::filesystem::create_directory(newPath, ec) || ec) {
        return Result<std::filesystem::path>::failure(
            Error(ErrorCode::InvalidArgument, "Failed to create folder on filesystem"));
    }

    refresh();

    // Select the new folder
    for (size_t i = 0; i < displayedItems_.size(); ++i) {
        if (displayedItems_[i].name == folderName) {
            selectedIndex_ = static_cast<int>(i);
            break;
        }
    }

    return Result<std::filesystem::path>::success(newPath);
}

Result<std::filesystem::path>
ContentBrowserController::createFile(std::string_view fileName, ContentFileTemplate templateType) {
    std::string finalName(fileName);
    if (finalName.empty()) {
        switch (templateType) {
        case ContentFileTemplate::EmptyFile:
            finalName = "NewFile";
            break;
        case ContentFileTemplate::TextFile:
            finalName = "NewTextDocument.txt";
            break;
        case ContentFileTemplate::LuaScript:
            finalName = "NewScript.lua";
            break;
        case ContentFileTemplate::HikariScene:
            finalName = "NewScene.hikari";
            break;
        }
    } else {
        // Enforce appropriate extension
        switch (templateType) {
        case ContentFileTemplate::TextFile:
            if (finalName.rfind(".txt") == std::string::npos) {
                finalName += ".txt";
            }
            break;
        case ContentFileTemplate::LuaScript:
            if (finalName.rfind(".lua") == std::string::npos) {
                finalName += ".lua";
            }
            break;
        case ContentFileTemplate::HikariScene:
            if (finalName.rfind(".hikari") == std::string::npos) {
                finalName += ".hikari";
            }
            break;
        case ContentFileTemplate::EmptyFile:
            break;
        }
    }

    if (!isValidFileName(finalName)) {
        return Result<std::filesystem::path>::failure(
            Error(ErrorCode::InvalidArgument, "Invalid file name"));
    }

    std::filesystem::path newPath = currentDirectory_ / finalName;
    if (!isPathWithinRoot(newPath)) {
        return Result<std::filesystem::path>::failure(
            Error(ErrorCode::InvalidArgument, "File path escapes project asset root"));
    }

    std::error_code ec;
    if (std::filesystem::exists(newPath, ec)) {
        return Result<std::filesystem::path>::failure(
            Error(ErrorCode::InvalidArgument, "A file with that name already exists"));
    }

    std::ofstream out(newPath, std::ios::out | std::ios::binary);
    if (!out.is_open()) {
        return Result<std::filesystem::path>::failure(
            Error(ErrorCode::InvalidArgument, "Failed to open file for writing"));
    }

    switch (templateType) {
    case ContentFileTemplate::EmptyFile:
    case ContentFileTemplate::TextFile:
        break;
    case ContentFileTemplate::LuaScript: {
        out << "-- " << finalName << "\n";
        out << "-- Hikari Lua Script Component\n\n";
        out << "function on_init()\n";
        out << "end\n\n";
        out << "function on_update(dt)\n";
        out << "end\n";
        break;
    }
    case ContentFileTemplate::HikariScene: {
        // Valid minimal scene schema conforming to SceneSerializer requirements
        std::string sceneStem = std::filesystem::path(finalName).stem().string();
        out << "{\n";
        out << "  \"formatVersion\": 1,\n";
        out << "  \"metadata\": {\n";
        out << "    \"name\": \"" << sceneStem << "\",\n";
        out << "    \"generator\": \"Hikari Engine\",\n";
        out << "    \"timestamp\": 0\n";
        out << "  },\n";
        out << "  \"entities\": []\n";
        out << "}\n";
        break;
    }
    }
    out.close();

    refresh();

    // Select new file
    for (size_t i = 0; i < displayedItems_.size(); ++i) {
        if (displayedItems_[i].name == finalName) {
            selectedIndex_ = static_cast<int>(i);
            break;
        }
    }

    return Result<std::filesystem::path>::success(newPath);
}

Result<std::filesystem::path>
ContentBrowserController::renameItem(const std::filesystem::path& targetPath,
                                     std::string_view newName) {
    if (!isValidFileName(newName)) {
        return Result<std::filesystem::path>::failure(
            Error(ErrorCode::InvalidArgument, "Invalid name supplied for rename"));
    }

    if (!isPathWithinRoot(targetPath)) {
        return Result<std::filesystem::path>::failure(
            Error(ErrorCode::InvalidArgument, "Target path is outside project root"));
    }

    std::error_code ec;
    if (!std::filesystem::exists(targetPath, ec)) {
        return Result<std::filesystem::path>::failure(
            Error(ErrorCode::InvalidArgument, "Target item to rename does not exist"));
    }

    std::filesystem::path newPath = targetPath.parent_path() / newName;
    if (!isPathWithinRoot(newPath)) {
        return Result<std::filesystem::path>::failure(
            Error(ErrorCode::InvalidArgument, "Renamed path escapes project root"));
    }

    if (std::filesystem::exists(newPath, ec)) {
        return Result<std::filesystem::path>::failure(
            Error(ErrorCode::InvalidArgument, "An item with the destination name already exists"));
    }

    std::filesystem::rename(targetPath, newPath, ec);
    if (ec) {
        return Result<std::filesystem::path>::failure(
            Error(ErrorCode::InvalidArgument, "Filesystem failed to rename item"));
    }

    refresh();

    for (size_t i = 0; i < displayedItems_.size(); ++i) {
        if (displayedItems_[i].name == newName) {
            selectedIndex_ = static_cast<int>(i);
            break;
        }
    }

    return Result<std::filesystem::path>::success(newPath);
}

bool ContentBrowserController::isDirectoryEmpty(const std::filesystem::path& dirPath) const {
    std::error_code ec;
    if (!std::filesystem::is_directory(dirPath, ec)) {
        return true;
    }
    std::filesystem::directory_iterator it(dirPath, ec);
    return it == std::filesystem::directory_iterator();
}

Result<void> ContentBrowserController::deleteItem(const std::filesystem::path& targetPath,
                                                  bool recursive) {
    if (!isPathWithinRoot(targetPath)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "Target path is outside project root"));
    }

    std::error_code ec;
    std::filesystem::path canonTarget = std::filesystem::weakly_canonical(targetPath, ec);
    std::filesystem::path canonRoot = std::filesystem::weakly_canonical(rootDirectory_, ec);
    if (normalizeSeparators(canonTarget.string()) == normalizeSeparators(canonRoot.string())) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "Cannot delete the project asset root"));
    }

    if (!std::filesystem::exists(targetPath, ec)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "Item to delete does not exist"));
    }

    if (std::filesystem::is_directory(targetPath, ec)) {
        if (!isDirectoryEmpty(targetPath) && !recursive) {
            return Result<void>::failure(Error(
                ErrorCode::InvalidArgument,
                "Directory is not empty; explicit confirmation for recursive delete is required"));
        }
        std::filesystem::remove_all(targetPath, ec);
    } else {
        std::filesystem::remove(targetPath, ec);
    }

    if (ec) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "Filesystem delete operation failed"));
    }

    refresh();
    return Result<void>::success();
}

Result<std::filesystem::path>
ContentBrowserController::duplicateFile(const std::filesystem::path& targetPath) {
    if (!isPathWithinRoot(targetPath)) {
        return Result<std::filesystem::path>::failure(
            Error(ErrorCode::InvalidArgument, "Target path is outside project root"));
    }

    std::error_code ec;
    if (!std::filesystem::exists(targetPath, ec) || std::filesystem::is_directory(targetPath, ec)) {
        return Result<std::filesystem::path>::failure(
            Error(ErrorCode::InvalidArgument, "Can only duplicate existing files"));
    }

    std::string stem = targetPath.stem().string();
    std::string ext = targetPath.extension().string();
    std::filesystem::path parent = targetPath.parent_path();

    std::filesystem::path candidate = parent / (stem + "_Copy" + ext);
    int suffix = 1;
    while (std::filesystem::exists(candidate, ec)) {
        candidate = parent / (stem + "_Copy_" + std::to_string(suffix++) + ext);
    }

    std::filesystem::copy_file(targetPath, candidate, std::filesystem::copy_options::none, ec);
    if (ec) {
        return Result<std::filesystem::path>::failure(
            Error(ErrorCode::InvalidArgument, "Filesystem copy failed during duplication"));
    }

    refresh();

    for (size_t i = 0; i < displayedItems_.size(); ++i) {
        if (displayedItems_[i].fullPath == candidate) {
            selectedIndex_ = static_cast<int>(i);
            break;
        }
    }

    return Result<std::filesystem::path>::success(candidate);
}

bool ContentBrowserController::isSupportedImportExtension(std::string_view extension) noexcept {
    if (extension.empty()) {
        return false;
    }
    std::string ext(extension);
    if (ext.front() != '.') {
        ext = "." + ext;
    }
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // 3D Meshes
    if (ext == ".obj" || ext == ".gltf" || ext == ".glb") {
        return true;
    }
    // Textures
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") {
        return true;
    }
    // Audio
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac") {
        return true;
    }
    // Scripts
    if (ext == ".lua") {
        return true;
    }
    // Scenes
    if (ext == ".hikari") {
        return true;
    }
    // Materials & Data
    if (ext == ".mtl" || ext == ".json" || ext == ".toml" || ext == ".txt" || ext == ".ini" ||
        ext == ".cfg") {
        return true;
    }
    return false;
}

Result<std::filesystem::path>
ContentBrowserController::importFile(const std::filesystem::path& sourcePath) {
    std::error_code ec;
    if (!std::filesystem::exists(sourcePath, ec)) {
        return Result<std::filesystem::path>::failure(Error(
            ErrorCode::InvalidArgument, "Source file does not exist: " + sourcePath.string()));
    }

    if (std::filesystem::is_directory(sourcePath, ec)) {
        return Result<std::filesystem::path>::failure(
            Error(ErrorCode::InvalidArgument,
                  "Cannot import a directory directly: " + sourcePath.string()));
    }

    const std::string ext = sourcePath.extension().string();
    if (!isSupportedImportExtension(ext)) {
        return Result<std::filesystem::path>::failure(
            Error(ErrorCode::InvalidArgument,
                  "Unsupported asset file format: " + (ext.empty() ? "(none)" : ext)));
    }

    // Convert extension to lowercase for parser validation
    std::string lowerExt = ext;
    std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Validate 3D mesh integrity via existing importers before copying
    if (lowerExt == ".obj") {
        auto parseRes = render::ObjImporter::importFromFile(sourcePath.string());
        if (!parseRes.hasValue()) {
            return Result<std::filesystem::path>::failure(Error(
                ErrorCode::InvalidArgument, std::string("Failed to parse Wavefront OBJ file: ") +
                                                std::string(parseRes.error().message)));
        }
    } else if (lowerExt == ".gltf" || lowerExt == ".glb") {
        auto parseRes = render::GltfImporter::importFromFile(sourcePath.string());
        if (!parseRes.hasValue()) {
            return Result<std::filesystem::path>::failure(
                Error(ErrorCode::InvalidArgument, std::string("Failed to parse glTF/GLB file: ") +
                                                      std::string(parseRes.error().message)));
        }
    }

    const std::string fileName = sourcePath.filename().string();
    if (!isValidFileName(fileName)) {
        return Result<std::filesystem::path>::failure(Error(
            ErrorCode::InvalidArgument, "File name contains invalid characters: " + fileName));
    }

    const std::filesystem::path destPath = currentDirectory_ / fileName;
    if (!isPathWithinRoot(destPath)) {
        return Result<std::filesystem::path>::failure(
            Error(ErrorCode::InvalidArgument, "Destination path escapes project asset root"));
    }

    // Check if source and destination are the exact same file
    if (std::filesystem::equivalent(sourcePath, destPath, ec)) {
        return Result<std::filesystem::path>::failure(
            Error(ErrorCode::InvalidArgument,
                  "Source file is already located in the destination folder"));
    }

    // Copy file into target asset directory
    std::filesystem::copy_file(sourcePath, destPath,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return Result<std::filesystem::path>::failure(Error(
            ErrorCode::InvalidArgument, "Filesystem copy failed during import: " + ec.message()));
    }

    // Refresh content browser items and select the newly imported asset
    refresh();

    for (size_t i = 0; i < displayedItems_.size(); ++i) {
        if (displayedItems_[i].fullPath == destPath) {
            selectedIndex_ = static_cast<int>(i);
            break;
        }
    }

    return Result<std::filesystem::path>::success(destPath);
}

Result<std::vector<std::filesystem::path>>
ContentBrowserController::importFiles(const std::vector<std::filesystem::path>& sourcePaths) {
    if (sourcePaths.empty()) {
        return Result<std::vector<std::filesystem::path>>::failure(
            Error(ErrorCode::InvalidArgument, "No files specified for import"));
    }

    std::vector<std::filesystem::path> importedPaths;
    importedPaths.reserve(sourcePaths.size());

    for (const auto& src : sourcePaths) {
        auto res = importFile(src);
        if (!res.hasValue()) {
            return Result<std::vector<std::filesystem::path>>::failure(res.error());
        }
        importedPaths.push_back(res.value());
    }

    return Result<std::vector<std::filesystem::path>>::success(std::move(importedPaths));
}

} // namespace eng::editor
