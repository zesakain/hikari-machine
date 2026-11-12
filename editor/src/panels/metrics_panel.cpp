/// @file metrics_panel.cpp
/// @brief Performance profiler and engine telemetry panel implementation with visual metric cards
/// and live graphs.

#include "panels/metrics_panel.h"

#include "core/time/frame_telemetry.h"
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
        const auto& theme = getActiveThemeColors();

        // Retrieve live telemetry
        const float curFps = ctx.currentFps;
        const float avgFps = ctx.averageFps > 0.0F ? ctx.averageFps : curFps;
        const float minFps = ctx.minFps > 0.0F ? ctx.minFps : curFps;
        const float maxFps = ctx.maxFps > 0.0F ? ctx.maxFps : curFps;

        const float curFrameMs = ctx.lastFrameTimeMs;
        const float avgFrameMs =
            ctx.averageFrameTimeMs > 0.0F ? ctx.averageFrameTimeMs : curFrameMs;
        const float minFrameMs = ctx.minFrameTimeMs > 0.0F ? ctx.minFrameTimeMs : curFrameMs;
        const float maxFrameMs = ctx.maxFrameTimeMs > 0.0F ? ctx.maxFrameTimeMs : curFrameMs;

        const float curCpuMs = ctx.cpuTimeMs;
        const float avgCpuMs = ctx.averageCpuTimeMs > 0.0F ? ctx.averageCpuTimeMs : curCpuMs;

        // 1. Primary Telemetry KPI Cards Row (4 Columns)
        ImGui::Columns(4, nullptr, false);

        // Card 1: Frame Rate (FPS)
        beginCard("KpiFps", 0.0F, 64.0F);
        ImGui::TextDisabled("Frame Rate");
        const Color4f fpsColor = (curFps >= 55.0F)   ? theme.statusSuccess
                                 : (curFps >= 30.0F) ? theme.statusWarning
                                                     : theme.statusError;
        ImGui::TextColored(toImVec4(fpsColor), "%.1f FPS", static_cast<double>(curFps));
        ImGui::TextDisabled("Avg: %.1f (Min: %.0f, Max: %.0f)", static_cast<double>(avgFps),
                            static_cast<double>(minFps), static_cast<double>(maxFps));
        endCard();
        ImGui::NextColumn();

        // Card 2: Total Frame Time
        beginCard("KpiFrameTime", 0.0F, 64.0F);
        ImGui::TextDisabled("Frame Time");
        ImGui::TextColored(toImVec4(theme.textPrimary), "%.2f ms", static_cast<double>(curFrameMs));
        ImGui::TextDisabled("Avg: %.2f ms", static_cast<double>(avgFrameMs));
        endCard();
        ImGui::NextColumn();

        // Card 3: CPU Workload
        beginCard("KpiCpuTime", 0.0F, 64.0F);
        ImGui::TextDisabled("CPU Workload");
        ImGui::TextColored(toImVec4(theme.textPrimary), "%.2f ms", static_cast<double>(curCpuMs));
        ImGui::TextDisabled("Avg: %.2f ms", static_cast<double>(avgCpuMs));
        endCard();
        ImGui::NextColumn();

        // Card 4: GPU Hardware Time
        beginCard("KpiGpuTime", 0.0F, 64.0F);
        ImGui::TextDisabled("GPU Hardware");
        if (ctx.hasGpuTiming) {
            ImGui::TextColored(toImVec4(theme.statusSuccess), "%.2f ms",
                               static_cast<double>(ctx.gpuTimeMs));
            ImGui::TextDisabled("Avg: %.2f ms", static_cast<double>(ctx.averageGpuTimeMs));
        } else {
            ImGui::TextDisabled("GPU: N/A");
            ImGui::TextDisabled("Queries unsupported");
        }
        endCard();
        ImGui::NextColumn();

        ImGui::Columns(1);
        ImGui::Spacing();

        // Secondary Cards: Draw Calls and Entities
        ImGui::Columns(2, nullptr, false);

        beginCard("KpiDrawCalls", 0.0F, 48.0F);
        ImGui::TextDisabled("Draw Calls");
        ImGui::TextColored(toImVec4(theme.textPrimary), "%u calls", ctx.lastDrawCalls);
        endCard();
        ImGui::NextColumn();

        beginCard("KpiEntities", 0.0F, 48.0F);
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

        // 2. Live Performance Graphs
        const float* frameTimeData = nullptr;
        const float* cpuTimeData = nullptr;
        const float* fpsData = nullptr;
        int plotCount = 0;
        int plotOffset = 0;

        if (ctx.telemetry != nullptr) {
            frameTimeData = ctx.telemetry->frameTimeHistory().data();
            cpuTimeData = ctx.telemetry->cpuTimeHistory().data();
            fpsData = ctx.telemetry->fpsHistory().data();
            plotCount = static_cast<int>(ctx.telemetry->sampleCount());
            plotOffset = static_cast<int>(ctx.telemetry->historyIndex());
        } else {
            fallbackHistory_[fallbackIndex_] = curFrameMs;
            fallbackIndex_ = (fallbackIndex_ + 1) % fallbackHistory_.size();
            frameTimeData = fallbackHistory_.data();
            cpuTimeData = fallbackHistory_.data();
            fpsData = fallbackHistory_.data();
            plotCount = static_cast<int>(fallbackHistory_.size());
            plotOffset = static_cast<int>(fallbackIndex_);
        }

        // Plot A: Total Frame Time History (ms)
        char framePlotOverlay[96];
        std::snprintf(framePlotOverlay, sizeof(framePlotOverlay),
                      "Frame Time: %.2f ms | Avg: %.2f ms (Min: %.1f, Max: %.1f)",
                      static_cast<double>(curFrameMs), static_cast<double>(avgFrameMs),
                      static_cast<double>(minFrameMs), static_cast<double>(maxFrameMs));

        const float framePlotMax = std::max(33.3F, maxFrameMs * 1.15F);
        ImGui::TextUnformatted("Frame Time History (ms)");
        ImGui::PlotLines("##FrameHistoryPlot", frameTimeData, plotCount, plotOffset,
                         framePlotOverlay, 0.0F, framePlotMax, ImVec2(-1.0F, 60.0F));

        ImGui::Spacing();

        // Plot B: CPU Workload History (ms)
        char cpuPlotOverlay[96];
        std::snprintf(cpuPlotOverlay, sizeof(cpuPlotOverlay),
                      "CPU Workload: %.2f ms | Avg: %.2f ms", static_cast<double>(curCpuMs),
                      static_cast<double>(avgCpuMs));

        const float cpuPlotMax = std::max(16.6F, avgCpuMs * 2.0F);
        ImGui::TextUnformatted("CPU Workload History (ms)");
        ImGui::PlotLines("##CpuHistoryPlot", cpuTimeData, plotCount, plotOffset, cpuPlotOverlay,
                         0.0F, cpuPlotMax, ImVec2(-1.0F, 60.0F));

        ImGui::Spacing();

        // Plot C: FPS History
        char fpsPlotOverlay[96];
        std::snprintf(fpsPlotOverlay, sizeof(fpsPlotOverlay),
                      "Instant: %.1f FPS | Avg: %.1f FPS (Min: %.0f, Max: %.0f)",
                      static_cast<double>(curFps), static_cast<double>(avgFps),
                      static_cast<double>(minFps), static_cast<double>(maxFps));

        const float fpsPlotMax = std::max(120.0F, maxFps * 1.1F);
        ImGui::TextUnformatted("FPS History");
        ImGui::PlotLines("##FpsHistoryPlot", fpsData, plotCount, plotOffset, fpsPlotOverlay, 0.0F,
                         fpsPlotMax, ImVec2(-1.0F, 60.0F));

        ImGui::Separator();

        // 3. Subsystem Detailed Telemetry Tabs
        if (ImGui::BeginTabBar("TelemetryTabBar")) {
            if (ImGui::BeginTabItem("Renderer")) {
                ImGui::Spacing();
                ImGui::Columns(2, nullptr, false);
                ImGui::SetColumnWidth(0, 180.0F);

                ImGui::TextDisabled("Frame Rate (FPS)");
                ImGui::NextColumn();
                ImGui::Text("%.1f FPS (Avg: %.1f, Min: %.0f, Max: %.0f)",
                            static_cast<double>(curFps), static_cast<double>(avgFps),
                            static_cast<double>(minFps), static_cast<double>(maxFps));
                ImGui::NextColumn();

                ImGui::TextDisabled("Total Frame Duration");
                ImGui::NextColumn();
                ImGui::Text("%.2f ms", static_cast<double>(curFrameMs));
                ImGui::NextColumn();

                ImGui::TextDisabled("CPU Execution Time");
                ImGui::NextColumn();
                ImGui::Text("%.2f ms", static_cast<double>(curCpuMs));
                ImGui::NextColumn();

                ImGui::TextDisabled("GPU Hardware Time");
                ImGui::NextColumn();
                if (ctx.hasGpuTiming) {
                    ImGui::Text("%.2f ms", static_cast<double>(ctx.gpuTimeMs));
                } else {
                    ImGui::TextDisabled("GPU: N/A (Timestamp queries unsupported)");
                }
                ImGui::NextColumn();

                ImGui::TextDisabled("Scene Render Resolution");
                ImGui::NextColumn();
                ImGui::Text("%ux%u", ctx.sceneRenderWidth, ctx.sceneRenderHeight);
                ImGui::NextColumn();

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
                ImGui::SetColumnWidth(0, 180.0F);

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
                ImGui::SetColumnWidth(0, 180.0F);

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

                ImGui::TextDisabled("GPU Timestamp Queries");
                ImGui::NextColumn();
                ImGui::TextDisabled("Disabled / Unsupported (RHI Tier 0 baseline)");
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
