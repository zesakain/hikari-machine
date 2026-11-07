/// @file editor_engine.cpp
/// @brief Dear ImGui developer workspace, panel layout, and Vulkan dynamic rendering backend.

#include "editor/editor_engine.h"

#include "core/assert.h"
#include "core/log.h"
#include "core/math.h"
#include "framework/conductor.h"
#include "physics/physics_components.h"
#include "platform/input_snapshot.h"
#include "rhi/command_list.h"
#include "rhi/rhi_device.h"
#include "script/script_components.h"
#include "world/world.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <vector>
#include <vulkan/vulkan.h>

namespace eng::editor {

struct EditorEngine::Impl {
    ImGuiContext* imguiContext{nullptr};
    rhi::RhiDevice* rhiDevice{nullptr};
    world::World* boundWorld{nullptr};
    framework::Conductor* boundConductor{nullptr};

    EditorConfig config{};
    uint32_t lastDrawCalls{0};
    uint32_t lastVertices{0};
    uint32_t lastIndices{0};
    uint64_t executedPasses{0};

    bool initialized{false};
    bool vulkanBackendInitialized{false};
    uint64_t selectedEntityId{0};

    // Sub-panel drawing routines
    void drawMainMenuBar();
    void drawToolbar();
    void drawHierarchyPanel();
    void drawInspectorPanel();
    void drawViewportPanel();
    void drawContentBrowser();
    void drawConsolePanel();
    void drawMetricsPanel();
    void drawConductorPanel();
    void drawDiagnosticPanel();
};

void EditorEngine::Impl::drawMainMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                ENG_LOG_INFO("Editor", "File -> New Scene requested");
            }
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
                ENG_LOG_INFO("Editor", "File -> Open Scene requested");
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                ENG_LOG_INFO("Editor", "File -> Save Scene requested");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                ENG_LOG_INFO("Editor", "File -> Exit requested");
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, false)) {
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, false)) {
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences...")) {
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Reset Layout")) {
                config = EditorConfig{};
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window")) {
            ImGui::MenuItem("Entity Hierarchy", nullptr, &config.showHierarchyPanel);
            ImGui::MenuItem("Entity Inspector", nullptr, &config.showEntityInspector);
            ImGui::MenuItem("Scene Viewport", nullptr, &config.showViewportPanel);
            ImGui::MenuItem("Content Browser", nullptr, &config.showContentBrowser);
            ImGui::MenuItem("Log Console", nullptr, &config.showConsolePanel);
            ImGui::MenuItem("Metrics & Telemetry", nullptr, &config.showMetricsPanel);
            ImGui::MenuItem("Conductor Monitor", nullptr, &config.showConductorPanel);
            ImGui::MenuItem("HIKARI EDITOR Diagnostic", nullptr, &config.showDiagnosticPanel);
            ImGui::MenuItem("Main Toolbar", nullptr, &config.showToolbar);
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo Window", nullptr, &config.showDemoWindow);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Entity")) {
            if (ImGui::MenuItem("Create Empty Entity")) {
                if (boundWorld != nullptr) {
                    (void)boundWorld->createEntity("EmptyEntity");
                }
            }
            if (ImGui::MenuItem("Create 3D Physics Cube")) {
                if (boundWorld != nullptr) {
                    auto e = boundWorld->createEntity("PhysicsCube");
                    boundWorld->setComponent(e, physics::PhysicsTransformComponent{
                                                    .transform = Transform{Vec3{0.0F, 5.0F, 0.0F}},
                                                });
                    boundWorld->setComponent(e,
                                             physics::RigidBodyComponent{
                                                 .bodyHandle = {},
                                                 .motionType = physics::MotionType::Dynamic,
                                                 .collisionLayer = physics::CollisionLayer::kMoving,
                                             });
                }
            }
            if (ImGui::MenuItem("Create 3D Physics Sphere")) {
                if (boundWorld != nullptr) {
                    auto e = boundWorld->createEntity("PhysicsSphere");
                    boundWorld->setComponent(e, physics::PhysicsTransformComponent{
                                                    .transform = Transform{Vec3{2.0F, 8.0F, 0.0F}},
                                                });
                    boundWorld->setComponent(e,
                                             physics::RigidBodyComponent{
                                                 .bodyHandle = {},
                                                 .motionType = physics::MotionType::Dynamic,
                                                 .collisionLayer = physics::CollisionLayer::kMoving,
                                             });
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Selected Entity", "Del", false, selectedEntityId != 0)) {
                if (boundWorld != nullptr && selectedEntityId != 0) {
                    boundWorld->destroyEntity(world::EntityHandle(selectedEntityId));
                    selectedEntityId = 0;
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Assets")) {
            if (ImGui::MenuItem("Import Asset...")) {
            }
            if (ImGui::MenuItem("Refresh Asset Directory")) {
                ENG_LOG_INFO("Editor", "Asset index refreshed");
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Reload Lua Scripts")) {
                ENG_LOG_INFO("Editor", "Lua gameplay scripts reloaded");
            }
            if (ImGui::MenuItem("Recompile Shaders")) {
                ENG_LOG_INFO("Editor", "Shader permutations cache refreshed");
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About Hikari...")) {
                config.showDiagnosticPanel = true;
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void EditorEngine::Impl::drawToolbar() {
    ImGui::SetNextWindowPos(ImVec2(0.0F, 20.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(1280.0F, 42.0F), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##MainToolbar", nullptr, flags)) {
        if (ImGui::Button(" Play ")) {
            ENG_LOG_INFO("Editor", "Simulation Play triggered");
        }
        ImGui::SameLine();
        if (ImGui::Button(" Pause ")) {
            ENG_LOG_INFO("Editor", "Simulation Pause triggered");
        }
        ImGui::SameLine();
        if (ImGui::Button(" Step ")) {
            ENG_LOG_INFO("Editor", "Simulation Step triggered");
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // Runtime Conductor state badge
        if (boundConductor != nullptr) {
            auto activeSlot = boundConductor->getActiveSlot();
            auto state = boundConductor->getState();

            const char* slotName = (activeSlot == framework::ActiveSlot::SlotA)
                                       ? "State A: Exploration"
                                       : "State B: Skill";
            ImVec4 badgeColor = (activeSlot == framework::ActiveSlot::SlotA)
                                    ? ImVec4(0.3F, 0.6F, 1.0F, 1.0F)
                                    : ImVec4(1.0F, 0.7F, 0.2F, 1.0F);

            if (state == framework::ConductorState::Transitioning) {
                slotName = "Transitioning...";
                badgeColor = ImVec4(0.9F, 0.3F, 0.9F, 1.0F);
            }

            ImGui::TextColored(badgeColor, "[Active: %s]", slotName);

            ImGui::SameLine();
            if (state == framework::ConductorState::Active) {
                if (ImGui::SmallButton("Request Switch")) {
                    (void)boundConductor->requestTransition();
                }
            } else if (state == framework::ConductorState::Prepared) {
                if (ImGui::SmallButton("Execute Switch")) {
                    (void)boundConductor->executeTransition();
                }
            }
        } else {
            ImGui::TextDisabled("[Conductor: None]");
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("FPS: %.1f (%.2f ms)", static_cast<double>(io.Framerate),
                    static_cast<double>(1000.0F / (io.Framerate > 0.0F ? io.Framerate : 60.0F)));
    }
    ImGui::End();
}

void EditorEngine::Impl::drawHierarchyPanel() {
    ImGui::SetNextWindowPos(ImVec2(10.0F, 66.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(250.0F, 420.0F), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Entity Hierarchy", &config.showHierarchyPanel)) {
        if (boundWorld != nullptr) {
            if (ImGui::Button("+ Empty")) {
                auto h = boundWorld->createEntity("EmptyEntity");
                selectedEntityId = h.id;
            }
            ImGui::SameLine();
            if (ImGui::Button("+ Cube")) {
                auto h = boundWorld->createEntity("PhysicsCube");
                boundWorld->setComponent(h, physics::PhysicsTransformComponent{
                                                .transform = Transform{Vec3{0.0F, 2.0F, 0.0F}},
                                            });
                boundWorld->setComponent(h, physics::RigidBodyComponent{
                                                .bodyHandle = {},
                                                .motionType = physics::MotionType::Dynamic,
                                                .collisionLayer = physics::CollisionLayer::kMoving,
                                            });
                selectedEntityId = h.id;
            }
            ImGui::SameLine();
            if (ImGui::Button("+ Sphere")) {
                auto h = boundWorld->createEntity("PhysicsSphere");
                boundWorld->setComponent(h, physics::PhysicsTransformComponent{
                                                .transform = Transform{Vec3{1.0F, 4.0F, 0.0F}},
                                            });
                boundWorld->setComponent(h, physics::RigidBodyComponent{
                                                .bodyHandle = {},
                                                .motionType = physics::MotionType::Dynamic,
                                                .collisionLayer = physics::CollisionLayer::kMoving,
                                            });
                selectedEntityId = h.id;
            }

            ImGui::Separator();
            static char searchBuf[64] = "";
            ImGui::InputText("Filter", searchBuf, sizeof(searchBuf));
            ImGui::Separator();

            flecs::world& ecs = boundWorld->getRawWorld();
            ecs.each([&](flecs::entity e) {
                if (!e.is_alive()) {
                    return;
                }
                const char* name = e.name().c_str();
                if (name == nullptr || name[0] == '\0') {
                    name = "Unnamed";
                }
                if (searchBuf[0] != '\0' && strstr(name, searchBuf) == nullptr) {
                    return;
                }

                char label[128];
                std::snprintf(label, sizeof(label), "[%llu] %s",
                              static_cast<unsigned long long>(e.id()), name);
                bool isSelected = (selectedEntityId == e.id());
                if (ImGui::Selectable(label, isSelected)) {
                    selectedEntityId = e.id();
                }
            });
        } else {
            ImGui::TextDisabled("No Shared World bound");
        }
    }
    ImGui::End();
}

void EditorEngine::Impl::drawInspectorPanel() {
    ImGui::SetNextWindowPos(ImVec2(980.0F, 66.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(290.0F, 420.0F), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Entity Inspector", &config.showEntityInspector)) {
        if (boundWorld != nullptr && selectedEntityId != 0) {
            flecs::world& ecs = boundWorld->getRawWorld();
            flecs::entity e = ecs.entity(selectedEntityId);

            if (e.is_alive()) {
                ImGui::TextColored(ImVec4(0.4F, 0.8F, 1.0F, 1.0F), "Entity ID: %llu",
                                   static_cast<unsigned long long>(e.id()));
                ImGui::Text("Name: %s", e.name().c_str());
                ImGui::Separator();

                // 1. Physics Transform Component
                if (e.has<physics::PhysicsTransformComponent>()) {
                    if (ImGui::CollapsingHeader("Physics Transform",
                                                ImGuiTreeNodeFlags_DefaultOpen)) {
                        auto* tc = e.get_mut<physics::PhysicsTransformComponent>();
                        float pos[3] = {tc->transform.position.x, tc->transform.position.y,
                                        tc->transform.position.z};
                        if (ImGui::DragFloat3("Position", pos, 0.1F)) {
                            tc->transform.position = Vec3(pos[0], pos[1], pos[2]);
                        }
                        float rot[4] = {tc->transform.rotation.x, tc->transform.rotation.y,
                                        tc->transform.rotation.z, tc->transform.rotation.w};
                        if (ImGui::DragFloat4("Rotation (xyzw)", rot, 0.05F)) {
                            tc->transform.rotation =
                                Quat(rot[0], rot[1], rot[2], rot[3]).normalized();
                        }
                        float scl[3] = {tc->transform.scale.x, tc->transform.scale.y,
                                        tc->transform.scale.z};
                        if (ImGui::DragFloat3("Scale", scl, 0.1F)) {
                            tc->transform.scale = Vec3(scl[0], scl[1], scl[2]);
                        }
                    }
                }

                // 2. Rigid Body Component
                if (e.has<physics::RigidBodyComponent>()) {
                    if (ImGui::CollapsingHeader("Rigid Body", ImGuiTreeNodeFlags_DefaultOpen)) {
                        auto* rb = e.get_mut<physics::RigidBodyComponent>();
                        int motionIdx = (rb->motionType == physics::MotionType::Static)      ? 0
                                        : (rb->motionType == physics::MotionType::Kinematic) ? 1
                                                                                             : 2;
                        const char* motions[] = {"Static", "Kinematic", "Dynamic"};
                        if (ImGui::Combo("Motion Type", &motionIdx, motions, 3)) {
                            rb->motionType = (motionIdx == 0)   ? physics::MotionType::Static
                                             : (motionIdx == 1) ? physics::MotionType::Kinematic
                                                                : physics::MotionType::Dynamic;
                        }
                        int layer = rb->collisionLayer;
                        if (ImGui::InputInt("Collision Layer", &layer)) {
                            rb->collisionLayer = static_cast<uint16_t>(layer);
                        }
                    }
                }

                // 3. Script Component
                if (e.has<script::ScriptComponent>()) {
                    if (ImGui::CollapsingHeader("Script Component",
                                                ImGuiTreeNodeFlags_DefaultOpen)) {
                        auto* sc = e.get_mut<script::ScriptComponent>();
                        ImGui::Text("Script: %s", sc->scriptName);
                        ImGui::Text("State: %s", sc->enabled ? "Enabled" : "Disabled");
                    }
                }

                ImGui::Separator();
                if (ImGui::Button("Delete Entity")) {
                    e.destruct();
                    selectedEntityId = 0;
                }
            } else {
                ImGui::TextDisabled("Selected entity is no longer alive");
            }
        } else {
            ImGui::TextDisabled("Select an entity from Hierarchy to inspect");
        }
    }
    ImGui::End();
}

void EditorEngine::Impl::drawViewportPanel() {
    ImGui::SetNextWindowPos(ImVec2(265.0F, 66.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(710.0F, 420.0F), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Scene Viewport", &config.showViewportPanel)) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 screenPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Dark viewport canvas background
        drawList->AddRectFilled(screenPos, ImVec2(screenPos.x + avail.x, screenPos.y + avail.y),
                                IM_COL32(20, 26, 38, 255));

        // Coordinate grid lines
        float step = 40.0F;
        for (float x = 0.0F; x < avail.x; x += step) {
            drawList->AddLine(ImVec2(screenPos.x + x, screenPos.y),
                              ImVec2(screenPos.x + x, screenPos.y + avail.y),
                              IM_COL32(35, 45, 62, 255));
        }
        for (float y = 0.0F; y < avail.y; y += step) {
            drawList->AddLine(ImVec2(screenPos.x, screenPos.y + y),
                              ImVec2(screenPos.x + avail.x, screenPos.y + y),
                              IM_COL32(35, 45, 62, 255));
        }

        // Coordinate crosshair at center
        ImVec2 center(screenPos.x + (avail.x * 0.5F), screenPos.y + (avail.y * 0.5F));
        drawList->AddLine(ImVec2(center.x - 20.0F, center.y), ImVec2(center.x + 20.0F, center.y),
                          IM_COL32(100, 180, 255, 255), 2.0F);
        drawList->AddLine(ImVec2(center.x, center.y - 20.0F), ImVec2(center.x, center.y + 20.0F),
                          IM_COL32(100, 180, 255, 255), 2.0F);

        // Viewport diagnostic overlay
        ImGui::SetCursorPos(ImVec2(15.0F, 30.0F));
        ImGui::TextColored(ImVec4(0.4F, 0.8F, 1.0F, 1.0F), "HIKARI SCENE VIEWPORT");
        ImGui::SetCursorPos(ImVec2(15.0F, 50.0F));
        ImGui::TextDisabled("Resolution: %.0fx%.0f (Aspect: %.2f:1)", static_cast<double>(avail.x),
                            static_cast<double>(avail.y),
                            avail.y > 0.0F ? static_cast<double>(avail.x / avail.y) : 1.0);
        ImGui::SetCursorPos(ImVec2(15.0F, 70.0F));
        const char* activeMode =
            (boundConductor != nullptr) ? boundConductor->getActiveMode().getName().data() : "None";
        ImGui::Text("Active Mode: %s", activeMode);
        ImGui::SetCursorPos(ImVec2(15.0F, 90.0F));
        ImGui::TextDisabled("Camera: Perspective (FOV 60 deg, Far 1000m) [Empty Scene Baseline]");
    }
    ImGui::End();
}

void EditorEngine::Impl::drawContentBrowser() {
    ImGui::SetNextWindowPos(ImVec2(10.0F, 492.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(480.0F, 220.0F), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Content Browser", &config.showContentBrowser)) {
        ImGui::TextColored(ImVec4(0.6F, 0.8F, 1.0F, 1.0F), "Virtual Project Asset Root:");
        ImGui::Separator();

        if (ImGui::TreeNodeEx("assets/ (Sources)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BulletText("models/ (glTF / binary meshes)");
            ImGui::BulletText("textures/ (PNG / KTX2 GPU textures)");
            ImGui::BulletText("audio/ (WAV sound effects & music)");
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("data/ (Tables & Definitions)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BulletText("skills.toml (Skill combat parameters)");
            ImGui::BulletText("characters.toml (Character balance data)");
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("game/ (Scripts & Modes)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BulletText("scripts/main.lua (Gameplay scripting hooks)");
            ImGui::BulletText("modes/exploration_mode.cpp (State A)");
            ImGui::BulletText("modes/skill_mode.cpp (State B)");
            ImGui::TreePop();
        }
    }
    ImGui::End();
}

void EditorEngine::Impl::drawConsolePanel() {
    ImGui::SetNextWindowPos(ImVec2(495.0F, 492.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(480.0F, 220.0F), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Log Console", &config.showConsolePanel)) {
        auto& logger = Logger::defaultLogger();
        if (ImGui::Button("Clear")) {
            logger.clear();
        }
        ImGui::SameLine();
        ImGui::Text("Entries: %zu / %zu", logger.count(), logger.capacity());
        ImGui::Separator();

        ImGui::BeginChild("LogScrollingRegion", ImVec2(0.0F, 0.0F), false,
                          ImGuiWindowFlags_HorizontalScrollbar);
        for (size_t i = 0; i < logger.count(); ++i) {
            const auto* record = logger.peek(i);
            if (record != nullptr) {
                ImVec4 color = ImVec4(1.0F, 1.0F, 1.0F, 1.0F);
                if (record->level == LogLevel::Warning) {
                    color = ImVec4(1.0F, 0.8F, 0.2F, 1.0F);
                } else if (record->level >= LogLevel::Error) {
                    color = ImVec4(1.0F, 0.3F, 0.3F, 1.0F);
                }
                ImGui::TextColored(color, "[%s] %.*s", toString(record->level).data(),
                                   static_cast<int>(record->messageLength), record->message.data());
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void EditorEngine::Impl::drawMetricsPanel() {
    ImGui::SetNextWindowPos(ImVec2(980.0F, 492.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(290.0F, 220.0F), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Metrics & Telemetry", &config.showMetricsPanel)) {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("FPS: %.1f", static_cast<double>(io.Framerate));
        ImGui::Text("Frame Time: %.3f ms",
                    static_cast<double>(1000.0F / (io.Framerate > 0.0F ? io.Framerate : 60.0F)));
        ImGui::Separator();
        ImGui::Text("ImGui Draws: %u", lastDrawCalls);
        ImGui::Text("ImGui Verts: %u", lastVertices);
        ImGui::Text("ImGui Idxs:  %u", lastIndices);
        ImGui::Text("Passes:      %llu", static_cast<unsigned long long>(executedPasses));

        if (boundWorld != nullptr) {
            ImGui::Separator();
            flecs::world& ecs = boundWorld->getRawWorld();
            ImGui::Text("ECS Entities: %d", ecs.count<flecs::Component>());
        }
    }
    ImGui::End();
}

void EditorEngine::Impl::drawConductorPanel() {
    if (!config.showConductorPanel) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(380.0F, 260.0F), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Conductor Monitor", &config.showConductorPanel)) {
        if (boundConductor != nullptr) {
            auto activeSlot = boundConductor->getActiveSlot();
            auto state = boundConductor->getState();
            ImGui::Text("Active Slot: %s (%s)",
                        activeSlot == framework::ActiveSlot::SlotA ? "Slot A" : "Slot B",
                        boundConductor->getActiveMode().getName().data());
            ImGui::Text("Standby Mode: %s", boundConductor->getStandbyMode().getName().data());

            const char* stateStr = "Active";
            switch (state) {
            case framework::ConductorState::Uninitialized:
                stateStr = "Uninitialized";
                break;
            case framework::ConductorState::Active:
                stateStr = "Active";
                break;
            case framework::ConductorState::Preparing:
                stateStr = "Preparing";
                break;
            case framework::ConductorState::Prepared:
                stateStr = "Prepared";
                break;
            case framework::ConductorState::Committing:
                stateStr = "Committing";
                break;
            case framework::ConductorState::Transitioning:
                stateStr = "Transitioning";
                break;
            case framework::ConductorState::Terminated:
                stateStr = "Terminated";
                break;
            }
            ImGui::Text("Lifecycle State: %s", stateStr);
            ImGui::Text("Committed Transitions: %u",
                        boundConductor->getSuccessfulTransitionsCount());

            ImGui::Separator();
            if (state == framework::ConductorState::Active) {
                if (ImGui::Button("Request Transition")) {
                    (void)boundConductor->requestTransition();
                }
            } else if (state == framework::ConductorState::Prepared) {
                if (ImGui::Button("Execute Transition")) {
                    (void)boundConductor->executeTransition();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    boundConductor->cancelTransition();
                }
            } else if (state == framework::ConductorState::Preparing) {
                ImGui::Text("Preparing standby mode...");
                if (ImGui::Button("Cancel")) {
                    boundConductor->cancelTransition();
                }
            }
        } else {
            ImGui::TextDisabled("No active Conductor bound");
        }
    }
    ImGui::End();
}

void EditorEngine::Impl::drawDiagnosticPanel() {
    if (!config.showDiagnosticPanel) {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(430.0F, 120.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420.0F, 170.0F), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("HIKARI EDITOR", &config.showDiagnosticPanel)) {
        ImGui::TextColored(ImVec4(0.2F, 0.9F, 0.4F, 1.0F),
                           "Hikari Engine v0.1.0 — Visual Editor & IDE");
        ImGui::Separator();
        ImGui::Text("Vulkan RHI Backend: Active (Dynamic Rendering 1.3)");
        if (rhiDevice != nullptr) {
            ImGui::Text("Physical GPU: %s", rhiDevice->getCaps().deviceName.data());
        }
        ImGui::Text("Font Atlas: Built-in ProggyClean (RGBA32)");
        ImGui::Text("UI Backend: ImGui Vulkan Dynamic Pipeline");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.3F, 0.8F, 1.0F, 1.0F), "STATUS: ALL SYSTEMS OPERATIONAL");
    }
    ImGui::End();
}

// -----------------------------------------------------------------------------
// EditorEngine Public Implementation
// -----------------------------------------------------------------------------
EditorEngine::EditorEngine() : impl_(std::make_unique<Impl>()) {}

EditorEngine::~EditorEngine() {
    shutdown();
}

EditorEngine::EditorEngine(EditorEngine&& other) noexcept = default;
EditorEngine& EditorEngine::operator=(EditorEngine&& other) noexcept = default;

bool EditorEngine::initialize(rhi::RhiDevice& rhiDevice, const EditorConfig& config) {
    if (impl_->initialized) {
        return false;
    }

    impl_->rhiDevice = &rhiDevice;
    impl_->config = config;

    IMGUI_CHECKVERSION();
    impl_->imguiContext = ImGui::CreateContext();
    if (impl_->imguiContext == nullptr) {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // Disable ini persistence in headless/unit tests
    io.DisplaySize = ImVec2(1280.0F, 720.0F);
    io.DeltaTime = 1.0F / 60.0F;

    // Dark professional styling
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0F;
    style.FrameRounding = 3.0F;
    style.PopupRounding = 3.0F;
    style.ScrollbarRounding = 3.0F;
    style.GrabRounding = 3.0F;
    style.TabRounding = 3.0F;

    // Build default font atlas (required for headless / null RHI execution)
    unsigned char* fontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);

    auto nativeHandles = rhiDevice.getNativeDeviceHandles();
    if (nativeHandles.device != nullptr) {
        auto colorFormat = static_cast<VkFormat>(nativeHandles.colorAttachmentFormat);
        VkPipelineRenderingCreateInfoKHR renderingInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
            .pNext = nullptr,
            .viewMask = 0,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &colorFormat,
            .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
            .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
        };

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.Instance = static_cast<VkInstance>(nativeHandles.instance);
        initInfo.PhysicalDevice = static_cast<VkPhysicalDevice>(nativeHandles.physicalDevice);
        initInfo.Device = static_cast<VkDevice>(nativeHandles.device);
        initInfo.QueueFamily = nativeHandles.graphicsQueueFamily;
        initInfo.Queue = static_cast<VkQueue>(nativeHandles.graphicsQueue);
        initInfo.DescriptorPoolSize = 64;
        initInfo.MinImageCount = 2;
        initInfo.ImageCount = std::max(2U, nativeHandles.swapchainImageCount);
        initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.UseDynamicRendering = true;
        initInfo.PipelineRenderingCreateInfo = renderingInfo;

        if (!ImGui_ImplVulkan_Init(&initInfo)) {
            ENG_LOG_ERROR("Editor", "Failed to initialize ImGui Vulkan backend");
            ImGui::DestroyContext(impl_->imguiContext);
            impl_->imguiContext = nullptr;
            return false;
        }
        impl_->vulkanBackendInitialized = true;
        ENG_LOG_INFO("Editor", "Dear ImGui Vulkan backend initialized with Dynamic Rendering");
    } else {
        ENG_LOG_INFO("Editor", "Dear ImGui initialized in headless/null RHI mode");
    }

    impl_->initialized = true;
    ENG_LOG_INFO("Editor", "Dear ImGui editor engine initialized successfully");
    return true;
}

void EditorEngine::shutdown() {
    if (impl_->initialized) {
        if (impl_->vulkanBackendInitialized) {
            ImGui_ImplVulkan_Shutdown();
            impl_->vulkanBackendInitialized = false;
        }
        if (impl_->imguiContext != nullptr) {
            ImGui::DestroyContext(impl_->imguiContext);
            impl_->imguiContext = nullptr;
        }
        impl_->rhiDevice = nullptr;
        impl_->boundWorld = nullptr;
        impl_->boundConductor = nullptr;
        impl_->initialized = false;
        ENG_LOG_INFO("Editor", "Dear ImGui editor engine shut down");
    }
}

bool EditorEngine::isInitialized() const noexcept {
    return impl_->initialized;
}

void EditorEngine::beginFrame(Duration dt) {
    if (!impl_->initialized) {
        return;
    }

    ImGui::SetCurrentContext(impl_->imguiContext);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0F, 720.0F);
    float deltaSec = static_cast<float>(dt.toSeconds());
    io.DeltaTime = deltaSec > 0.0F ? deltaSec : (1.0F / 60.0F);

    if (impl_->vulkanBackendInitialized) {
        ImGui_ImplVulkan_NewFrame();
    }
    ImGui::NewFrame();
}

void EditorEngine::drawPanels() {
    if (!impl_->initialized) {
        return;
    }

    ImGui::SetCurrentContext(impl_->imguiContext);
    impl_->drawMainMenuBar();
    if (impl_->config.showToolbar) {
        impl_->drawToolbar();
    }
    if (impl_->config.showHierarchyPanel) {
        impl_->drawHierarchyPanel();
    }
    if (impl_->config.showEntityInspector) {
        impl_->drawInspectorPanel();
    }
    if (impl_->config.showViewportPanel) {
        impl_->drawViewportPanel();
    }
    if (impl_->config.showContentBrowser) {
        impl_->drawContentBrowser();
    }
    if (impl_->config.showConsolePanel) {
        impl_->drawConsolePanel();
    }
    if (impl_->config.showMetricsPanel) {
        impl_->drawMetricsPanel();
    }
    if (impl_->config.showConductorPanel) {
        impl_->drawConductorPanel();
    }
    if (impl_->config.showDiagnosticPanel) {
        impl_->drawDiagnosticPanel();
    }
    if (impl_->config.showDemoWindow) {
        ImGui::ShowDemoWindow(&impl_->config.showDemoWindow);
    }
}

void EditorEngine::endFrame(rhi::RhiCommandList& cmdList) {
    if (!impl_->initialized) {
        return;
    }

    ImGui::SetCurrentContext(impl_->imguiContext);
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData == nullptr) {
        return;
    }

    impl_->lastDrawCalls = 0;
    impl_->lastVertices = static_cast<uint32_t>(drawData->TotalVtxCount);
    impl_->lastIndices = static_cast<uint32_t>(drawData->TotalIdxCount);
    for (int n = 0; n < drawData->CmdListsCount; n++) {
        impl_->lastDrawCalls += static_cast<uint32_t>(drawData->CmdLists[n]->CmdBuffer.Size);
    }
    impl_->executedPasses++;

    if (impl_->vulkanBackendInitialized) {
        auto nativeCmd = cmdList.getNativeCommandListHandles();
        if (nativeCmd.commandBuffer != nullptr) {
            ImGui_ImplVulkan_RenderDrawData(drawData,
                                            static_cast<VkCommandBuffer>(nativeCmd.commandBuffer));
        }
    }
}

void EditorEngine::processInput(const platform::InputSnapshot& input) {
    if (!impl_->initialized) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(input.mouseX, input.mouseY);
    io.AddMouseButtonEvent(
        0, input.mouseButtons.test(static_cast<size_t>(platform::MouseButton::Left)));
    io.AddMouseButtonEvent(
        1, input.mouseButtons.test(static_cast<size_t>(platform::MouseButton::Right)));
    io.AddMouseButtonEvent(
        2, input.mouseButtons.test(static_cast<size_t>(platform::MouseButton::Middle)));
    io.AddMouseWheelEvent(0.0F, input.wheelDeltaY);
}

void EditorEngine::bindWorld(world::World* world) noexcept {
    impl_->boundWorld = world;
}

void EditorEngine::bindConductor(framework::Conductor* conductor) noexcept {
    impl_->boundConductor = conductor;
}

uint32_t EditorEngine::lastDrawCallCount() const noexcept {
    return impl_->lastDrawCalls;
}

uint32_t EditorEngine::lastVertexCount() const noexcept {
    return impl_->lastVertices;
}

uint32_t EditorEngine::lastIndexCount() const noexcept {
    return impl_->lastIndices;
}

uint64_t EditorEngine::executedEditorPasses() const noexcept {
    return impl_->executedPasses;
}

} // namespace eng::editor