/// @file content_browser_panel.cpp
/// @brief Content browser panel implementation with dynamic directory navigation, history, and
/// breadcrumbs.

#include "panels/content_browser_panel.h"

#include "core/log.h"
#include "editor/editor_theme.h"
#include "editor/selection_model.h"
#include "physics/physics_components.h"
#include "render/asset_manager.h"
#include "render/mesh_component.h"
#include "world/world.h"

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
    if (!isOpen_) {
        return;
    }

    if (ImGui::Begin(getDisplayName().data(), &isOpen_)) {
        drawToolbar(ctx);
        drawBreadcrumbs();

        ImGui::Separator();

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
    }
    ImGui::End();
}

void ContentBrowserPanel::drawToolbar(EditorContext& ctx) {
    (void)ctx;
    // Navigation Buttons: Back, Forward, Up, Refresh
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

    // Search Filter Input
    static char searchBuf[64] = "";
    ImGui::SetNextItemWidth(160.0F);
    if (ImGui::InputTextWithHint("##AssetSearch", "Search assets...", searchBuf, sizeof(searchBuf))) {
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

        bool nodeOpen =
            ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(
                                  std::hash<std::string>{}(dirPath.string()))),
                              flags, "%s", nodeLabel);

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            controller_.navigateTo(dirPath);
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
    } else {
        if (ImGui::BeginTable("AssetItemsTable", 4,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 55.0F);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Ext", ImGuiTableColumnFlags_WidthFixed, 60.0F);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 75.0F);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < items.size(); ++i) {
                const auto& item = items[i];
                ImGui::TableNextRow();

                // Col 0: Type Badge
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(rgbaToImVec4(item.badgeColorRgba), "%s", item.typeBadge);

                // Col 1: Name
                ImGui::TableSetColumnIndex(1);
                const bool isSelected = (controller_.getSelectedItemIndex() == static_cast<int>(i));
                char rowLabel[256];
                std::snprintf(rowLabel, sizeof(rowLabel), "%s##row_%zu", item.name.c_str(), i);

                if (ImGui::Selectable(rowLabel, isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                    controller_.setSelectedItemIndex(static_cast<int>(i));
                }

                // Double click opens folder
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (item.isDirectory) {
                        controller_.navigateTo(item.fullPath);
                        break;
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

                    if (item.isMesh) {
                        if (ImGui::MenuItem("Create Entity with Mesh")) {
                            if (ctx.world != nullptr) {
                                auto h = ctx.world->createEntity(item.name);
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
                                    const auto* md = ctx.assetManager->getMeshData(id);
                                    if (md == nullptr) {
                                        (void)ctx.assetManager->loadMesh(item.relativePath);
                                        md = ctx.assetManager->getMeshData(id);
                                    }
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
            }
            ImGui::EndTable();
        }
    }

    ImGui::EndChild();
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

} // namespace eng::editor
