/// @file metrics_panel.cpp
/// @brief Performance profiler and engine telemetry panel implementation.

#include "panels/metrics_panel.h"

#include "rhi/rhi_device.h"
#include "world/world.h"

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

        // 1. Frame Timing
        ImGui::Text("FPS: %.1f", static_cast<double>(io.Framerate));
        ImGui::SameLine();
        ImGui::TextDisabled("(Frame Time: %.2f ms)", static_cast<double>(currentFrameMs));

        ImGui::PlotLines("##FrameHistory", frameHistory_.data(),
                         static_cast<int>(frameHistory_.size()), static_cast<int>(historyIndex_),
                         "Frame Time (ms)", 0.0F, 33.3F, ImVec2(-1.0F, 65.0F));

        ImGui::Separator();

        // 2. Editor Rendering Pipeline
        ImGui::TextColored(ImVec4(0.38F, 0.74F, 0.98F, 1.0F), "Editor Render Passes:");
        ImGui::Text("ImGui Draw Calls: %u", ctx.lastDrawCalls);
        ImGui::Text("ImGui Vertices:   %u", ctx.lastVertices);
        ImGui::Text("ImGui Indices:    %u", ctx.lastIndices);
        ImGui::Text("Executed Passes:  %llu", static_cast<unsigned long long>(ctx.executedPasses));

        ImGui::Separator();

        // 3. Subsystem Telemetry
        if (ctx.rhiDevice != nullptr) {
            ImGui::Text("Active GPU: %s", ctx.rhiDevice->getCaps().deviceName.data());
            ImGui::Text("Vulkan API: 1.3 (Dynamic Rendering)");
        } else {
            ImGui::TextDisabled("RHI: Headless NullDevice");
        }

        if (ctx.world != nullptr) {
            flecs::world& ecs = ctx.world->getRawWorld();
            ImGui::Text("Active ECS Entities: %d", ecs.count<flecs::Component>());
        }
    }
    ImGui::End();
}

} // namespace eng::editor
