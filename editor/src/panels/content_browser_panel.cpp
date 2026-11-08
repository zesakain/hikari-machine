/// @file content_browser_panel.cpp
/// @brief Content browser panel implementation with two-pane asset browsing and metadata cards.

#include "panels/content_browser_panel.h"

#include "editor/editor_theme.h"

#include <cstdio>
#include <cstring>
#include <imgui.h>

namespace eng::editor {

namespace {
struct AssetItem {
    const char* name;
    const char* extension;
    const char* sizeStr;
    const char* typeBadge;
    ImVec4 badgeColor;
};

const AssetItem kSampleAssets[] = {
    {"player_mesh", ".gltf", "2.4 MB", "[3D]", ImVec4(0.23F, 0.51F, 0.96F, 1.0F)},
    {"environment", ".gltf", "8.1 MB", "[3D]", ImVec4(0.23F, 0.51F, 0.96F, 1.0F)},
    {"albedo_grid", ".png", "1.2 MB", "[TEX]", ImVec4(0.13F, 0.77F, 0.37F, 1.0F)},
    {"normal_noise", ".png", "512 KB", "[TEX]", ImVec4(0.13F, 0.77F, 0.37F, 1.0F)},
    {"audio_sample", ".wav", "340 KB", "[AUD]", ImVec4(0.96F, 0.62F, 0.04F, 1.0F)},
    {"ambient_loop", ".wav", "180 KB", "[AUD]", ImVec4(0.96F, 0.62F, 0.04F, 1.0F)},
    {"game_config", ".toml", "24 KB", "[DATA]", ImVec4(0.70F, 0.40F, 0.90F, 1.0F)},
    {"entities", ".toml", "16 KB", "[DATA]", ImVec4(0.70F, 0.40F, 0.90F, 1.0F)},
    {"main", ".lua", "8 KB", "[LUA]", ImVec4(0.92F, 0.30F, 0.60F, 1.0F)},
};

int s_selectedAssetIndex = 0;
char s_assetSearchFilter[64] = "";
} // namespace

void ContentBrowserPanel::draw([[maybe_unused]] EditorContext& ctx) {
    if (!isOpen_) {
        return;
    }

    if (ImGui::Begin(getDisplayName().data(), &isOpen_)) {
        // 1. Navigation Breadcrumb & Search Bar
        ImGui::TextDisabled("Location: Content > assets > models");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0F);
        ImGui::InputTextWithHint("##AssetSearch", "Search assets...", s_assetSearchFilter,
                                 sizeof(s_assetSearchFilter));

        ImGui::Separator();

        // 2. Two-Pane Layout
        float totalHeight = ImGui::GetContentRegionAvail().y - 28.0F; // Leave space for footer
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

            for (size_t i = 0; i < sizeof(kSampleAssets) / sizeof(kSampleAssets[0]); ++i) {
                const auto& item = kSampleAssets[i];
                if (s_assetSearchFilter[0] != '\0' &&
                    strstr(item.name, s_assetSearchFilter) == nullptr) {
                    continue;
                }

                ImGui::TableNextRow();

                // Col 0: Type Badge
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(item.badgeColor, "%s", item.typeBadge);

                // Col 1: Name (Selectable across columns)
                ImGui::TableSetColumnIndex(1);
                bool isSelected = (s_selectedAssetIndex == static_cast<int>(i));
                char rowLabel[64];
                std::snprintf(rowLabel, sizeof(rowLabel), "%s##%zu", item.name, i);
                if (ImGui::Selectable(rowLabel, isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                    s_selectedAssetIndex = static_cast<int>(i);
                }

                // Col 2: Ext
                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("%s", item.extension);

                // Col 3: Size
                ImGui::TableSetColumnIndex(3);
                ImGui::TextDisabled("%s", item.sizeStr);
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::Columns(1);
        ImGui::Separator();

        // 3. Footer Bar: Selected Asset Details
        if (s_selectedAssetIndex >= 0 &&
            s_selectedAssetIndex <
                static_cast<int>(sizeof(kSampleAssets) / sizeof(kSampleAssets[0]))) {
            const auto& sel = kSampleAssets[s_selectedAssetIndex];
            ImGui::TextColored(sel.badgeColor, "%s", sel.typeBadge);
            ImGui::SameLine();
            ImGui::Text("%s%s", sel.name, sel.extension);
            ImGui::SameLine();
            ImGui::TextDisabled("| Size: %s | Ready for instantiation", sel.sizeStr);
        } else {
            ImGui::TextDisabled("No asset selected.");
        }
    }
    ImGui::End();
}

} // namespace eng::editor
