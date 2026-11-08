/// @file content_browser_panel.h
/// @brief Content browser panel for inspecting project assets, data files, and scripts.

#pragma once

#include "editor/content_browser_controller.h"
#include "editor/editor_panel.h"

#include <array>
#include <string>

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

private:
    void drawToolbar(EditorContext& ctx);
    void drawBreadcrumbs();
    void drawDirectoryTree();
    void drawAssetItems(EditorContext& ctx);
    void drawFooter(EditorContext& ctx);
    void drawModals();

    ContentBrowserController controller_{"assets"};

    // Creation modals state
    bool showCreateFolderModal_{false};
    std::array<char, 128> folderNameBuffer_{};

    bool showCreateFileModal_{false};
    std::array<char, 128> fileNameBuffer_{};
    ContentFileTemplate pendingFileTemplate_{ContentFileTemplate::TextFile};

    bool showErrorModal_{false};
    std::string errorMessage_{};
};

} // namespace eng::editor
