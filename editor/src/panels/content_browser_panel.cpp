/// @file content_browser_panel.cpp
/// @brief Content browser panel implementation with directory navigation, asset management, and
/// drag-and-drop.

#include "panels/content_browser_panel.h"

#include "core/log.h"
#include "editor/editor_theme.h"
#include "editor/external_editor.h"
#include "editor/hierarchy_panel.h"
#include "editor/selection_model.h"
#include "physics/physics_components.h"
#include "render/asset_manager.h"
#include "render/mesh_component.h"
#include "world/components.h"
#include "world/world.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <imgui.h>

namespace eng::editor {

namespace {

ImVec4 rgbaToImVec4(uint32_t rgba) noexcept {
    const float r = static_cast<float>((rgba >> 24) & 0xFF) / 255.0F;
    const float g = static_cast<float>((rgba >> 16) & 0xFF) / 255.0F;
    const float b = static_cast<float>((rgba >> 8) & 0xFF) / 255.0F;
    const float a = static_cast<float>(rgba & 0xFF) / 255.0F;
    return ImVec4(r, g, b, a);
}

} // namespace

ContentBrowserPanel::ContentBrowserPanel(std::filesystem::path rootDir)
    : controller_(std::move(rootDir)) {}

void ContentBrowserPanel::draw(EditorContext& ctx) {
    processPendingImports(ctx);

    if (!isOpen_) {
        return;
    }

    if (ImGui::Begin(getDisplayName().data(), &isOpen_)) {
        drawToolbar(ctx);
        drawBreadcrumbs();

        ImGui::Separator();

        const float footerHeight = 72.0F;
        (void)footerHeight;

        ImGui::Columns(2, "ContentBrowserSplit", true);
        ImGui::SetColumnWidth(0, 180.0F);

        // Left Pane: Directory Tree
        drawDirectoryTree();

        ImGui::NextColumn();

        // Right Pane: Asset Items
        drawAssetItems(ctx);

        ImGui::Columns(1);
        ImGui::Separator();

        // Bottom Footer: Inspection and Actions
        drawFooter(ctx);

        // Modals
        drawModals();
    }
    ImGui::End();
}

void ContentBrowserPanel::drawToolbar(EditorContext& ctx) {
    (void)ctx;
    // 1. Navigation Buttons: Back, Forward, Up, Refresh
    const bool canBack = controller_.canNavigateBack();
    if (!canBack) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("<##NavBack", ImVec2(24.0F, 0.0F))) {
        controller_.navigateBack();
    }
    if (!canBack) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    const bool canFwd = controller_.canNavigateForward();
    if (!canFwd) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(">##NavFwd", ImVec2(24.0F, 0.0F))) {
        controller_.navigateForward();
    }
    if (!canFwd) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    const bool canUp = controller_.canNavigateUp();
    if (!canUp) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("^##NavUp", ImVec2(24.0F, 0.0F))) {
        controller_.navigateUp();
    }
    if (!canUp) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("R##Refresh", ImVec2(24.0F, 0.0F))) {
        controller_.refresh();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // 2. Creation Buttons
    if (ImGui::Button("+ Folder")) {
        folderNameBuffer_.fill('\0');
        std::snprintf(folderNameBuffer_.data(), folderNameBuffer_.size(), "NewFolder");
        showCreateFolderModal_ = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("+ File...")) {
        ImGui::OpenPopup("CreateFileMenuPopup");
    }

    if (ImGui::BeginPopup("CreateFileMenuPopup")) {
        if (ImGui::MenuItem("Empty File")) {
            fileNameBuffer_.fill('\0');
            std::snprintf(fileNameBuffer_.data(), fileNameBuffer_.size(), "NewFile");
            pendingFileTemplate_ = ContentFileTemplate::EmptyFile;
            showCreateFileModal_ = true;
        }
        if (ImGui::MenuItem("Text Document (.txt)")) {
            fileNameBuffer_.fill('\0');
            std::snprintf(fileNameBuffer_.data(), fileNameBuffer_.size(), "NewDocument.txt");
            pendingFileTemplate_ = ContentFileTemplate::TextFile;
            showCreateFileModal_ = true;
        }
        if (ImGui::MenuItem("Lua Script (.lua)")) {
            fileNameBuffer_.fill('\0');
            std::snprintf(fileNameBuffer_.data(), fileNameBuffer_.size(), "NewScript.lua");
            pendingFileTemplate_ = ContentFileTemplate::LuaScript;
            showCreateFileModal_ = true;
        }
        if (ImGui::MenuItem("Hikari Scene (.hikari)")) {
            fileNameBuffer_.fill('\0');
            std::snprintf(fileNameBuffer_.data(), fileNameBuffer_.size(), "NewScene.hikari");
            pendingFileTemplate_ = ContentFileTemplate::HikariScene;
            showCreateFileModal_ = true;
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Import")) {
        openNativeImportDialog(ctx);
    }

    ImGui::SameLine();
    const auto* selectedItem = controller_.getSelectedItem();
    const bool canDelete = (selectedItem != nullptr);
    if (!canDelete) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Delete")) {
        if (selectedItem != nullptr) {
            targetDeletePath_ = selectedItem->fullPath;
            targetDeleteItemName_ = selectedItem->name;
            targetDeleteItemType_ =
                selectedItem->isDirectory
                    ? "Folder"
                    : (selectedItem->extension.empty() ? "File"
                                                       : (selectedItem->extension + " File"));
            isDeleteNonEmptyDir_ = (selectedItem->isDirectory &&
                                    !controller_.isDirectoryEmpty(selectedItem->fullPath));
            showDeleteModal_ = true;
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (!canDelete) {
            ImGui::SetTooltip("Delete: Select an item to delete");
        } else {
            ImGui::SetTooltip("Delete selected item");
        }
    }
    if (!canDelete) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // 3. View Mode Toggle (Grid / List)
    const bool isGrid = (controller_.getViewMode() == ContentViewMode::Grid);
    if (isGrid) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.23F, 0.51F, 0.96F, 1.0F));
    }
    if (ImGui::Button("Grid", ImVec2(42.0F, 0.0F))) {
        controller_.setViewMode(ContentViewMode::Grid);
    }
    if (isGrid) {
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    const bool isList = (controller_.getViewMode() == ContentViewMode::List);
    if (isList) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.23F, 0.51F, 0.96F, 1.0F));
    }
    if (ImGui::Button("List", ImVec2(42.0F, 0.0F))) {
        controller_.setViewMode(ContentViewMode::List);
    }
    if (isList) {
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // 4. Sort Options
    if (ImGui::Button("Sort...")) {
        ImGui::OpenPopup("SortMenuPopup");
    }
    if (ImGui::BeginPopup("SortMenuPopup")) {
        auto curField = controller_.getSortField();
        auto curDir = controller_.getSortDirection();

        if (ImGui::MenuItem("Name", nullptr, curField == ContentSortField::Name)) {
            controller_.setSort(ContentSortField::Name, curDir);
        }
        if (ImGui::MenuItem("Type", nullptr, curField == ContentSortField::Type)) {
            controller_.setSort(ContentSortField::Type, curDir);
        }
        if (ImGui::MenuItem("Size", nullptr, curField == ContentSortField::Size)) {
            controller_.setSort(ContentSortField::Size, curDir);
        }
        if (ImGui::MenuItem("Date Modified", nullptr, curField == ContentSortField::Date)) {
            controller_.setSort(ContentSortField::Date, curDir);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Ascending", nullptr, curDir == ContentSortDirection::Ascending)) {
            controller_.setSort(curField, ContentSortDirection::Ascending);
        }
        if (ImGui::MenuItem("Descending", nullptr, curDir == ContentSortDirection::Descending)) {
            controller_.setSort(curField, ContentSortDirection::Descending);
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    // 5. Category Filter
    static const char* kCategories[] = {"All", "Meshes", "Textures", "Audio", "Scripts", "Scenes"};
    int catIdx = static_cast<int>(controller_.getFilterCategory());
    ImGui::SetNextItemWidth(90.0F);
    if (ImGui::Combo("##CategoryFilter", &catIdx, kCategories, IM_ARRAYSIZE(kCategories))) {
        controller_.setFilterCategory(static_cast<ContentFilterCategory>(catIdx));
    }

    ImGui::SameLine();
    // 6. Search Filter Input
    static char searchBuf[64] = "";
    ImGui::SetNextItemWidth(140.0F);
    if (ImGui::InputTextWithHint("##AssetSearch", "Search...", searchBuf, sizeof(searchBuf))) {
        controller_.setSearchFilter(searchBuf);
    }
}

void ContentBrowserPanel::drawBreadcrumbs() {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0F, 2.0F));
    ImGui::TextDisabled("Location:");
    ImGui::SameLine();

    const auto breadcrumbs = controller_.getBreadcrumbs();
    for (size_t i = 0; i < breadcrumbs.size(); ++i) {
        if (i > 0) {
            ImGui::TextDisabled(">");
            ImGui::SameLine();
        }

        const auto& seg = breadcrumbs[i];
        char btnId[64];
        std::snprintf(btnId, sizeof(btnId), "%s##bc_%zu", seg.name.c_str(), i);

        const bool isCurrent = (i == breadcrumbs.size() - 1);
        if (isCurrent) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 1.0F, 1.0F, 1.0F));
        }

        if (ImGui::SmallButton(btnId)) {
            controller_.navigateTo(seg.path);
        }

        if (isCurrent) {
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::PopStyleVar();
}

void ContentBrowserPanel::drawDirectoryTree() {
    const float totalHeight = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("DirTreeRegion", ImVec2(0.0F, totalHeight), false);

    std::function<void(const std::filesystem::path&)> renderDirNode;
    renderDirNode = [&](const std::filesystem::path& dirPath) {
        std::string dirName = dirPath.filename().string();
        if (dirName.empty()) {
            dirName = dirPath.string();
        }

        std::error_code ec;
        bool hasSubdirs = false;
        for (const auto& entry : std::filesystem::directory_iterator(
                 dirPath, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (entry.is_directory(ec)) {
                hasSubdirs = true;
                break;
            }
        }

        const bool isCurrent = (controller_.getCurrentDirectory() == dirPath);
        ImGuiTreeNodeFlags flags =
            ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (!hasSubdirs) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        if (isCurrent) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        char nodeLabel[128];
        std::snprintf(nodeLabel, sizeof(nodeLabel), "[D] %s", dirName.c_str());

        bool nodeOpen = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(
                                              std::hash<std::string>{}(dirPath.string()))),
                                          flags, "%s", nodeLabel);

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            controller_.navigateTo(dirPath);
        }

        if (dirPath != controller_.getRootDirectory()) {
            char treeCtxId[64];
            std::snprintf(treeCtxId, sizeof(treeCtxId), "TreeDirCtx_%zu",
                          std::hash<std::string>{}(dirPath.string()));
            if (ImGui::BeginPopupContextItem(treeCtxId)) {
                if (ImGui::MenuItem("Delete Folder...")) {
                    targetDeletePath_ = dirPath;
                    targetDeleteItemName_ = dirName;
                    targetDeleteItemType_ = "Folder";
                    isDeleteNonEmptyDir_ = !controller_.isDirectoryEmpty(dirPath);
                    showDeleteModal_ = true;
                }
                ImGui::EndPopup();
            }
        }

        if (hasSubdirs && nodeOpen) {
            for (const auto& entry : std::filesystem::directory_iterator(
                     dirPath, std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (entry.is_directory(ec)) {
                    renderDirNode(entry.path());
                }
            }
            ImGui::TreePop();
        }
    };

    renderDirNode(controller_.getRootDirectory());

    ImGui::EndChild();
}

void ContentBrowserPanel::drawAssetItems(EditorContext& ctx) {
    const float totalHeight = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("AssetItemsRegion", ImVec2(0.0F, totalHeight), false);

    const auto& items = controller_.getItems();
    if (items.empty()) {
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::TextDisabled("   This directory is empty.");
        ImGui::TextDisabled("   Use the toolbar or right-click to create new files or folders.");
    } else {
        if (controller_.getViewMode() == ContentViewMode::Grid) {
            drawGridView(ctx);
        } else {
            drawListView(ctx);
        }
    }

    // Context menu on empty area
    if (ImGui::BeginPopupContextWindow("ContentBrowserWindowCtx",
                                       ImGuiPopupFlags_MouseButtonRight |
                                           ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Create Folder")) {
            folderNameBuffer_.fill('\0');
            std::snprintf(folderNameBuffer_.data(), folderNameBuffer_.size(), "NewFolder");
            showCreateFolderModal_ = true;
        }
        if (ImGui::BeginMenu("Create File")) {
            if (ImGui::MenuItem("Empty File")) {
                fileNameBuffer_.fill('\0');
                std::snprintf(fileNameBuffer_.data(), fileNameBuffer_.size(), "NewFile");
                pendingFileTemplate_ = ContentFileTemplate::EmptyFile;
                showCreateFileModal_ = true;
            }
            if (ImGui::MenuItem("Text Document (.txt)")) {
                fileNameBuffer_.fill('\0');
                std::snprintf(fileNameBuffer_.data(), fileNameBuffer_.size(), "NewDocument.txt");
                pendingFileTemplate_ = ContentFileTemplate::TextFile;
                showCreateFileModal_ = true;
            }
            if (ImGui::MenuItem("Lua Script (.lua)")) {
                fileNameBuffer_.fill('\0');
                std::snprintf(fileNameBuffer_.data(), fileNameBuffer_.size(), "NewScript.lua");
                pendingFileTemplate_ = ContentFileTemplate::LuaScript;
                showCreateFileModal_ = true;
            }
            if (ImGui::MenuItem("Hikari Scene (.hikari)")) {
                fileNameBuffer_.fill('\0');
                std::snprintf(fileNameBuffer_.data(), fileNameBuffer_.size(), "NewScene.hikari");
                pendingFileTemplate_ = ContentFileTemplate::HikariScene;
                showCreateFileModal_ = true;
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Import Asset...")) {
            openNativeImportDialog(ctx);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Refresh")) {
            controller_.refresh();
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();
}

void ContentBrowserPanel::drawListView(EditorContext& ctx) {
    const auto& items = controller_.getItems();

    if (ImGui::BeginTable("AssetItemsTable", 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 55.0F);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Ext", ImGuiTableColumnFlags_WidthFixed, 60.0F);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 75.0F);
        ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 130.0F);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            ImGui::TableNextRow();

            // Col 0: Type Badge
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(rgbaToImVec4(item.badgeColorRgba), "%s", item.typeBadge);

            // Col 1: Name (Selectable across columns)
            ImGui::TableSetColumnIndex(1);
            const bool isSelected = (controller_.getSelectedItemIndex() == static_cast<int>(i));
            char rowLabel[256];
            std::snprintf(rowLabel, sizeof(rowLabel), "%s##row_%zu", item.name.c_str(), i);

            if (ImGui::Selectable(rowLabel, isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                controller_.setSelectedItemIndex(static_cast<int>(i));
            }

            // Double click opens folder or launches external script editor for Lua scripts
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (item.isDirectory) {
                    controller_.navigateTo(item.fullPath);
                    break;
                }
                if (ExternalEditorLauncher::isSupportedScriptFile(item.fullPath)) {
                    auto res = ExternalEditorLauncher::openScript(item.fullPath);
                    if (!res.hasValue()) {
                        errorMessage_ = res.error().message;
                        showErrorModal_ = true;
                    }
                }
            }

            // Drag and drop source for 3D meshes into Viewport
            if (item.isMesh && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("CONTENT_BROWSER_MESH", item.relativePath.c_str(),
                                          item.relativePath.size() + 1);
                ImGui::Text("Drop %s into Viewport", item.name.c_str());
                ImGui::EndDragDropSource();
            }

            // Context menu on item
            char ctxMenuId[64];
            std::snprintf(ctxMenuId, sizeof(ctxMenuId), "ItemCtx_%zu", i);
            if (ImGui::BeginPopupContextItem(ctxMenuId)) {
                controller_.setSelectedItemIndex(static_cast<int>(i));

                if (ExternalEditorLauncher::isSupportedScriptFile(item.fullPath)) {
                    if (ImGui::MenuItem("Open in External Editor")) {
                        auto res = ExternalEditorLauncher::openScript(item.fullPath);
                        if (!res.hasValue()) {
                            errorMessage_ = res.error().message;
                            showErrorModal_ = true;
                        }
                    }
                    ImGui::Separator();
                }

                if (item.isMesh) {
                    if (ImGui::MenuItem("Create Entity with Mesh")) {
                        if (ctx.world != nullptr) {
                            const std::string stem =
                                std::filesystem::path(item.name).stem().string();
                            const std::string uniqueName =
                                HierarchyPanel::generateUniqueEntityName(*ctx.world, stem.c_str());
                            auto h = ctx.world->createEntity(uniqueName);
                            ctx.world->setComponent(h, world::TransformComponent{});
                            ctx.world->setComponent(h, physics::PhysicsTransformComponent{});
                            const render::AssetId id =
                                render::makeAssetIdFromPath(item.relativePath);
                            ctx.world->setComponent(h, render::MeshComponent{
                                                           .meshAsset = id,
                                                           .assetPath = item.relativePath,
                                                           .submeshIndex = 0,
                                                           .visible = true,
                                                           .castShadows = true,
                                                       });

                            Vec3 boxHalfExtents{0.5F, 0.5F, 0.5F};
                            if (ctx.assetManager != nullptr) {
                                auto loadRes = ctx.assetManager->loadMesh(item.relativePath);
                                if (!loadRes.hasValue()) {
                                    ENG_LOG_WARN("Editor", "Failed to load mesh asset");
                                }
                                const auto* md = ctx.assetManager->getMeshData(id);
                                if (md != nullptr && md->bounds.isValid()) {
                                    boxHalfExtents = md->bounds.getSize() * 0.5F;
                                }
                            }
                            ctx.world->setComponent(
                                h, physics::ColliderComponent{
                                       .desc = physics::ColliderDesc::makeBox(boxHalfExtents),
                                   });

                            if (ctx.selection != nullptr) {
                                ctx.selection->select(h);
                            }
                            ctx.selectedEntityId = h.id;
                            ctx.markDirty();
                        }
                    }
                    ImGui::Separator();
                }

                if (ImGui::MenuItem("Rename...")) {
                    renameBuffer_.fill('\0');
                    std::snprintf(renameBuffer_.data(), renameBuffer_.size(), "%s",
                                  item.name.c_str());
                    targetRenamePath_ = item.fullPath;
                    showRenameModal_ = true;
                }

                if (!item.isDirectory && ImGui::MenuItem("Duplicate")) {
                    auto dupRes = controller_.duplicateFile(item.fullPath);
                    if (!dupRes.hasValue()) {
                        errorMessage_ = dupRes.error().message;
                        showErrorModal_ = true;
                    }
                }

                if (ImGui::MenuItem("Delete...")) {
                    targetDeletePath_ = item.fullPath;
                    targetDeleteItemName_ = item.name;
                    targetDeleteItemType_ =
                        item.isDirectory
                            ? "Folder"
                            : (item.extension.empty() ? "File" : (item.extension + " File"));
                    isDeleteNonEmptyDir_ =
                        (item.isDirectory && !controller_.isDirectoryEmpty(item.fullPath));
                    showDeleteModal_ = true;
                }

                ImGui::Separator();
                if (ImGui::MenuItem("Copy Asset Path")) {
                    ImGui::SetClipboardText(item.relativePath.c_str());
                }

                ImGui::EndPopup();
            }

            // Col 2: Ext
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("%s", item.extension.c_str());

            // Col 3: Size
            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("%s", item.sizeFormatted.c_str());

            // Col 4: Modified
            ImGui::TableSetColumnIndex(4);
            ImGui::TextDisabled("%s", item.lastModifiedFormatted.c_str());
        }
        ImGui::EndTable();
    }
}

void ContentBrowserPanel::drawGridView(EditorContext& ctx) {
    const auto& items = controller_.getItems();
    const float cardWidth = 84.0F;
    const float cardHeight = 90.0F;
    const float availWidth = ImGui::GetContentRegionAvail().x;
    int cols = static_cast<int>(availWidth / (cardWidth + 12.0F));
    if (cols < 1) {
        cols = 1;
    }

    for (size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        if (i % static_cast<size_t>(cols) != 0) {
            ImGui::SameLine();
        }

        ImGui::PushID(static_cast<int>(i));
        const bool isSelected = (controller_.getSelectedItemIndex() == static_cast<int>(i));

        const ImVec2 cardMin = ImGui::GetCursorScreenPos();
        const ImVec2 cardMax = ImVec2(cardMin.x + cardWidth, cardMin.y + cardHeight);

        char cardSelectableId[64];
        std::snprintf(cardSelectableId, sizeof(cardSelectableId), "##card_%zu", i);

        if (ImGui::Selectable(cardSelectableId, isSelected, ImGuiSelectableFlags_None,
                              ImVec2(cardWidth, cardHeight))) {
            controller_.setSelectedItemIndex(static_cast<int>(i));
        }

        const bool isCardHovered = ImGui::IsItemHovered();

        // Double-click opens folder or launches external script editor for Lua scripts
        if (isCardHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (item.isDirectory) {
                controller_.navigateTo(item.fullPath);
                ImGui::PopID();
                break;
            }
            if (ExternalEditorLauncher::isSupportedScriptFile(item.fullPath)) {
                auto res = ExternalEditorLauncher::openScript(item.fullPath);
                if (!res.hasValue()) {
                    errorMessage_ = res.error().message;
                    showErrorModal_ = true;
                }
            }
        }

        // Drag & drop source
        if (item.isMesh && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("CONTENT_BROWSER_MESH", item.relativePath.c_str(),
                                      item.relativePath.size() + 1);
            ImGui::Text("Drop %s into Viewport", item.name.c_str());
            ImGui::EndDragDropSource();
        }

        // Render card background & visuals over selectable
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (isSelected) {
            drawList->AddRectFilled(cardMin, cardMax, ImColor(59, 130, 246, 64), 4.0F);
            drawList->AddRect(cardMin, cardMax, ImColor(59, 130, 246, 255), 4.0F);
        } else {
            drawList->AddRectFilled(cardMin, cardMax,
                                    ImColor(30, 35, 45, isCardHovered ? 200 : 153), 4.0F);
            drawList->AddRect(cardMin, cardMax, ImColor(60, 65, 75, isCardHovered ? 220 : 150),
                              4.0F);
        }

        // Badge icon
        const ImVec2 badgeSize = ImGui::CalcTextSize(item.typeBadge);
        const ImVec2 badgePos(cardMin.x + (cardWidth - badgeSize.x) * 0.5F, cardMin.y + 12.0F);
        drawList->AddText(badgePos, item.badgeColorRgba, item.typeBadge);

        // Name
        std::string displayName = item.name;
        if (displayName.size() > 10) {
            displayName = displayName.substr(0, 8) + "..";
        }
        const ImVec2 nameSize = ImGui::CalcTextSize(displayName.c_str());
        const ImVec2 namePos(cardMin.x + std::max(4.0F, (cardWidth - nameSize.x) * 0.5F),
                             cardMin.y + cardHeight - 24.0F);
        drawList->AddText(namePos, ImColor(220, 220, 220, 255), displayName.c_str());

        // Context menu
        if (ImGui::BeginPopupContextItem("GridItemCtx")) {
            controller_.setSelectedItemIndex(static_cast<int>(i));

            if (ExternalEditorLauncher::isSupportedScriptFile(item.fullPath)) {
                if (ImGui::MenuItem("Open in External Editor")) {
                    auto res = ExternalEditorLauncher::openScript(item.fullPath);
                    if (!res.hasValue()) {
                        errorMessage_ = res.error().message;
                        showErrorModal_ = true;
                    }
                }
                ImGui::Separator();
            }

            if (item.isMesh && ImGui::MenuItem("Create Entity with Mesh")) {
                if (ctx.world != nullptr) {
                    const std::string stem = std::filesystem::path(item.name).stem().string();
                    const std::string uniqueName =
                        HierarchyPanel::generateUniqueEntityName(*ctx.world, stem.c_str());
                    auto h = ctx.world->createEntity(uniqueName);
                    ctx.world->setComponent(h, world::TransformComponent{});
                    ctx.world->setComponent(h, physics::PhysicsTransformComponent{});
                    const render::AssetId id = render::makeAssetIdFromPath(item.relativePath);
                    ctx.world->setComponent(h, render::MeshComponent{
                                                   .meshAsset = id,
                                                   .assetPath = item.relativePath,
                                                   .submeshIndex = 0,
                                                   .visible = true,
                                                   .castShadows = true,
                                               });

                    Vec3 boxHalfExtents{0.5F, 0.5F, 0.5F};
                    if (ctx.assetManager != nullptr) {
                        auto loadRes = ctx.assetManager->loadMesh(item.relativePath);
                        if (!loadRes.hasValue()) {
                            ENG_LOG_WARN("Editor", "Failed to load mesh asset");
                        }
                        const auto* md = ctx.assetManager->getMeshData(id);
                        if (md != nullptr && md->bounds.isValid()) {
                            boxHalfExtents = md->bounds.getSize() * 0.5F;
                        }
                    }
                    ctx.world->setComponent(
                        h, physics::ColliderComponent{
                               .desc = physics::ColliderDesc::makeBox(boxHalfExtents),
                           });

                    if (ctx.selection != nullptr) {
                        ctx.selection->select(h);
                    }
                    ctx.selectedEntityId = h.id;
                    ctx.markDirty();
                }
            }

            if (ImGui::MenuItem("Rename...")) {
                renameBuffer_.fill('\0');
                std::snprintf(renameBuffer_.data(), renameBuffer_.size(), "%s", item.name.c_str());
                targetRenamePath_ = item.fullPath;
                showRenameModal_ = true;
            }

            if (!item.isDirectory && ImGui::MenuItem("Duplicate")) {
                auto dupRes = controller_.duplicateFile(item.fullPath);
                if (!dupRes.hasValue()) {
                    errorMessage_ = dupRes.error().message;
                    showErrorModal_ = true;
                }
            }

            if (ImGui::MenuItem("Delete...")) {
                targetDeletePath_ = item.fullPath;
                targetDeleteItemName_ = item.name;
                targetDeleteItemType_ =
                    item.isDirectory
                        ? "Folder"
                        : (item.extension.empty() ? "File" : (item.extension + " File"));
                isDeleteNonEmptyDir_ =
                    (item.isDirectory && !controller_.isDirectoryEmpty(item.fullPath));
                showDeleteModal_ = true;
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Copy Asset Path")) {
                ImGui::SetClipboardText(item.relativePath.c_str());
            }

            ImGui::EndPopup();
        }

        ImGui::PopID();
    }
}

void ContentBrowserPanel::drawFooter(EditorContext& ctx) {
    const auto* sel = controller_.getSelectedItem();
    if (sel != nullptr) {
        ImGui::TextColored(rgbaToImVec4(sel->badgeColorRgba), "%s", sel->typeBadge);
        ImGui::SameLine();
        ImGui::Text("%s", sel->name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("| %s | %s", sel->sizeFormatted.c_str(), sel->relativePath.c_str());

        if (sel->isMesh) {
            const render::AssetId meshId = render::makeAssetIdFromPath(sel->relativePath);
            const render::MeshData* md = nullptr;
            if (ctx.assetManager != nullptr) {
                md = ctx.assetManager->getMeshData(meshId);
                if (md == nullptr) {
                    (void)ctx.assetManager->loadMesh(sel->relativePath);
                    md = ctx.assetManager->getMeshData(meshId);
                }
            }

            if (md != nullptr) {
                ImGui::TextColored(
                    ImVec4(0.35F, 0.85F, 0.40F, 1.0F),
                    "Mesh Stats: %u vertices, %u triangles, %zu submeshes, %zu materials",
                    md->getVertexCount(), md->getTriangleCount(), md->submeshes.size(),
                    md->materials.size());
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("+ Create Entity with Mesh")) {
                if (ctx.world != nullptr) {
                    auto h = ctx.world->createEntity(sel->name);
                    ctx.world->setComponent(h, world::TransformComponent{});
                    ctx.world->setComponent(h, physics::PhysicsTransformComponent{});
                    ctx.world->setComponent(h, render::MeshComponent{
                                                   .meshAsset = meshId,
                                                   .assetPath = sel->relativePath,
                                                   .submeshIndex = 0,
                                                   .visible = true,
                                                   .castShadows = true,
                                               });

                    Vec3 boxHalfExtents{0.5F, 0.5F, 0.5F};
                    if (md != nullptr && md->bounds.isValid()) {
                        boxHalfExtents = md->bounds.getSize() * 0.5F;
                    }
                    ctx.world->setComponent(
                        h, physics::ColliderComponent{
                               .desc = physics::ColliderDesc::makeBox(boxHalfExtents),
                           });

                    if (ctx.selection != nullptr) {
                        ctx.selection->select(h);
                    }
                    ctx.selectedEntityId = h.id;
                    ctx.markDirty();
                }
            }

            if (ctx.selectedEntityId != 0 && ctx.world != nullptr) {
                world::EntityHandle selH(ctx.selectedEntityId);
                if (ctx.world->isAlive(selH)) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Assign to Selected Entity")) {
                        ctx.world->setComponent(selH, render::MeshComponent{
                                                          .meshAsset = meshId,
                                                          .assetPath = sel->relativePath,
                                                          .submeshIndex = 0,
                                                          .visible = true,
                                                          .castShadows = true,
                                                      });
                        ctx.markDirty();
                    }
                }
            }
        } else if (sel->isDirectory) {
            ImGui::TextDisabled("Folder. Double-click to navigate into it.");
        } else {
            ImGui::TextDisabled("File ready for project usage.");
        }
    } else {
        ImGui::TextDisabled("No item selected.");
    }
}

void ContentBrowserPanel::drawModals() {
    // 1. Create Folder Modal
    if (showCreateFolderModal_) {
        ImGui::OpenPopup("Create Folder##Modal");
        showCreateFolderModal_ = false;
    }

    if (ImGui::BeginPopupModal("Create Folder##Modal", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::Text("Enter folder name:");
        const bool enterPressed =
            ImGui::InputText("##FolderNameInput", folderNameBuffer_.data(),
                             folderNameBuffer_.size(), ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::Spacing();
        if (ImGui::Button("Create", ImVec2(80.0F, 0.0F)) || enterPressed) {
            auto res = controller_.createFolder(folderNameBuffer_.data());
            if (!res.hasValue()) {
                errorMessage_ = res.error().message;
                showErrorModal_ = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80.0F, 0.0F)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // 2. Create File Modal
    if (showCreateFileModal_) {
        ImGui::OpenPopup("Create File##Modal");
        showCreateFileModal_ = false;
    }

    if (ImGui::BeginPopupModal("Create File##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::Text("Enter file name:");
        const bool enterPressed =
            ImGui::InputText("##FileNameInput", fileNameBuffer_.data(), fileNameBuffer_.size(),
                             ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::Spacing();
        if (ImGui::Button("Create", ImVec2(80.0F, 0.0F)) || enterPressed) {
            auto res = controller_.createFile(fileNameBuffer_.data(), pendingFileTemplate_);
            if (!res.hasValue()) {
                errorMessage_ = res.error().message;
                showErrorModal_ = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80.0F, 0.0F)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // 3. Rename Modal
    if (showRenameModal_) {
        ImGui::OpenPopup("Rename##Modal");
        showRenameModal_ = false;
    }

    if (ImGui::BeginPopupModal("Rename##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::Text("Enter new name for '%s':", targetRenamePath_.filename().string().c_str());
        const bool enterPressed = ImGui::InputText(
            "##RenameInput", renameBuffer_.data(), renameBuffer_.size(),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

        ImGui::Spacing();
        if (ImGui::Button("Rename", ImVec2(80.0F, 0.0F)) || enterPressed) {
            auto res = controller_.renameItem(targetRenamePath_, renameBuffer_.data());
            if (!res.hasValue()) {
                errorMessage_ = res.error().message;
                showErrorModal_ = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80.0F, 0.0F)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // 4. Delete Confirmation Modal
    if (showDeleteModal_) {
        ImGui::OpenPopup("Confirm Delete##Modal");
        showDeleteModal_ = false;
    }

    if (ImGui::BeginPopupModal("Confirm Delete##Modal", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        std::string itemName = targetDeleteItemName_;
        if (itemName.empty()) {
            itemName = targetDeletePath_.filename().string();
        }
        std::string itemType = targetDeleteItemType_;
        if (itemType.empty()) {
            itemType = isDeleteNonEmptyDir_ ? "Folder" : "Item";
        }

        ImGui::Text("Item: %s", itemName.c_str());
        ImGui::Text("Type: %s", itemType.c_str());
        ImGui::Spacing();

        if (isDeleteNonEmptyDir_) {
            ImGui::TextColored(ImVec4(1.0F, 0.4F, 0.4F, 1.0F), "WARNING: Non-empty folder!");
            ImGui::Text("This folder contains files or subdirectories.");
            ImGui::Text("Are you sure you want to permanently delete it and all its contents?");
        } else {
            ImGui::Text("Are you sure you want to delete this %s?", itemType.c_str());
        }

        ImGui::Spacing();
        if (ImGui::Button("Delete", ImVec2(80.0F, 0.0F)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            auto res = controller_.deleteItem(targetDeletePath_, isDeleteNonEmptyDir_);
            if (!res.hasValue()) {
                errorMessage_ = res.error().message;
                showErrorModal_ = true;
            } else {
                controller_.setSelectedItemIndex(-1);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80.0F, 0.0F)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // 5. Error Modal
    if (showErrorModal_) {
        ImGui::OpenPopup("Filesystem Error##Modal");
        showErrorModal_ = false;
    }

    if (ImGui::BeginPopupModal("Filesystem Error##Modal", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(1.0F, 0.35F, 0.35F, 1.0F), "Operation Failed:");
        ImGui::TextWrapped("%s", errorMessage_.c_str());

        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(80.0F, 0.0F)) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // 6. Manual Import Modal (Fallback)
    if (showImportPathModal_) {
        ImGui::OpenPopup("Import Asset Path##Modal");
        showImportPathModal_ = false;
    }

    if (ImGui::BeginPopupModal("Import Asset Path##Modal", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter full or relative path to asset file to import:");
        ImGui::InputText("##ImportPathInput", importPathBuffer_.data(), importPathBuffer_.size());
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Import", ImVec2(100.0F, 0.0F))) {
            if (importPathBuffer_[0] != '\0') {
                std::filesystem::path p(importPathBuffer_.data());
                queuePendingImports({p});
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0F, 0.0F))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ContentBrowserPanel::openNativeImportDialog(EditorContext& ctx) {
    (void)ctx;
    static const SDL_DialogFileFilter filters[] = {
        {"All Supported Assets (*.obj, *.gltf, *.glb, *.png, *.jpg, ...)",
         "obj;gltf;glb;png;jpg;jpeg;tga;bmp;wav;ogg;mp3;flac;lua;hikari;json;toml;txt;mtl"},
        {"3D Models (*.obj, *.gltf, *.glb)", "obj;gltf;glb"},
        {"Wavefront OBJ (*.obj)", "obj"},
        {"glTF / GLB (*.gltf, *.glb)", "gltf;glb"},
        {"Textures (*.png, *.jpg, *.jpeg, *.tga, *.bmp)", "png;jpg;jpeg;tga;bmp"},
        {"Audio (*.wav, *.ogg, *.mp3, *.flac)", "wav;ogg;mp3;flac"},
        {"Scripts & Scenes (*.lua, *.hikari)", "lua;hikari"},
        {"All Files (*.*)", "*"}};

    const std::filesystem::path currentDir = controller_.getCurrentDirectory();
    std::error_code ec;
    std::string defaultLocation = std::filesystem::absolute(currentDir, ec).string();

    SDL_ShowOpenFileDialog(
        [](void* userdata, const char* const* filelist, int filter) {
            (void)filter;
            auto* panel = static_cast<ContentBrowserPanel*>(userdata);
            if (panel == nullptr) {
                return;
            }
            if (filelist == nullptr) {
                const char* err = SDL_GetError();
                std::string errStr =
                    (err != nullptr && *err != '\0') ? err : "Failed to open native file dialog";
                panel->recordNativeDialogError(errStr);
                return;
            }
            if (*filelist == nullptr) {
                // User cancelled dialog
                return;
            }
            std::vector<std::filesystem::path> paths;
            for (const char* const* p = filelist; *p != nullptr; ++p) {
                paths.emplace_back(*p);
            }
            panel->queuePendingImports(std::move(paths));
        },
        this, nullptr, filters, static_cast<int>(sizeof(filters) / sizeof(filters[0])),
        defaultLocation.c_str(), true);
}

void ContentBrowserPanel::queuePendingImports(std::vector<std::filesystem::path> paths) {
    if (paths.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(pendingImportMutex_);
    pendingImportPaths_.insert(pendingImportPaths_.end(), std::make_move_iterator(paths.begin()),
                               std::make_move_iterator(paths.end()));
}

void ContentBrowserPanel::recordNativeDialogError(std::string_view errorMsg) {
    std::lock_guard<std::mutex> lock(pendingImportMutex_);
    nativeDialogError_ = std::string(errorMsg);
}

void ContentBrowserPanel::processPendingImports(EditorContext& ctx) {
    std::vector<std::filesystem::path> pathsToImport;
    std::string nativeErr;
    {
        std::lock_guard<std::mutex> lock(pendingImportMutex_);
        pathsToImport.swap(pendingImportPaths_);
        nativeErr.swap(nativeDialogError_);
    }

    if (!nativeErr.empty()) {
        errorMessage_ = "Native file dialog error: " + nativeErr;
        showErrorModal_ = true;
        showImportPathModal_ = true;
    }

    if (pathsToImport.empty()) {
        return;
    }

    size_t successCount = 0;
    std::string firstError{};

    for (const auto& src : pathsToImport) {
        auto res = controller_.importFile(src);
        if (!res.hasValue()) {
            if (firstError.empty()) {
                firstError = res.error().message;
            }
            ENG_LOG_WARN("Editor", "Failed to import asset into Content Browser");
        } else {
            successCount++;
            const auto& dest = res.value();
            if (ctx.assetManager != nullptr) {
                std::string ext = dest.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext == ".obj" || ext == ".gltf" || ext == ".glb") {
                    std::string relPath =
                        ContentBrowserController::normalizeSeparators(dest.string());
                    (void)ctx.assetManager->loadMesh(relPath);
                }
            }
        }
    }

    if (successCount > 0) {
        ctx.markDirty();
        ENG_LOG_INFO("Editor", "Successfully imported asset into Content Browser");
    }

    if (!firstError.empty()) {
        errorMessage_ = firstError;
        showErrorModal_ = true;
    }
}

} // namespace eng::editor
