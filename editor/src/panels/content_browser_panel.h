/// @file content_browser_panel.h
/// @brief Content browser panel for inspecting project assets, data files, and scripts.

#pragma once

#include "editor/content_browser_controller.h"
#include "editor/editor_panel.h"

#include <array>
#include <mutex>
#include <string>
#include <vector>

namespace eng::editor {

class ContentBrowserPanel final : public EditorPanel {
public:
    ContentBrowserPanel() = default;
    explicit ContentBrowserPanel(std::filesystem::path rootDir);
    ~ContentBrowserPanel() override = default;

    [[nodiscard]] std::string_view getId() const noexcept override { return "content_browser"; }

    [[nodiscard]] std::string_view getDisplayName() const noexcept override {
        return "Content Browser";
    }

    [[nodiscard]] std::string_view getCategory() const noexcept override { return "Project"; }

    void draw(EditorContext& ctx) override;

    [[nodiscard]] ContentBrowserController& getController() noexcept { return controller_; }
    [[nodiscard]] const ContentBrowserController& getController() const noexcept {
        return controller_;
    }

    /// Opens the native platform file dialog to import assets into the current directory.
    void openNativeImportDialog(EditorContext& ctx);

    /// Queues external file paths to be imported safely on the main thread.
    void queuePendingImports(std::vector<std::filesystem::path> paths);

    /// Records an error message from native dialog callbacks.
    void recordNativeDialogError(std::string_view errorMsg);

private:
    void drawToolbar(EditorContext& ctx);
    void drawBreadcrumbs();
    void drawDirectoryTree();
    void drawAssetItems(EditorContext& ctx);
    void drawListView(EditorContext& ctx);
    void drawGridView(EditorContext& ctx);
    void drawFooter(EditorContext& ctx);
    void drawModals();
    void processPendingImports(EditorContext& ctx);

    ContentBrowserController controller_{"assets"};

    // Creation modals state
    bool showCreateFolderModal_{false};
    std::array<char, 128> folderNameBuffer_{};

    bool showCreateFileModal_{false};
    std::array<char, 128> fileNameBuffer_{};
    ContentFileTemplate pendingFileTemplate_{ContentFileTemplate::TextFile};

    bool showRenameModal_{false};
    std::array<char, 128> renameBuffer_{};
    std::filesystem::path targetRenamePath_{};

    bool showDeleteModal_{false};
    std::filesystem::path targetDeletePath_{};
    std::string targetDeleteItemName_{};
    std::string targetDeleteItemType_{};
    bool isDeleteNonEmptyDir_{false};

    bool showErrorModal_{false};
    std::string errorMessage_{};

    // Import state
    mutable std::mutex pendingImportMutex_{};
    std::vector<std::filesystem::path> pendingImportPaths_{};
    std::string nativeDialogError_{};
    bool showImportPathModal_{false};
    std::array<char, 512> importPathBuffer_{};
};

} // namespace eng::editor
