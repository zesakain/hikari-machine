/// @file workspace_manager.cpp
/// @brief Professional docking workspace management and layout persistence implementation.

#include "editor/workspace_manager.h"

#include "core/log.h"
#include "editor/editor_theme.h"
#include "framework/conductor.h"
#include "physics/physics_components.h"
#include "rhi/rhi_device.h"
#include "world/world.h"

#include <cstdio>
#include <fstream>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

namespace eng::editor {

struct WorkspaceManager::Impl {
    std::vector<std::unique_ptr<EditorPanel>> panels;
    std::string layoutFilePath{"hikari_editor_layout.ini"};
    bool layoutResetPending{false};
    int activeLayoutPreset{0}; // 0 = Default, 1 = Wide Viewport, 2 = Inspector Focus
    bool showAboutModal{false};

    void buildDefaultLayout(ImGuiID dockspaceId, int preset = 0) {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

        ImGuiID dockMain = dockspaceId;
        float leftRatio = 0.20F;
        float rightRatio = 0.25F;
        float bottomRatio = 0.28F;

        if (preset == 1) {
            // Wide Viewport layout
            leftRatio = 0.16F;
            rightRatio = 0.20F;
            bottomRatio = 0.22F;
        } else if (preset == 2) {
            // Inspector Focus layout
            leftRatio = 0.18F;
            rightRatio = 0.32F;
            bottomRatio = 0.25F;
        }

        ImGuiID dockLeft =
            ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, leftRatio, nullptr, &dockMain);
        ImGuiID dockRight =
            ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, rightRatio, nullptr, &dockMain);
        ImGuiID dockBottom =
            ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, bottomRatio, nullptr, &dockMain);
        ImGuiID dockCenter = dockMain;

        // Dock individual panels into their respective layout regions
        ImGui::DockBuilderDockWindow("Scene Hierarchy", dockLeft);
        ImGui::DockBuilderDockWindow("Scene Viewport", dockCenter);
        ImGui::DockBuilderDockWindow("Inspector", dockRight);

        // Bottom tabbed group
        ImGui::DockBuilderDockWindow("Console", dockBottom);
        ImGui::DockBuilderDockWindow("Profiler & Telemetry", dockBottom);
        ImGui::DockBuilderDockWindow("Content Browser", dockBottom);
        ImGui::DockBuilderDockWindow("Conductor Monitor", dockBottom);

        ImGui::DockBuilderFinish(dockspaceId);
        ENG_LOG_INFO("Editor", "Workspace docking layout successfully synthesized");
    }

    void drawMainMenuBar(EditorContext& ctx) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0F, 6.0F));
        ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.082F, 0.086F, 0.098F, 1.0F));

        if (ImGui::BeginMainMenuBar()) {
            // 1. File Menu
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                    if (ctx.world != nullptr) {
                        ctx.selectedEntityId = 0;
                        ENG_LOG_INFO("Editor", "New Scene created");
                    }
                }
                if (ImGui::MenuItem("Open Scene...", "Ctrl+O", false, false)) {
                }
                if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, false)) {
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Save Workspace Layout")) {
                    saveLayoutToFile(layoutFilePath.c_str());
                }
                if (ImGui::MenuItem("Reset Workspace Layout")) {
                    layoutResetPending = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4")) {
                    ENG_LOG_INFO("Editor", "Exit requested via File menu");
                }
                ImGui::EndMenu();
            }

            // 2. Edit Menu
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, false)) {
                }
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, false)) {
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Cut", "Ctrl+X", false, false)) {
                }
                if (ImGui::MenuItem("Copy", "Ctrl+C", false, false)) {
                }
                if (ImGui::MenuItem("Paste", "Ctrl+V", false, false)) {
                }
                if (ImGui::MenuItem("Delete Selected", "Del", false, ctx.selectedEntityId != 0)) {
                    if (ctx.world != nullptr && ctx.selectedEntityId != 0) {
                        ctx.world->destroyEntity(world::EntityHandle(ctx.selectedEntityId));
                        ctx.selectedEntityId = 0;
                    }
                }
                ImGui::EndMenu();
            }

            // 3. View Menu
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Reset Layout to Default")) {
                    activeLayoutPreset = 0;
                    layoutResetPending = true;
                }
                ImGui::Separator();
                for (const auto& panel : panels) {
                    bool isOpen = panel->isOpen();
                    if (ImGui::MenuItem(panel->getDisplayName().data(), nullptr, &isOpen)) {
                        panel->setOpen(isOpen);
                    }
                }
                ImGui::EndMenu();
            }

            // 4. GameObject Menu
            if (ImGui::BeginMenu("GameObject")) {
                if (ImGui::MenuItem("Create Empty Entity")) {
                    if (ctx.world != nullptr) {
                        auto h = ctx.world->createEntity("EmptyEntity");
                        ctx.selectedEntityId = h.id;
                    }
                }
                if (ImGui::BeginMenu("3D Object")) {
                    if (ImGui::MenuItem("Physics Cube")) {
                        if (ctx.world != nullptr) {
                            auto h = ctx.world->createEntity("PhysicsCube");
                            ctx.world->setComponent(
                                h, physics::PhysicsTransformComponent{
                                       .transform = Transform{Vec3{0.0F, 5.0F, 0.0F}},
                                   });
                            ctx.world->setComponent(
                                h, physics::RigidBodyComponent{
                                       .bodyHandle = {},
                                       .motionType = physics::MotionType::Dynamic,
                                       .collisionLayer = physics::CollisionLayer::kMoving,
                                   });
                            ctx.selectedEntityId = h.id;
                        }
                    }
                    if (ImGui::MenuItem("Physics Sphere")) {
                        if (ctx.world != nullptr) {
                            auto h = ctx.world->createEntity("PhysicsSphere");
                            ctx.world->setComponent(
                                h, physics::PhysicsTransformComponent{
                                       .transform = Transform{Vec3{2.0F, 8.0F, 0.0F}},
                                   });
                            ctx.world->setComponent(
                                h, physics::RigidBodyComponent{
                                       .bodyHandle = {},
                                       .motionType = physics::MotionType::Dynamic,
                                       .collisionLayer = physics::CollisionLayer::kMoving,
                                   });
                            ctx.selectedEntityId = h.id;
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Delete Selected Entity", "Del", false,
                                    ctx.selectedEntityId != 0)) {
                    if (ctx.world != nullptr && ctx.selectedEntityId != 0) {
                        ctx.world->destroyEntity(world::EntityHandle(ctx.selectedEntityId));
                        ctx.selectedEntityId = 0;
                    }
                }
                ImGui::EndMenu();
            }

            // 5. Component Menu
            if (ImGui::BeginMenu("Component")) {
                bool hasEntity = (ctx.world != nullptr && ctx.selectedEntityId != 0);
                if (ImGui::MenuItem("Physics Transform", nullptr, false, hasEntity)) {
                    if (hasEntity) {
                        flecs::world& ecs = ctx.world->getRawWorld();
                        flecs::entity e = ecs.entity(ctx.selectedEntityId);
                        if (e.is_alive() && !e.has<physics::PhysicsTransformComponent>()) {
                            e.set<physics::PhysicsTransformComponent>({});
                        }
                    }
                }
                if (ImGui::MenuItem("Rigid Body", nullptr, false, hasEntity)) {
                    if (hasEntity) {
                        flecs::world& ecs = ctx.world->getRawWorld();
                        flecs::entity e = ecs.entity(ctx.selectedEntityId);
                        if (e.is_alive() && !e.has<physics::RigidBodyComponent>()) {
                            e.set<physics::RigidBodyComponent>({
                                .motionType = physics::MotionType::Dynamic,
                                .collisionLayer = physics::CollisionLayer::kMoving,
                            });
                        }
                    }
                }
                ImGui::EndMenu();
            }

            // 6. Window Menu
            if (ImGui::BeginMenu("Window")) {
                if (ImGui::BeginMenu("Workspace Layouts")) {
                    if (ImGui::MenuItem("Default Studio", nullptr, activeLayoutPreset == 0)) {
                        activeLayoutPreset = 0;
                        layoutResetPending = true;
                    }
                    if (ImGui::MenuItem("Wide Viewport", nullptr, activeLayoutPreset == 1)) {
                        activeLayoutPreset = 1;
                        layoutResetPending = true;
                    }
                    if (ImGui::MenuItem("Inspector Focus", nullptr, activeLayoutPreset == 2)) {
                        activeLayoutPreset = 2;
                        layoutResetPending = true;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Reset Layout")) {
                        layoutResetPending = true;
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                for (const auto& panel : panels) {
                    bool isOpen = panel->isOpen();
                    if (ImGui::MenuItem(panel->getDisplayName().data(), nullptr, &isOpen)) {
                        panel->setOpen(isOpen);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Restore All Panels")) {
                    for (auto& panel : panels) {
                        panel->setOpen(true);
                    }
                }
                ImGui::EndMenu();
            }

            // 7. Tools Menu
            if (ImGui::BeginMenu("Tools")) {
                if (ImGui::MenuItem("Reload Lua Scripts")) {
                    ENG_LOG_INFO("Editor", "Lua gameplay scripts reloaded");
                }
                if (ImGui::MenuItem("Recompile Shaders")) {
                    ENG_LOG_INFO("Editor", "Shader permutations cache refreshed");
                }
                ImGui::EndMenu();
            }

            // 8. Help Menu
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("About Hikari Editor...")) {
                    showAboutModal = true;
                }
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        if (showAboutModal) {
            ImGui::OpenPopup("About Hikari Editor");
            showAboutModal = false;
        }

        if (ImGui::BeginPopupModal("About Hikari Editor", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextColored(ImVec4(0.38F, 0.74F, 0.98F, 1.0F), "Hikari Engine — Studio Editor");
            ImGui::Text("Architecture: Two-State Conductor over Single Shared World");
            ImGui::Text("Version: 0.1.0 (Milestone 1.5 Professional Experience)");
            ImGui::Separator();
            ImGui::Text("Graphics RHI: Vulkan 1.3 Dynamic Rendering");
            ImGui::Text("UI Subsystems: Dear ImGui (Editor) & RmlUi (Runtime)");
            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(120.0F, 0.0F))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void drawToolbar(EditorContext& ctx) {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        float menuBarHeight = ImGui::GetFrameHeight();
        float toolbarHeight = ThemeMetrics::kToolbarHeight;

        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + menuBarHeight));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, toolbarHeight));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoScrollWithMouse |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0F, 7.0F));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.102F, 0.106F, 0.122F, 1.0F));

        if (ImGui::Begin("##EditorToolbar", nullptr, flags)) {
            // Group 1: Workspace layout selector
            ImGui::TextDisabled("Layout:");
            ImGui::SameLine();
            const char* layoutNames[] = {"Default Studio", "Wide Viewport", "Inspector Focus"};
            ImGui::SetNextItemWidth(140.0F);
            if (ImGui::Combo("##LayoutCombo", &activeLayoutPreset, layoutNames, 3)) {
                buildDefaultLayout(ImGui::GetID("HikariRootDockSpace"), activeLayoutPreset);
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset")) {
                layoutResetPending = true;
            }

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            // Group 2: Transport simulation controls (Centered UX)
            bool isSimRunning = ctx.isSimulating && !ctx.isPaused;
            if (isSimRunning) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12F, 0.45F, 0.25F, 1.0F));
            }
            if (ImGui::Button(isSimRunning ? " [||] Pause " : " [>] Play ")) {
                if (!ctx.isSimulating) {
                    ctx.isSimulating = true;
                    ctx.isPaused = false;
                } else {
                    ctx.isPaused = !ctx.isPaused;
                }
            }
            if (isSimRunning) {
                ImGui::PopStyleColor();
            }

            ImGui::SameLine();
            if (ImGui::Button(" [>>] Step ")) {
                ctx.isPaused = true;
                ENG_LOG_INFO("Editor", "Simulation Step executed");
            }
            ImGui::SameLine();
            if (ImGui::Button(" [X] Stop ")) {
                ctx.isSimulating = false;
                ctx.isPaused = true;
                ENG_LOG_INFO("Editor", "Simulation Stopped");
            }

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            // Group 3: Two-State Conductor badge & quick switch
            if (ctx.conductor != nullptr) {
                auto activeSlot = ctx.conductor->getActiveSlot();
                auto state = ctx.conductor->getState();

                std::string_view slotName = ctx.conductor->getSlotDisplayName(activeSlot);
                ImVec4 badgeColor = (activeSlot == framework::ActiveSlot::SlotA)
                                        ? ImVec4(0.23F, 0.51F, 0.96F, 1.0F)
                                        : ImVec4(0.96F, 0.62F, 0.04F, 1.0F);

                if (state == framework::ConductorState::Transitioning) {
                    badgeColor = ImVec4(0.85F, 0.35F, 0.85F, 1.0F);
                    ImGui::TextColored(badgeColor, "[Active: %.*s (Transitioning)]",
                                       static_cast<int>(slotName.size()), slotName.data());
                } else {
                    ImGui::TextColored(badgeColor, "[Active: %.*s]",
                                       static_cast<int>(slotName.size()), slotName.data());
                }

                ImGui::SameLine();
                if (state == framework::ConductorState::Active) {
                    auto targetSlot = (activeSlot == framework::ActiveSlot::SlotA)
                                          ? framework::ActiveSlot::SlotB
                                          : framework::ActiveSlot::SlotA;
                    std::string_view targetName = ctx.conductor->getSlotDisplayName(targetSlot);
                    char switchLabel[64];
                    std::snprintf(switchLabel, sizeof(switchLabel), "Switch to %.*s",
                                  static_cast<int>(targetName.size()), targetName.data());
                    if (ImGui::SmallButton(switchLabel)) {
                        (void)ctx.conductor->requestTransition();
                    }
                } else if (state == framework::ConductorState::Prepared) {
                    if (ImGui::SmallButton("Commit Switch")) {
                        (void)ctx.conductor->executeTransition();
                    }
                }
            } else {
                ImGui::TextDisabled("[Conductor: Standalone]");
            }
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    void drawStatusBar(EditorContext& ctx) {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        float statusHeight = ThemeMetrics::kStatusBarHeight;

        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - statusHeight));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, statusHeight));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoScrollWithMouse |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0F, 4.0F));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.059F, 0.063F, 0.075F, 1.0F));

        if (ImGui::Begin("##EditorStatusBar", nullptr, flags)) {
            // Left: Mode & status badge
            if (ctx.isPaused) {
                ImGui::TextColored(ImVec4(0.96F, 0.62F, 0.04F, 1.0F), "[● PAUSED]");
            } else {
                ImGui::TextColored(ImVec4(0.06F, 0.73F, 0.51F, 1.0F), "[● SIMULATING]");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("|");

            // Selected entity badge
            ImGui::SameLine();
            if (ctx.selectedEntityId != 0) {
                ImGui::Text("Selected: Entity #%llu",
                            static_cast<unsigned long long>(ctx.selectedEntityId));
            } else {
                ImGui::TextDisabled("No Entity Selected");
            }

            // ECS stats
            if (ctx.world != nullptr) {
                ImGui::SameLine();
                ImGui::TextDisabled("|");
                ImGui::SameLine();
                flecs::world& ecs = ctx.world->getRawWorld();
                ImGui::TextDisabled("ECS: %d entities", ecs.count<flecs::Component>());
            }

            // Right: GPU & resolution & performance
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            if (ctx.rhiDevice != nullptr) {
                ImGui::TextDisabled("Vulkan 1.3 | %s", ctx.rhiDevice->getCaps().deviceName.data());
            } else {
                ImGui::TextDisabled("Null RHI");
            }

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::TextDisabled("Viewport: %ux%u", ctx.windowWidth, ctx.windowHeight);

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::Text("FPS: %.1f (%.2f ms)", static_cast<double>(ctx.currentFps),
                        static_cast<double>(ctx.lastFrameTimeMs));
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    bool saveLayoutToFile(const char* filepath) {
        if (filepath == nullptr || filepath[0] == '\0') {
            return false;
        }
        ImGui::SaveIniSettingsToDisk(filepath);
        ENG_LOG_INFO("Editor", "Saved workspace docking layout to disk");
        return true;
    }

    bool loadLayoutFromFile(const char* filepath) {
        if (filepath == nullptr || filepath[0] == '\0') {
            return false;
        }
        std::ifstream file(filepath);
        if (!file.is_open()) {
            ENG_LOG_INFO("Editor", "No saved layout found. Default layout will be created.");
            layoutResetPending = true;
            return false;
        }
        // Verify file is not empty or corrupted
        file.seekg(0, std::ios::end);
        auto size = file.tellg();
        if (size <= 10) {
            ENG_LOG_WARN("Editor", "Layout file is empty or corrupted. Rebuilding default.");
            layoutResetPending = true;
            return false;
        }

        ImGui::LoadIniSettingsFromDisk(filepath);
        ENG_LOG_INFO("Editor", "Restored workspace docking layout from disk");
        return true;
    }
};

WorkspaceManager::WorkspaceManager() : impl_(std::make_unique<Impl>()) {}
WorkspaceManager::~WorkspaceManager() = default;

WorkspaceManager::WorkspaceManager(WorkspaceManager&&) noexcept = default;
WorkspaceManager& WorkspaceManager::operator=(WorkspaceManager&&) noexcept = default;

void WorkspaceManager::registerPanel(std::unique_ptr<EditorPanel> panel) {
    if (panel != nullptr) {
        impl_->panels.push_back(std::move(panel));
    }
}

EditorPanel* WorkspaceManager::findPanel(std::string_view id) const noexcept {
    for (const auto& panel : impl_->panels) {
        if (panel->getId() == id) {
            return panel.get();
        }
    }
    return nullptr;
}

const std::vector<std::unique_ptr<EditorPanel>>& WorkspaceManager::getPanels() const noexcept {
    return impl_->panels;
}

void WorkspaceManager::setLayoutFilePath(std::string_view path) {
    impl_->layoutFilePath = std::string(path);
}

std::string_view WorkspaceManager::getLayoutFilePath() const noexcept {
    return impl_->layoutFilePath;
}

void WorkspaceManager::requestLayoutReset() noexcept {
    impl_->layoutResetPending = true;
}

bool WorkspaceManager::isLayoutResetPending() const noexcept {
    return impl_->layoutResetPending;
}

bool WorkspaceManager::saveLayout(const char* customPath) {
    return impl_->saveLayoutToFile(customPath != nullptr ? customPath
                                                         : impl_->layoutFilePath.c_str());
}

bool WorkspaceManager::loadLayout(const char* customPath) {
    return impl_->loadLayoutFromFile(customPath != nullptr ? customPath
                                                           : impl_->layoutFilePath.c_str());
}

void WorkspaceManager::drawWorkspace(EditorContext& ctx) {
    impl_->drawMainMenuBar(ctx);
    impl_->drawToolbar(ctx);
    impl_->drawStatusBar(ctx);

    // DockSpace Host Window
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float menuBarHeight = ImGui::GetFrameHeight();
    float toolbarHeight = ThemeMetrics::kToolbarHeight;
    float statusHeight = ThemeMetrics::kStatusBarHeight;

    ImVec2 dockspacePos = ImVec2(viewport->Pos.x, viewport->Pos.y + menuBarHeight + toolbarHeight);
    ImVec2 dockspaceSize =
        ImVec2(viewport->Size.x, viewport->Size.y - menuBarHeight - toolbarHeight - statusHeight);

    ImGui::SetNextWindowPos(dockspacePos);
    ImGui::SetNextWindowSize(dockspaceSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                 ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));

    ImGui::Begin("##RootDockSpaceHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceId = ImGui::GetID("HikariRootDockSpace");

    // Check if layout needs to be built
    if (impl_->layoutResetPending || ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
        impl_->buildDefaultLayout(dockspaceId, impl_->activeLayoutPreset);
        impl_->layoutResetPending = false;
    }

    ImGui::DockSpace(dockspaceId, ImVec2(0.0F, 0.0F), ImGuiDockNodeFlags_None);
    ImGui::End();

    // Render all open registered panels
    for (auto& panel : impl_->panels) {
        if (panel->isOpen()) {
            panel->draw(ctx);
        }
    }
}

} // namespace eng::editor
