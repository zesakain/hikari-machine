/// @file metrics_panel.cpp
/// @brief Performance profiler and engine telemetry panel implementation with visual metric cards.

#include "panels/metrics_panel.h"

#include "editor/editor_theme.h"
#include "editor/ui_helpers.h"
#include "imgui_utils.h"
#include "rhi/rhi_device.h"
#include "world/world.h"

#include <algorithm>
#include <cstdio>
#include <imgui.h>

namespace eng::editor {

void MetricsPanel::draw(EditorContext& ctx) {
    if (!isOpen_) {
        return;
    }

    if (ImGui::Begin(getDisplayName().data(), &isOpen_)) {
        ImGuiIO& io = ImGui::GetIO();
        float currentFrameMs = 1000.0F / (io.Framerate > 0.0F ? io.Framerate : 60.0F);

        frameHistory_[historyIndex_] = currentFrameMs;
        historyIndex_ = (historyIndex_ + 1) % frameHistory_.size();

        float minMs = 999.0F;
        float maxMs = 0.0F;
        float avgMs = 0.0F;
        for (float v : frameHistory_) {
            minMs = std::min(minMs, v);
            maxMs = std::max(maxMs, v);
            avgMs += v;
        }
        avgMs /= static_cast<float>(frameHistory_.size());

        const auto& theme = getActiveThemeColors();

        // 1. Metric Cards Row
        ImGui::Columns(4, nullptr, false);

        // Card 1: FPS
        beginCard("KpiFps", 0.0F, 56.0F);
        ImGui::TextDisabled("Frame Rate");
        Color4f fpsColor = (io.Framerate >= 55.0F)   ? theme.statusSuccess
                           : (io.Framerate >= 30.0F) ? theme.statusWarning
                                                     : theme.statusError;
        ImGui::TextColored(toImVec4(fpsColor), "%.1f FPS", static_cast<double>(io.Framerate));
        endCard();
        ImGui::NextColumn();

        // Card 2: Frame Time
        beginCard("KpiFrameTime", 0.0F, 56.0F);
        ImGui::TextDisabled("Frame Time");
        ImGui::TextColored(toImVec4(theme.textPrimary), "%.2f ms",
                           static_cast<double>(currentFrameMs));
        endCard();
        ImGui::NextColumn();

        // Card 3: Draw Calls
        beginCard("KpiDrawCalls", 0.0F, 56.0F);
        ImGui::TextDisabled("Draw Calls");
        ImGui::TextColored(toImVec4(theme.textPrimary), "%u calls", ctx.lastDrawCalls);
        endCard();
        ImGui::NextColumn();

        // Card 4: Entities
        beginCard("KpiEntities", 0.0F, 56.0F);
        ImGui::TextDisabled("ECS Entities");
        if (ctx.world != nullptr) {
            flecs::world& ecs = ctx.world->getRawWorld();
            ImGui::TextColored(toImVec4(theme.textPrimary), "%d entities",
                               ecs.count<flecs::Component>());
        } else {
            ImGui::TextDisabled("0");
        }
        endCard();
        ImGui::NextColumn();

        ImGui::Columns(1);
        ImGui::Separator();

        // 2. Frame Time History Graph
        char plotOverlay[64];
        std::snprintf(plotOverlay, sizeof(plotOverlay), "Avg: %.2f ms (Min: %.1f, Max: %.1f)",
                      static_cast<double>(avgMs), static_cast<double>(minMs),
                      static_cast<double>(maxMs));

        ImGui::PlotLines("##FrameHistoryPlot", frameHistory_.data(),
                         static_cast<int>(frameHistory_.size()), static_cast<int>(historyIndex_),
                         plotOverlay, 0.0F, 33.3F, ImVec2(-1.0F, 65.0F));

        ImGui::Separator();

        // 3. Subsystem Detailed Telemetry Tabs
        if (ImGui::BeginTabBar("TelemetryTabBar")) {
            if (ImGui::BeginTabItem("Renderer")) {
                ImGui::Spacing();
                ImGui::Columns(2, nullptr, false);
                ImGui::SetColumnWidth(0, 150.0F);

                ImGui::TextDisabled("ImGui Draw Calls");
                ImGui::NextColumn();
                ImGui::Text("%u", ctx.lastDrawCalls);
                ImGui::NextColumn();

                ImGui::TextDisabled("ImGui Vertices");
                ImGui::NextColumn();
                ImGui::Text("%u", ctx.lastVertices);
                ImGui::NextColumn();

                ImGui::TextDisabled("ImGui Indices");
                ImGui::NextColumn();
                ImGui::Text("%u", ctx.lastIndices);
                ImGui::NextColumn();

                ImGui::TextDisabled("Executed Passes");
                ImGui::NextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(ctx.executedPasses));
                ImGui::NextColumn();

                ImGui::Columns(1);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Simulation")) {
                ImGui::Spacing();
                ImGui::Columns(2, nullptr, false);
                ImGui::SetColumnWidth(0, 150.0F);

                ImGui::TextDisabled("Fixed Tick Delta");
                ImGui::NextColumn();
                ImGui::Text("16.6 ms (60 Hz)");
                ImGui::NextColumn();

                ImGui::TextDisabled("Continuous Simulation");
                ImGui::NextColumn();
                ImGui::TextColored(ImVec4(0.06F, 0.73F, 0.51F, 1.0F), "Active");
                ImGui::NextColumn();

                ImGui::TextDisabled("Worker Threads");
                ImGui::NextColumn();
                ImGui::Text("2 Threads (Lock-free JobSystem)");
                ImGui::NextColumn();

                ImGui::Columns(1);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Hardware & RHI")) {
                ImGui::Spacing();
                ImGui::Columns(2, nullptr, false);
                ImGui::SetColumnWidth(0, 150.0F);

                ImGui::TextDisabled("Active Device");
                ImGui::NextColumn();
                if (ctx.rhiDevice != nullptr) {
                    ImGui::Text("%s", ctx.rhiDevice->getCaps().deviceName.data());
                } else {
                    ImGui::TextDisabled("Null RHI");
                }
                ImGui::NextColumn();

                ImGui::TextDisabled("Graphics API");
                ImGui::NextColumn();
                ImGui::Text("Vulkan 1.3 (Dynamic Rendering, Synchronization2)");
                ImGui::NextColumn();

                ImGui::TextDisabled("Surface Format");
                ImGui::NextColumn();
                ImGui::Text("VK_FORMAT_B8G8R8A8_UNORM (sRGB)");
                ImGui::NextColumn();

                ImGui::Columns(1);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

} // namespace eng::editor
