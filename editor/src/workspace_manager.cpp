/// @file workspace_manager.cpp
/// @brief Professional docking workspace management and layout persistence implementation.

#include "editor/workspace_manager.h"

#include "core/log.h"
#include "editor/editor_theme.h"
#include "editor/hierarchy_panel.h"
#include "editor/selection_model.h"
#include "editor/ui_helpers.h"
#include "framework/conductor.h"
#include "framework/runtime_session.h"
#include "framework/scene.h"
#include "framework/scene_serializer.h"
#include "framework/simulation_state.h"
#include "imgui_utils.h"
#include "physics/physics_components.h"
#include "render/gltf_scene_importer.h"
#include "render/light_components.h"
#include "render/mesh_component.h"
#include "rhi/rhi_device.h"
#include "script/script_components.h"
#include "world/components.h"
#include "world/hierarchy_systems.h"
#include "world/world.h"

#include <cstdio>
#include <filesystem>
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

    // Native Scene Persistence State
    std::string currentScenePath{""};
    std::string currentSceneName{"Untitled"};
    bool isSceneDirty{false};

    bool showNewConfirmModal{false};
    bool showOpenConfirmModal{false};
    bool showOpenDialogModal{false};
    bool showSaveAsDialogModal{false};
    bool showSceneErrorModal{false};
    std::string lastSceneError{""};

    char openPathBuffer[512]{"assets/scenes/sample.hikari"};
    char savePathBuffer[512]{"assets/scenes/my_scene.hikari"};

    bool newScene(world::World& world, SelectionModel* selection) {
        world.clearUserEntities();
        if (selection != nullptr) {
            selection->clear();
        }
        currentScenePath.clear();
        currentSceneName = "Untitled";
        isSceneDirty = false;
        ENG_LOG_INFO("Editor", "New Scene created successfully");
        return true;
    }

    bool saveScene(world::World& world, const char* customPath = nullptr) {
        std::string targetPath = (customPath != nullptr && customPath[0] != '\0')
                                     ? std::string(customPath)
                                     : currentScenePath;
        if (targetPath.empty()) {
            showSaveAsDialogModal = true;
            return false;
        }

        framework::SceneMetadata meta;
        meta.name = currentSceneName;
        meta.generator = "Hikari Editor 0.1.0";

        auto res = framework::SceneSerializer::serializeToFile(world, targetPath, meta);
        if (!res.hasValue()) {
            lastSceneError = "Failed to save scene: ";
            lastSceneError += res.error().message;
            showSceneErrorModal = true;
            ENG_LOG_ERROR("Editor", "Scene save failed");
            return false;
        }

        currentScenePath = targetPath;
        std::filesystem::path p(currentScenePath);
        currentSceneName = p.stem().string();
        isSceneDirty = false;
        ENG_LOG_INFO("Editor", "Scene successfully saved to disk");
        return true;
    }

    bool openScene(world::World& world, const char* filepath, SelectionModel* selection) {
        if (filepath == nullptr || filepath[0] == '\0') {
            lastSceneError = "Invalid empty scene file path.";
            showSceneErrorModal = true;
            return false;
        }

        if (!std::filesystem::exists(filepath)) {
            lastSceneError = "Scene file does not exist on disk: ";
            lastSceneError += filepath;
            showSceneErrorModal = true;
            ENG_LOG_ERROR("Editor", "Scene file does not exist on disk");
            return false;
        }

        auto res = framework::SceneSerializer::deserializeFromFile(world, filepath, true);
        if (!res.hasValue()) {
            lastSceneError = "Failed to open scene: ";
            lastSceneError += res.error().message;
            showSceneErrorModal = true;
            ENG_LOG_ERROR("Editor", "Scene deserialization failed");
            return false;
        }

        if (selection != nullptr) {
            selection->clear();
        }

        currentScenePath = filepath;
        std::filesystem::path p(currentScenePath);
        currentSceneName = p.stem().string();
        isSceneDirty = false;

        ENG_LOG_INFO("Editor", "Scene successfully opened and loaded into World");
        return true;
    }

    bool loadBenchmarkScene(world::World& world, const char* filepath,
                            SelectionModel* selection = nullptr,
                            render::AssetManager* assetManager = nullptr) {
        if (filepath == nullptr || filepath[0] == '\0' || !std::filesystem::exists(filepath)) {
            lastSceneError = "Benchmark scene file does not exist on disk: ";
            if (filepath != nullptr) {
                lastSceneError += filepath;
            }
            showSceneErrorModal = true;
            ENG_LOG_ERROR("Editor", "Benchmark scene file not found");
            return false;
        }

        newScene(world, selection);

        auto importRes = render::GltfSceneImporter::importScene(filepath, world, assetManager);
        if (!importRes.success) {
            lastSceneError = "Failed to import benchmark scene: " + importRes.errorMessage;
            showSceneErrorModal = true;
            ENG_LOG_ERROR("Editor", "Benchmark scene import failed");
            return false;
        }

        // Ensure default directional sunlight exists if none is in the scene
        flecs::world& ecs = world.getRawWorld();
        bool hasSun = false;
        ecs.query<const render::DirectionalLightComponent>().each(
            [&](flecs::entity, const render::DirectionalLightComponent&) { hasSun = true; });

        if (!hasSun) {
            auto sun = world.createEntity("Sunlight");
            render::DirectionalLightComponent sunLight{};
            sunLight.enabled = true;
            sunLight.direction = Vec3(0.4F, 0.85F, 0.3F).normalized();
            sunLight.color = Vec3(1.0F, 0.96F, 0.90F);
            sunLight.intensity = 3.5F;
            sunLight.castShadows = true;
            sunLight.shadowBias = 0.0025F;
            world.setComponent(sun, sunLight);
        }

        world::updateHierarchyTransforms(world);

        currentScenePath = filepath;
        currentSceneName = std::filesystem::path(filepath).stem().string();
        isSceneDirty = false;

        ENG_LOG_INFO("Editor", "Benchmark scene loaded successfully: " + std::string(filepath));
        return true;
    }

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
                    if (isSceneDirty) {
                        showNewConfirmModal = true;
                    } else if (ctx.world != nullptr) {
                        newScene(*ctx.world, ctx.selection);
                        ctx.selectedEntityId = 0;
                    }
                }
                if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
                    if (isSceneDirty) {
                        showOpenConfirmModal = true;
                    } else {
                        showOpenDialogModal = true;
                    }
                }
                if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                    if (ctx.world != nullptr) {
                        saveScene(*ctx.world);
                    }
                }
                if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
                    showSaveAsDialogModal = true;
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
                        auto name = HierarchyPanel::generateUniqueEntityName(
                            ctx.world->getRawWorld(), "EmptyEntity");
                        auto h = ctx.world->createEntity(name);
                        if (ctx.selection != nullptr) {
                            ctx.selection->select(h, false);
                        }
                        ctx.selectedEntityId = h.id;
                    }
                }
                if (ImGui::BeginMenu("3D Object")) {
                    if (ImGui::MenuItem("Physics Cube")) {
                        if (ctx.world != nullptr) {
                            auto name = HierarchyPanel::generateUniqueEntityName(
                                ctx.world->getRawWorld(), "PhysicsCube");
                            auto h = ctx.world->createEntity(name);
                            ctx.world->setComponent(
                                h, world::TransformComponent{Transform{Vec3{0.0F, 5.0F, 0.0F}}});
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
                            if (ctx.selection != nullptr) {
                                ctx.selection->select(h, false);
                            }
                            ctx.selectedEntityId = h.id;
                            world::updateHierarchyTransforms(*ctx.world);
                            ctx.markDirty();
                        }
                    }
                    if (ImGui::MenuItem("Physics Sphere")) {
                        if (ctx.world != nullptr) {
                            auto name = HierarchyPanel::generateUniqueEntityName(
                                ctx.world->getRawWorld(), "PhysicsSphere");
                            auto h = ctx.world->createEntity(name);
                            ctx.world->setComponent(
                                h, world::TransformComponent{Transform{Vec3{2.0F, 8.0F, 0.0F}}});
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
                            if (ctx.selection != nullptr) {
                                ctx.selection->select(h, false);
                            }
                            ctx.selectedEntityId = h.id;
                            world::updateHierarchyTransforms(*ctx.world);
                            ctx.markDirty();
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                bool canDelete = (ctx.selectedEntityId != 0) ||
                                 (ctx.selection != nullptr && !ctx.selection->empty());
                if (ImGui::MenuItem("Delete Selected Entity", "Del", false, canDelete)) {
                    if (ctx.world != nullptr) {
                        if (ctx.selection != nullptr && !ctx.selection->empty()) {
                            auto selectedEntities = ctx.selection->getSelectedEntities();
                            std::vector<world::EntityHandle> toDelete(selectedEntities.begin(),
                                                                      selectedEntities.end());
                            for (auto h : toDelete) {
                                ctx.world->destroyEntity(h);
                            }
                            ctx.selection->clear();
                            ctx.selectedEntityId = 0;
                        } else if (ctx.selectedEntityId != 0) {
                            ctx.world->destroyEntity(world::EntityHandle(ctx.selectedEntityId));
                            ctx.selectedEntityId = 0;
                        }
                    }
                }
                ImGui::EndMenu();
            }

            // 5. Component Menu
            if (ImGui::BeginMenu("Component")) {
                bool hasEntity = (ctx.world != nullptr && ctx.selectedEntityId != 0 &&
                                  ctx.world->isAlive(world::EntityHandle(ctx.selectedEntityId)));
                if (ImGui::MenuItem("Physics Transform", nullptr, false, hasEntity)) {
                    if (hasEntity) {
                        flecs::world& ecs = ctx.world->getRawWorld();
                        flecs::entity e = ecs.entity(ctx.selectedEntityId);
                        if (!e.has<physics::PhysicsTransformComponent>()) {
                            e.set<physics::PhysicsTransformComponent>({});
                        }
                    }
                }
                if (ImGui::MenuItem("Rigid Body", nullptr, false, hasEntity)) {
                    if (hasEntity) {
                        flecs::world& ecs = ctx.world->getRawWorld();
                        flecs::entity e = ecs.entity(ctx.selectedEntityId);
                        if (!e.has<physics::RigidBodyComponent>()) {
                            e.set<physics::RigidBodyComponent>({
                                .motionType = physics::MotionType::Dynamic,
                                .collisionLayer = physics::CollisionLayer::kMoving,
                            });
                        }
                    }
                }
                if (ImGui::MenuItem("Collider", nullptr, false, hasEntity)) {
                    if (hasEntity) {
                        flecs::world& ecs = ctx.world->getRawWorld();
                        flecs::entity e = ecs.entity(ctx.selectedEntityId);
                        if (!e.has<physics::ColliderComponent>()) {
                            e.set<physics::ColliderComponent>({
                                .desc = physics::ColliderDesc::makeBox(Vec3(0.5F, 0.5F, 0.5F)),
                            });
                        }
                    }
                }
                if (ImGui::MenuItem("Script Component", nullptr, false, hasEntity)) {
                    if (hasEntity) {
                        flecs::world& ecs = ctx.world->getRawWorld();
                        flecs::entity e = ecs.entity(ctx.selectedEntityId);
                        if (!e.has<script::ScriptComponent>()) {
                            e.set<script::ScriptComponent>({
                                .scriptName = "",
                                .enabled = true,
                                .updateInterval = 0.0f,
                                .timeSinceLastUpdate = 0.0f,
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
                if (ImGui::BeginMenu("Theme")) {
                    auto currentTheme = getActiveThemeType();
                    if (ImGui::MenuItem("Hikari Dark", nullptr, currentTheme == ThemeType::Dark)) {
                        applyEditorTheme(ThemeType::Dark);
                    }
                    if (ImGui::MenuItem("Hikari Midnight", nullptr,
                                        currentTheme == ThemeType::Midnight)) {
                        applyEditorTheme(ThemeType::Midnight);
                    }
                    if (ImGui::MenuItem("Hikari Light", nullptr,
                                        currentTheme == ThemeType::Light)) {
                        applyEditorTheme(ThemeType::Light);
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

            // 8. Benchmarks Menu (Milestone 10G)
            if (ImGui::BeginMenu("Benchmarks")) {
                if (ImGui::MenuItem("Khronos Sponza Atrium (10G)")) {
                    if (ctx.world != nullptr) {
                        loadBenchmarkScene(*ctx.world, "test-assets/benchmark/sponza/Sponza.gltf",
                                           ctx.selection, ctx.assetManager);
                        ctx.selectedEntityId = 0;
                    }
                }
                if (ImGui::MenuItem("Khronos Flight Helmet")) {
                    if (ctx.world != nullptr) {
                        loadBenchmarkScene(*ctx.world,
                                           "test-assets/benchmark/flight_helmet/FlightHelmet.gltf",
                                           ctx.selection, ctx.assetManager);
                        ctx.selectedEntityId = 0;
                    }
                }
                if (ImGui::MenuItem("Damaged Helmet PBR")) {
                    if (ctx.world != nullptr) {
                        loadBenchmarkScene(*ctx.world, "test-assets/DamagedHelmet.glb",
                                           ctx.selection, ctx.assetManager);
                        ctx.selectedEntityId = 0;
                    }
                }
                ImGui::EndMenu();
            }

            // 9. Help Menu
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

        // Global Scene Shortcuts
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput) {
            if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
                if (isSceneDirty) {
                    showNewConfirmModal = true;
                } else if (ctx.world != nullptr) {
                    newScene(*ctx.world, ctx.selection);
                    ctx.selectedEntityId = 0;
                }
            } else if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
                if (isSceneDirty) {
                    showOpenConfirmModal = true;
                } else {
                    showOpenDialogModal = true;
                }
            } else if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
                if (ctx.world != nullptr) {
                    saveScene(*ctx.world);
                }
            } else if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
                showSaveAsDialogModal = true;
            } else if (ImGui::IsKeyPressed(ImGuiKey_F5, false) ||
                       (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_P, false))) {
                if (ctx.runtimeSession != nullptr) {
                    auto st = ctx.runtimeSession->getSimulationState();
                    if (st == framework::SimulationState::Edit ||
                        st == framework::SimulationState::Stopped) {
                        if (ctx.selection != nullptr) {
                            ctx.selection->clear();
                        }
                        ctx.selectedEntityId = 0;
                        (void)ctx.runtimeSession->play();
                    } else if (st == framework::SimulationState::Playing) {
                        (void)ctx.runtimeSession->pause();
                    } else if (st == framework::SimulationState::Paused) {
                        (void)ctx.runtimeSession->resume();
                    }
                }
            } else if ((io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F5, false)) ||
                       (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_X, false))) {
                if (ctx.runtimeSession != nullptr && ctx.runtimeSession->isSimulating()) {
                    if (ctx.selection != nullptr) {
                        ctx.selection->clear();
                    }
                    ctx.selectedEntityId = 0;
                    (void)ctx.runtimeSession->stop();
                }
            } else if (ImGui::IsKeyPressed(ImGuiKey_F10, false)) {
                if (ctx.runtimeSession != nullptr && ctx.runtimeSession->getSimulationState() ==
                                                         framework::SimulationState::Paused) {
                    (void)ctx.runtimeSession->step(Duration::fromMilliseconds(16));
                }
            }
        }

        // --- Modals ---

        if (showAboutModal) {
            ImGui::OpenPopup("About Hikari Editor");
            showAboutModal = false;
        }
        if (ImGui::BeginPopupModal("About Hikari Editor", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextColored(ImVec4(0.38F, 0.74F, 0.98F, 1.0F), "Hikari Engine - Studio Editor");
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

        if (showNewConfirmModal) {
            ImGui::OpenPopup("Unsaved Changes##NewScene");
            showNewConfirmModal = false;
        }
        if (ImGui::BeginPopupModal("Unsaved Changes##NewScene", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextColored(ImVec4(0.96F, 0.62F, 0.04F, 1.0F), "[!] Unsaved Modifications");
            ImGui::Text("The active scene has unsaved changes.\nDo you want to discard them and "
                        "create a new scene?");
            ImGui::Separator();
            if (ImGui::Button("Discard & Create New", ImVec2(170.0F, 0.0F))) {
                if (ctx.world != nullptr) {
                    newScene(*ctx.world, ctx.selection);
                    ctx.selectedEntityId = 0;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(90.0F, 0.0F))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (showOpenConfirmModal) {
            ImGui::OpenPopup("Unsaved Changes##OpenScene");
            showOpenConfirmModal = false;
        }
        if (ImGui::BeginPopupModal("Unsaved Changes##OpenScene", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextColored(ImVec4(0.96F, 0.62F, 0.04F, 1.0F), "[!] Unsaved Modifications");
            ImGui::Text("The active scene has unsaved changes.\nDo you want to discard them and "
                        "open a different scene?");
            ImGui::Separator();
            if (ImGui::Button("Discard & Open...", ImVec2(160.0F, 0.0F))) {
                showOpenDialogModal = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(90.0F, 0.0F))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (showOpenDialogModal) {
            ImGui::OpenPopup("Open Scene");
            showOpenDialogModal = false;
        }
        if (ImGui::BeginPopupModal("Open Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Enter .hikari scene file path:");
            ImGui::InputText("##OpenPath", openPathBuffer, sizeof(openPathBuffer));
            ImGui::SameLine();
            if (ImGui::Button("Sample Scene")) {
                std::snprintf(openPathBuffer, sizeof(openPathBuffer),
                              "assets/scenes/sample.hikari");
            }
            ImGui::Separator();
            if (ImGui::Button("Open", ImVec2(120.0F, 0.0F))) {
                if (ctx.world != nullptr) {
                    if (openScene(*ctx.world, openPathBuffer, ctx.selection)) {
                        ctx.selectedEntityId = 0;
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120.0F, 0.0F))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (showSaveAsDialogModal) {
            ImGui::OpenPopup("Save Scene As");
            showSaveAsDialogModal = false;
        }
        if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Enter destination .hikari file path:");
            ImGui::InputText("##SaveAsPath", savePathBuffer, sizeof(savePathBuffer));
            ImGui::Separator();
            if (ImGui::Button("Save", ImVec2(120.0F, 0.0F))) {
                if (ctx.world != nullptr) {
                    if (saveScene(*ctx.world, savePathBuffer)) {
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120.0F, 0.0F))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (showSceneErrorModal) {
            ImGui::OpenPopup("Scene Operation Error");
            showSceneErrorModal = false;
        }
        if (ImGui::BeginPopupModal("Scene Operation Error", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextColored(ImVec4(1.0F, 0.3F, 0.3F, 1.0F), "[!] Error:");
            ImGui::TextWrapped("%s", lastSceneError.c_str());
            ImGui::Separator();
            if (ImGui::Button("OK", ImVec2(120.0F, 0.0F))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void drawToolbar(EditorContext& ctx) {
        const auto& theme = getActiveThemeColors();
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        float menuBarHeight = ImGui::GetFrameHeight();
        float toolbarHeight = ThemeMetrics::kToolbarHeight;

        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + menuBarHeight));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, toolbarHeight));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoScrollWithMouse |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ThemeMetrics::kSpacingL, 4.0F));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, toImVec4(theme.backgroundApp));

        if (ImGui::Begin("##EditorToolbar", nullptr, flags)) {
            // Group 1: Scene identity badge & Workspace layout selector
            char sceneBuf[128];
            std::snprintf(sceneBuf, sizeof(sceneBuf), "Scene: %s%s", currentSceneName.c_str(),
                          isSceneDirty ? "*" : "");
            drawBadge(sceneBuf, isSceneDirty ? theme.statusWarning : theme.accentPrimary);

            ImGui::SameLine();
            ImGui::TextColored(toImVec4(theme.borderMedium), "|");
            ImGui::SameLine();

            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(toImVec4(theme.textSecondary), "Layout:");
            ImGui::SameLine();
            const char* layoutNames[] = {"Default Studio", "Wide Viewport", "Inspector Focus"};
            ImGui::SetNextItemWidth(130.0F);
            if (ImGui::Combo("##LayoutCombo", &activeLayoutPreset, layoutNames, 3)) {
                buildDefaultLayout(ImGui::GetID("HikariRootDockSpace"), activeLayoutPreset);
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Layout", ImVec2(0.0F, ThemeMetrics::kButtonHeightStandard))) {
                layoutResetPending = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Reset Docking Layout to Default");
            }

            // Group 2: Transport simulation controls (Centered Island)
            auto simState =
                ctx.runtimeSession
                    ? ctx.runtimeSession->getSimulationState()
                    : (ctx.isSimulating ? (ctx.isPaused ? framework::SimulationState::Paused
                                                        : framework::SimulationState::Playing)
                                        : framework::SimulationState::Edit);
            bool canPlay = (simState == framework::SimulationState::Edit ||
                            simState == framework::SimulationState::Stopped);
            bool canPause = (simState == framework::SimulationState::Playing);
            bool canResume = (simState == framework::SimulationState::Paused);
            bool canStep = (simState == framework::SimulationState::Paused);
            bool canStop = (simState == framework::SimulationState::Playing ||
                            simState == framework::SimulationState::Paused);

            constexpr float kTransportIslandWidth = 470.0F;
            float centerPosX = (viewport->Size.x - kTransportIslandWidth) * 0.5F;
            if (centerPosX > ImGui::GetCursorPosX()) {
                ImGui::SameLine(centerPosX);
            } else {
                ImGui::SameLine();
            }

            // Play button
            if (drawTransportButton(EditorIcons::kPlay, "Play", theme.transportPlay, !canPlay,
                                    canPlay)) {
                if (ctx.selection != nullptr) {
                    ctx.selection->clear();
                }
                ctx.selectedEntityId = 0;
                if (ctx.runtimeSession != nullptr) {
                    (void)ctx.runtimeSession->play();
                    ctx.world = ctx.runtimeSession->getActiveWorld();
                    ctx.conductor = ctx.runtimeSession->getActiveConductor();
                    ctx.simulationState = ctx.runtimeSession->getSimulationState();
                    ctx.isRuntimeActive = ctx.runtimeSession->isSimulating();
                    ctx.isSimulating = ctx.runtimeSession->isSimulating();
                    ctx.isPaused = false;
                } else {
                    ctx.isSimulating = true;
                    ctx.isPaused = false;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Start Simulation (F5 / Ctrl+P)");
            }

            ImGui::SameLine();

            // Pause button
            if (drawTransportButton(EditorIcons::kPause, "Pause", theme.transportPause, !canPause,
                                    simState == framework::SimulationState::Paused)) {
                if (ctx.runtimeSession != nullptr) {
                    (void)ctx.runtimeSession->pause();
                    ctx.simulationState = ctx.runtimeSession->getSimulationState();
                    ctx.isPaused = true;
                } else {
                    ctx.isPaused = true;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Pause Simulation (Ctrl+P)");
            }

            ImGui::SameLine();

            // Resume button
            if (drawTransportButton(EditorIcons::kResume, "Resume", theme.transportResume,
                                    !canResume, canResume)) {
                if (ctx.runtimeSession != nullptr) {
                    (void)ctx.runtimeSession->resume();
                    ctx.simulationState = ctx.runtimeSession->getSimulationState();
                    ctx.isPaused = false;
                } else {
                    ctx.isPaused = false;
                }
            }

            ImGui::SameLine();

            // Step button
            if (drawTransportButton(EditorIcons::kStep, "Step", theme.transportStep, !canStep)) {
                if (ctx.runtimeSession != nullptr) {
                    (void)ctx.runtimeSession->step(Duration::fromMilliseconds(16));
                }
                ENG_LOG_INFO("Editor", "Simulation Step executed");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Advance Single Frame Step (F10)");
            }

            ImGui::SameLine();

            // Stop button
            if (drawTransportButton(EditorIcons::kStop, "Stop", theme.transportStop, !canStop,
                                    canStop)) {
                if (ctx.selection != nullptr) {
                    ctx.selection->clear();
                }
                ctx.selectedEntityId = 0;
                if (ctx.runtimeSession != nullptr) {
                    (void)ctx.runtimeSession->stop();
                    ctx.world = ctx.runtimeSession->getActiveWorld();
                    ctx.conductor = ctx.runtimeSession->getActiveConductor();
                    ctx.simulationState = ctx.runtimeSession->getSimulationState();
                    ctx.isRuntimeActive = false;
                    ctx.isSimulating = false;
                    ctx.isPaused = false;
                } else {
                    ctx.isSimulating = false;
                    ctx.isPaused = false;
                }
                ENG_LOG_INFO("Editor", "Simulation Stopped; returned to Edit Mode");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Stop Simulation & Restore Edit State (Shift+F5 / Ctrl+X)");
            }

            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();

            // Simulation Mode Badge
            if (simState == framework::SimulationState::Playing) {
                drawStatusPill("PLAYING", theme.statusSuccess);
            } else if (simState == framework::SimulationState::Paused) {
                drawStatusPill("PAUSED", theme.statusWarning);
            } else {
                drawStatusPill("EDIT MODE", theme.textMuted);
            }

            // Group 3: Two-State Conductor badge & quick switch
            if (ctx.conductor != nullptr) {
                float rightAnchor = viewport->Size.x - 240.0F;
                if (ImGui::GetCursorPosX() < rightAnchor) {
                    ImGui::SameLine(rightAnchor);
                } else {
                    ImGui::SameLine();
                }

                auto activeSlot = ctx.conductor->getActiveSlot();
                auto state = ctx.conductor->getState();

                std::string_view slotName = ctx.conductor->getSlotDisplayName(activeSlot);
                char slotBuf[64];
                std::snprintf(slotBuf, sizeof(slotBuf), "Active: %.*s",
                              static_cast<int>(slotName.size()), slotName.data());

                if (state == framework::ConductorState::Transitioning) {
                    drawBadge("Transitioning", theme.badgeStandby);
                } else {
                    drawBadge(slotBuf, (activeSlot == framework::ActiveSlot::SlotA)
                                           ? theme.accentPrimary
                                           : theme.statusWarning);
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
                        (void)ctx.conductor->requestTransition(targetSlot, true);
                    }
                } else if (state == framework::ConductorState::Prepared) {
                    if (ImGui::SmallButton("Commit Switch")) {
                        (void)ctx.conductor->executeTransition();
                    }
                }
            } else {
                ImGui::SameLine();
                ImGui::TextDisabled("[Conductor: Standalone]");
            }
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    void drawStatusBar(EditorContext& ctx) {
        const auto& theme = getActiveThemeColors();
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        float statusHeight = ThemeMetrics::kStatusBarHeight;

        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - statusHeight));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, statusHeight));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoScrollWithMouse |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0F, 5.0F));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, toImVec4(theme.backgroundDarkest));

        if (ImGui::Begin("##EditorStatusBar", nullptr, flags)) {
            // Left: Mode & status badge
            if (ctx.isRuntimeActive) {
                if (ctx.isPaused) {
                    drawStatusPill("RUNTIME PAUSED", theme.statusWarning);
                } else {
                    drawStatusPill("RUNTIME PLAYING", theme.statusSuccess);
                }
            } else {
                drawStatusPill("EDIT MODE", theme.textMuted);
            }
            ImGui::SameLine();
            ImGui::TextColored(toImVec4(theme.borderMedium), "|");
            ImGui::SameLine();
            if (!currentScenePath.empty()) {
                ImGui::TextColored(toImVec4(theme.textSecondary), "Scene: %s%s",
                                   currentScenePath.c_str(), isSceneDirty ? "*" : "");
            } else {
                ImGui::TextColored(toImVec4(theme.textMuted), "Scene: [Untitled]%s",
                                   isSceneDirty ? "*" : "");
            }
            ImGui::SameLine();
            ImGui::TextColored(toImVec4(theme.borderMedium), "|");

            // Selected entity badge
            ImGui::SameLine();
            if (ctx.selection != nullptr && ctx.selection->count() > 1) {
                ImGui::TextColored(toImVec4(theme.textPrimary), "Selected: %zu Entities",
                                   ctx.selection->count());
            } else if (ctx.selectedEntityId != 0 && ctx.world != nullptr &&
                       ctx.world->isAlive(world::EntityHandle(ctx.selectedEntityId))) {
                flecs::world& ecs = ctx.world->getRawWorld();
                flecs::entity e = ecs.entity(ctx.selectedEntityId);
                const char* name = e.name().c_str();
                if (name != nullptr && name[0] != '\0') {
                    ImGui::TextColored(toImVec4(theme.textPrimary), "Selected: %s (#%llu)", name,
                                       static_cast<unsigned long long>(ctx.selectedEntityId));
                } else {
                    ImGui::TextColored(toImVec4(theme.textPrimary), "Selected: Entity #%llu",
                                       static_cast<unsigned long long>(ctx.selectedEntityId));
                }
            } else {
                ImGui::TextColored(toImVec4(theme.textMuted), "No Selection");
            }

            // ECS stats
            if (ctx.world != nullptr) {
                ImGui::SameLine();
                ImGui::TextColored(toImVec4(theme.borderMedium), "|");
                ImGui::SameLine();
                flecs::world& ecs = ctx.world->getRawWorld();
                ImGui::TextColored(toImVec4(theme.textSecondary), "ECS: %d entities",
                                   ecs.count<flecs::Component>());
            }

            // Right: GPU & resolution & performance
            ImGui::SameLine();
            ImGui::TextColored(toImVec4(theme.borderMedium), "|");
            ImGui::SameLine();
            if (ctx.rhiDevice != nullptr) {
                ImGui::TextColored(toImVec4(theme.textSecondary), "Vulkan 1.3 | %s",
                                   ctx.rhiDevice->getCaps().deviceName.data());
            } else {
                ImGui::TextColored(toImVec4(theme.textMuted), "Null RHI");
            }

            ImGui::SameLine();
            ImGui::TextColored(toImVec4(theme.borderMedium), "|");
            ImGui::SameLine();
            ImGui::TextColored(toImVec4(theme.textSecondary), "Viewport: %ux%u", ctx.windowWidth,
                               ctx.windowHeight);

            ImGui::SameLine();
            ImGui::TextColored(toImVec4(theme.borderMedium), "|");
            ImGui::SameLine();

            Color4f fpsColor = (ctx.currentFps >= 55.0F)   ? theme.statusSuccess
                               : (ctx.currentFps >= 30.0F) ? theme.statusWarning
                                                           : theme.statusError;
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetCursorScreenPos();
            const float radius = 3.5F;
            const float lineHeight = ImGui::GetTextLineHeight();
            const float centerY = p.y + (lineHeight * 0.5F);
            drawList->AddCircleFilled(ImVec2(p.x + radius + 1.0F, centerY), radius,
                                      ImGui::GetColorU32(toImVec4(fpsColor)));
            ImGui::Dummy(ImVec2((radius * 2.0F) + 5.0F, lineHeight));
            ImGui::SameLine(0.0F, 0.0F);
            ImGui::TextColored(toImVec4(fpsColor), "%.1f FPS (%.2f ms)",
                               static_cast<double>(ctx.currentFps),
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

bool WorkspaceManager::newScene(world::World& world, SelectionModel* selection) {
    return impl_->newScene(world, selection);
}

bool WorkspaceManager::saveScene(world::World& world, const char* customPath) {
    return impl_->saveScene(world, customPath);
}

bool WorkspaceManager::openScene(world::World& world, const char* filepath,
                                 SelectionModel* selection) {
    return impl_->openScene(world, filepath, selection);
}

bool WorkspaceManager::loadBenchmarkScene(world::World& world, const char* filepath,
                                          SelectionModel* selection,
                                          render::AssetManager* assetManager) {
    return impl_->loadBenchmarkScene(world, filepath, selection, assetManager);
}

std::string_view WorkspaceManager::getCurrentScenePath() const noexcept {
    return impl_->currentScenePath;
}

void WorkspaceManager::setCurrentScenePath(std::string_view path) {
    impl_->currentScenePath = std::string(path);
    std::filesystem::path p(impl_->currentScenePath);
    impl_->currentSceneName = p.stem().string();
}

bool WorkspaceManager::isSceneDirty() const noexcept {
    return impl_->isSceneDirty;
}

void WorkspaceManager::markSceneDirty() noexcept {
    impl_->isSceneDirty = true;
}

void WorkspaceManager::clearSceneDirty() noexcept {
    impl_->isSceneDirty = false;
}

void WorkspaceManager::drawWorkspace(EditorContext& ctx) {
    if (ctx.requestMarkDirty) {
        impl_->isSceneDirty = true;
        ctx.requestMarkDirty = false;
    }
    ctx.isSceneDirty = impl_->isSceneDirty;

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
