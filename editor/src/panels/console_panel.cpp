/// @file console_panel.cpp
/// @brief Structured log console panel implementation.

#include "panels/console_panel.h"

#include "core/log.h"

#include <cstring>
#include <imgui.h>

namespace eng::editor {

void ConsolePanel::draw([[maybe_unused]] EditorContext& ctx) {
    if (!isOpen_) {
        return;
    }

    if (ImGui::Begin(getDisplayName().data(), &isOpen_)) {
        auto& logger = Logger::defaultLogger();

        // Control header
        if (ImGui::Button("Clear")) {
            logger.clear();
        }
        ImGui::SameLine();

        const char* levelItems[] = {"All Levels", "Info+", "Warning+", "Error Only"};
        ImGui::SetNextItemWidth(120.0F);
        ImGui::Combo("##LogLevelCombo", &selectedLogLevel_, levelItems, 4);

        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0F);
        ImGui::InputTextWithHint("##LogSearch", "Filter logs...", filterBuffer_.data(),
                                 filterBuffer_.size());

        ImGui::SameLine();
        ImGui::Checkbox("Auto-Scroll", &autoScroll_);

        ImGui::SameLine();
        ImGui::TextDisabled("| %zu / %zu entries", logger.count(), logger.capacity());

        ImGui::Separator();

        // Scrolling log region
        ImGui::BeginChild("LogScrollRegion", ImVec2(0.0F, 0.0F), false,
                          ImGuiWindowFlags_HorizontalScrollbar);

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

            // Filter by text search
            if (filterBuffer_[0] != '\0') {
                std::string_view msg(record->message.data(), record->messageLength);
                if (msg.find(filterBuffer_.data()) == std::string_view::npos) {
                    continue;
                }
            }

            // Color-code by level
            ImVec4 levelColor = ImVec4(0.85F, 0.88F, 0.92F, 1.0F);
            if (record->level == LogLevel::Info) {
                levelColor = ImVec4(0.38F, 0.74F, 0.98F, 1.0F);
            } else if (record->level == LogLevel::Warning) {
                levelColor = ImVec4(0.96F, 0.62F, 0.04F, 1.0F);
            } else if (record->level >= LogLevel::Error) {
                levelColor = ImVec4(0.94F, 0.27F, 0.27F, 1.0F);
            }

            ImGui::TextColored(levelColor, "[%s] %.*s", toString(record->level).data(),
                               static_cast<int>(record->messageLength), record->message.data());
        }

        if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0F);
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace eng::editor
