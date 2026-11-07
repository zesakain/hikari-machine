/// @file content_browser_panel.cpp
/// @brief Content browser panel implementation.

#include "panels/content_browser_panel.h"

#include <imgui.h>

namespace eng::editor {

void ContentBrowserPanel::draw([[maybe_unused]] EditorContext& ctx) {
    if (!isOpen_) {
        return;
    }

    if (ImGui::Begin(getDisplayName().data(), &isOpen_)) {
        ImGui::TextColored(ImVec4(0.38F, 0.74F, 0.98F, 1.0F), "Project Directory Hierarchy:");
        ImGui::Separator();

        // 1. Assets
        if (ImGui::TreeNodeEx("assets/ (Source Assets)", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::TreeNode("models/ (3D Meshes & Scenes)")) {
                ImGui::BulletText("player.gltf (glTF 2.0 binary mesh)");
                ImGui::BulletText("arena_level.gltf (Static level geometry)");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("textures/ (GPU Textures)")) {
                ImGui::BulletText("albedo_grid.png (RGBA8 2048x2048)");
                ImGui::BulletText("normal_noise.png (RG16 normal map)");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("audio/ (Sound Effects & Music)")) {
                ImGui::BulletText("skill_cast.wav (Stereo 48kHz)");
                ImGui::BulletText("hit_impact.wav (Mono 48kHz)");
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }

        // 2. Data
        if (ImGui::TreeNodeEx("data/ (TOML & JSON Tables)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BulletText("skills.toml (Skill balance & hitbox parameters)");
            ImGui::BulletText("characters.toml (Character stats & speed)");
            ImGui::TreePop();
        }

        // 3. Game Scripts & Logic
        if (ImGui::TreeNodeEx("game/ (Lua Scripts & Gameplay Modes)",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BulletText("scripts/main.lua (Gameplay scripting hooks)");
            ImGui::BulletText("modes/exploration_mode.cpp (State A implementation)");
            ImGui::BulletText("modes/skill_mode.cpp (State B implementation)");
            ImGui::TreePop();
        }
    }
    ImGui::End();
}

} // namespace eng::editor
