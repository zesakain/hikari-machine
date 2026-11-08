/// @file console_panel.cpp
/// @brief Structured log console panel implementation with log level badges and detail preview.

#include "panels/console_panel.h"

#include "core/log.h"
#include "editor/editor_theme.h"

#include <cstring>
#include <imgui.h>

namespace eng::editor {

namespace {
size_t s_selectedLogIndex = static_cast<size_t>(-1);
bool s_collapseDuplicates = false;
} // namespace

void ConsolePanel::draw([[maybe_unused]] EditorContext& ctx) {
    if (!isOpen_) {
        return;
    }

    if (ImGui::Begin(getDisplayName().data(), &isOpen_)) {
        auto& logger = Logger::defaultLogger();

        // 1. Top Filter Bar
        if (ImGui::Button("Clear")) {
            logger.clear();
            s_selectedLogIndex = static_cast<size_t>(-1);
        }
        ImGui::SameLine();

        // Level filter buttons
        auto drawFilterBtn = [this](int levelIdx, const char* name, ImVec4 color) {
            bool isSelected = (selectedLogLevel_ == levelIdx);
            if (isSelected) {
                ImGui::PushStyleColor(ImGuiCol_Button, color);
            }
            if (ImGui::Button(name)) {
                selectedLogLevel_ = levelIdx;
            }
            if (isSelected) {
                ImGui::PopStyleColor();
            }
            ImGui::SameLine();
        };

        drawFilterBtn(0, "All", ImVec4(0.25F, 0.28F, 0.35F, 1.0F));
        drawFilterBtn(1, "Info", ImVec4(0.20F, 0.45F, 0.85F, 1.0F));
        drawFilterBtn(2, "Warning", ImVec4(0.85F, 0.55F, 0.05F, 1.0F));
        drawFilterBtn(3, "Error", ImVec4(0.85F, 0.20F, 0.20F, 1.0F));

        ImGui::SetNextItemWidth(140.0F);
        ImGui::InputTextWithHint("##LogSearch", "Search logs...", filterBuffer_.data(),
                                 filterBuffer_.size());

        ImGui::SameLine();
        ImGui::Checkbox("Auto-Scroll", &autoScroll_);
        ImGui::SameLine();
        ImGui::Checkbox("Collapse", &s_collapseDuplicates);

        ImGui::SameLine();
        ImGui::TextDisabled("| %zu / %zu entries", logger.count(), logger.capacity());

        ImGui::Separator();

        // 2. Split view: Log table (top 75%) and Detail preview pane (bottom 25%)
        float totalHeight = ImGui::GetContentRegionAvail().y;
        float tableHeight =
            (s_selectedLogIndex < logger.count()) ? (totalHeight - 75.0F) : totalHeight;

        ImGui::BeginChild("LogTableRegion", ImVec2(0.0F, tableHeight), false);

        if (ImGui::BeginTable("ConsoleTable", 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 65.0F);
            ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 85.0F);
            ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < logger.count(); ++i) {
                const auto* record = logger.peek(i);
                if (record == nullptr) {
                    continue;
                }

                // Filter by level
                if (selectedLogLevel_ == 1 && record->level < LogLevel::Info) {
                    continue;
                }
                if (selectedLogLevel_ == 2 && record->level < LogLevel::Warning) {
                    continue;
                }
                if (selectedLogLevel_ == 3 && record->level < LogLevel::Error) {
                    continue;
                }

                // Filter by search text
                if (filterBuffer_[0] != '\0') {
                    std::string_view msg(record->message.data(), record->messageLength);
                    if (msg.find(filterBuffer_.data()) == std::string_view::npos) {
                        continue;
                    }
                }

                ImGui::TableNextRow();

                // Column 1: Level badge
                ImGui::TableSetColumnIndex(0);
                ImVec4 badgeColor = ImVec4(0.5F, 0.5F, 0.5F, 1.0F);
                if (record->level == LogLevel::Info) {
                    badgeColor = ImVec4(0.38F, 0.74F, 0.98F, 1.0F);
                } else if (record->level == LogLevel::Warning) {
                    badgeColor = ImVec4(0.96F, 0.62F, 0.04F, 1.0F);
                } else if (record->level >= LogLevel::Error) {
                    badgeColor = ImVec4(0.94F, 0.27F, 0.27F, 1.0F);
                }

                bool isSelected = (s_selectedLogIndex == i);
                char rowId[32];
                std::snprintf(rowId, sizeof(rowId), "[%s]##%zu", toString(record->level).data(), i);
                if (ImGui::Selectable(rowId, isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                    s_selectedLogIndex = i;
                }

                // Column 2: Category
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("System");

                // Column 3: Message
                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(badgeColor, "%.*s", static_cast<int>(record->messageLength),
                                   record->message.data());
            }

            if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0F);
            }

            ImGui::EndTable();
        }
        ImGui::EndChild();

        // 3. Detail Preview Pane
        if (s_selectedLogIndex < logger.count()) {
            const auto* record = logger.peek(s_selectedLogIndex);
            if (record != nullptr) {
                ImGui::Separator();
                ImGui::BeginChild("LogDetailRegion", ImVec2(0.0F, 0.0F), true);
                ImGui::TextColored(ImVec4(0.38F, 0.74F, 0.98F, 1.0F), "Selected Log Details:");
                ImGui::Text("Level: %s | Thread ID: Main", toString(record->level).data());
                ImGui::TextWrapped("%.*s", static_cast<int>(record->messageLength),
                                   record->message.data());
                ImGui::EndChild();
            }
        }
    }
    ImGui::End();
}

} // namespace eng::editor
