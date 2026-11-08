/// @file content_browser_panel.cpp
/// @brief Content browser panel implementation with asset browsing, mesh metadata cards, and
/// instantiation.

#include "panels/content_browser_panel.h"

#include "editor/editor_theme.h"
#include "editor/selection_model.h"
#include "physics/physics_components.h"
#include "render/asset_manager.h"
#include "render/mesh_component.h"
#include "world/world.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <imgui.h>
#include <string>
#include <vector>

namespace eng::editor {

namespace {

struct AssetItem {
    std::string name;
    std::string extension;
    std::string relativePath;
    std::string sizeStr;
    const char* typeBadge{"[ASSET]"};
    ImVec4 badgeColor{0.7F, 0.7F, 0.7F, 1.0F};
    bool isMesh{false};
};

std::string formatFileSize(uintmax_t bytes) {
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

std::vector<AssetItem> discoverAssets() {
    std::vector<AssetItem> items;

    // 1. Scan filesystem for real assets in assets/
    const std::filesystem::path modelsDir = "assets/models";
    if (std::filesystem::exists(modelsDir) && std::filesystem::is_directory(modelsDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(modelsDir)) {
            if (entry.is_regular_file()) {
                const auto& path = entry.path();
                const std::string ext = path.extension().string();
                std::string relPath = path.string();
                std::replace(relPath.begin(), relPath.end(), '\\', '/');

                if (ext == ".obj") {
                    AssetItem item{};
                    item.name = path.stem().string();
                    item.extension = ext;
                    item.relativePath = relPath;
                    item.sizeStr = formatFileSize(entry.file_size());
                    item.typeBadge = "[OBJ]";
                    item.badgeColor = ImVec4(0.20F, 0.70F, 0.95F, 1.0F);
                    item.isMesh = true;
                    items.push_back(std::move(item));
                }
            }
        }
    }

    // 2. Sample standard engine placeholders
    const AssetItem samplePlaceholders[] = {
        {"player_mesh", ".gltf", "assets/models/player_mesh.gltf", "2.4 MB", "[3D]",
         ImVec4(0.23F, 0.51F, 0.96F, 1.0F), false},
        {"albedo_grid", ".png", "assets/textures/albedo_grid.png", "1.2 MB", "[TEX]",
         ImVec4(0.13F, 0.77F, 0.37F, 1.0F), false},
        {"audio_sample", ".wav", "assets/audio/audio_sample.wav", "340 KB", "[AUD]",
         ImVec4(0.96F, 0.62F, 0.04F, 1.0F), false},
        {"game_config", ".toml", "data/game_config.toml", "24 KB", "[DATA]",
         ImVec4(0.70F, 0.40F, 0.90F, 1.0F), false},
        {"main", ".lua", "game/scripts/main.lua", "8 KB", "[LUA]",
         ImVec4(0.92F, 0.30F, 0.60F, 1.0F), false},
    };

    for (const auto& sample : samplePlaceholders) {
        items.push_back(sample);
    }

    return items;
}

int s_selectedAssetIndex = 0;
char s_assetSearchFilter[64] = "";

} // namespace

void ContentBrowserPanel::draw(EditorContext& ctx) {
    if (!isOpen_) {
        return;
    }

    const auto assets = discoverAssets();

    if (ImGui::Begin(getDisplayName().data(), &isOpen_)) {
        // 1. Navigation Breadcrumb & Search Bar
        ImGui::TextDisabled("Location: Content > assets > models");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0F);
        ImGui::InputTextWithHint("##AssetSearch", "Search assets...", s_assetSearchFilter,
                                 sizeof(s_assetSearchFilter));

        ImGui::Separator();

        // 2. Two-Pane Layout
        const float footerHeight = 68.0F;
        const float totalHeight = ImGui::GetContentRegionAvail().y - footerHeight;
        ImGui::Columns(2, "ContentBrowserSplit", true);
        ImGui::SetColumnWidth(0, 180.0F);

        // Left Pane: Directory Tree
        ImGui::BeginChild("DirTreeRegion", ImVec2(0.0F, totalHeight), false);
        if (ImGui::TreeNodeEx("assets", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Selectable("  models", true)) {
            }
            if (ImGui::Selectable("  textures", false)) {
            }
            if (ImGui::Selectable("  audio", false)) {
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("data", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Selectable("  tables", false)) {
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("game", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Selectable("  scripts", false)) {
            }
            if (ImGui::Selectable("  modes", false)) {
            }
            ImGui::TreePop();
        }
        ImGui::EndChild();

        ImGui::NextColumn();

        // Right Pane: Asset Items Table
        ImGui::BeginChild("AssetItemsRegion", ImVec2(0.0F, totalHeight), false);
        if (ImGui::BeginTable("AssetItemsTable", 4,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 50.0F);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Ext", ImGuiTableColumnFlags_WidthFixed, 60.0F);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 75.0F);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < assets.size(); ++i) {
                const auto& item = assets[i];
                if (s_assetSearchFilter[0] != '\0' &&
                    item.name.find(s_assetSearchFilter) == std::string::npos) {
                    continue;
                }

                ImGui::TableNextRow();

                // Col 0: Type Badge
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(item.badgeColor, "%s", item.typeBadge);

                // Col 1: Name (Selectable across columns)
                ImGui::TableSetColumnIndex(1);
                const bool isSelected = (s_selectedAssetIndex == static_cast<int>(i));
                char rowLabel[128];
                std::snprintf(rowLabel, sizeof(rowLabel), "%s##%zu", item.name.c_str(), i);
                if (ImGui::Selectable(rowLabel, isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                    s_selectedAssetIndex = static_cast<int>(i);
                }

                // Context menu for asset actions
                if (ImGui::BeginPopupContextItem()) {
                    if (item.isMesh) {
                        if (ImGui::MenuItem("Create Entity with Mesh")) {
                            if (ctx.world != nullptr) {
                                auto h = ctx.world->createEntity(item.name);
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

                                // Auto-size collider if mesh data is available
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
                ImGui::TextDisabled("%s", item.sizeStr.c_str());
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::Columns(1);
        ImGui::Separator();

        // 3. Footer Bar: Selected Asset Details & Instantiation Actions
        if (s_selectedAssetIndex >= 0 && s_selectedAssetIndex < static_cast<int>(assets.size())) {
            const auto& sel = assets[s_selectedAssetIndex];
            ImGui::TextColored(sel.badgeColor, "%s", sel.typeBadge);
            ImGui::SameLine();
            ImGui::Text("%s%s", sel.name.c_str(), sel.extension.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("| %s | %s", sel.sizeStr.c_str(), sel.relativePath.c_str());

            if (sel.isMesh) {
                const render::AssetId meshId = render::makeAssetIdFromPath(sel.relativePath);
                const render::MeshData* md = nullptr;
                if (ctx.assetManager != nullptr) {
                    md = ctx.assetManager->getMeshData(meshId);
                    if (md == nullptr) {
                        (void)ctx.assetManager->loadMesh(sel.relativePath);
                        md = ctx.assetManager->getMeshData(meshId);
                    }
                }

                if (md != nullptr) {
                    ImGui::TextColored(ImVec4(0.35F, 0.85F, 0.40F, 1.0F),
                                       "Mesh Stats: %u vertices, %u triangles, %zu submeshes",
                                       md->getVertexCount(), md->getTriangleCount(),
                                       md->submeshes.size());
                }

                ImGui::SameLine();
                if (ImGui::SmallButton("+ Create Entity with Mesh")) {
                    if (ctx.world != nullptr) {
                        auto h = ctx.world->createEntity(sel.name);
                        ctx.world->setComponent(h, physics::PhysicsTransformComponent{});
                        ctx.world->setComponent(h, render::MeshComponent{
                                                       .meshAsset = meshId,
                                                       .assetPath = sel.relativePath,
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
                                                              .assetPath = sel.relativePath,
                                                              .submeshIndex = 0,
                                                              .visible = true,
                                                              .castShadows = true,
                                                          });
                            ctx.markDirty();
                        }
                    }
                }
            } else {
                ImGui::TextDisabled("Ready for instantiation.");
            }
        } else {
            ImGui::TextDisabled("No asset selected.");
        }
    }
    ImGui::End();
}

} // namespace eng::editor
