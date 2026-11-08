/// @file viewport_panel.cpp
/// @brief Functional 3D scene viewport panel with perspective rendering, camera navigation, and
/// transform gizmos.

#include "panels/viewport_panel.h"

#include "core/log.h"
#include "editor/editor_engine.h"
#include "editor/editor_theme.h"
#include "editor/selection_model.h"
#include "framework/conductor.h"
#include "physics/physics_components.h"
#include "render/asset_manager.h"
#include "render/mesh_component.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <imgui.h>
#include <numbers>
#include <vector>

namespace eng::editor {

namespace {

enum class GizmoAxis : uint8_t { None = 0, X = 1, Y = 2, Z = 3, Center = 4 };

// Viewport state persistence
int s_viewMode = 0;      // 0 = Lit, 1 = Wireframe, 2 = Normals
int s_transformMode = 0; // 0 = Translate, 1 = Rotate, 2 = Scale
int s_snappingMode = 2;  // 0 = Off, 1 = 0.1m, 2 = 0.5m, 3 = 1.0m
int s_coordSpace = 0;    // 0 = World, 1 = Local
bool s_showGrid = true;
bool s_showStats = true;
bool s_useGpuRenderer = true; // True = GPU Viewport (SceneRenderer), False = CPU Software Rasterizer

// Active gizmo dragging state
bool s_isDraggingGizmo = false;
GizmoAxis s_activeGizmoAxis = GizmoAxis::None;
Vec3 s_dragStartEntityPos{Vec3::zero()};
Quat s_dragStartEntityRot{Quat::identity()};
Vec3 s_dragStartEntityScale{Vec3::one()};
Vec3 s_dragStartHitPoint{Vec3::zero()};

// Camera navigation tracking
bool s_isFlying = false;
static bool s_wasLooking = false;

float getSnapStep(int snapMode, int transformMode) noexcept {
    if (snapMode == 0) {
        return 0.0F; // Snapping off
    }
    if (transformMode == 1) {                     // Rotate
        return std::numbers::pi_v<float> / 12.0F; // 15 degrees
    }
    if (transformMode == 2) { // Scale
        return (snapMode == 1) ? 0.1F : ((snapMode == 2) ? 0.25F : 0.5F);
    }
    // Translate
    return (snapMode == 1) ? 0.1F : ((snapMode == 2) ? 0.5F : 1.0F);
}

float snapValue(float val, float step) noexcept {
    if (step <= 1e-5F) {
        return val;
    }
    return std::round(val / step) * step;
}

float distancePointToSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b) noexcept {
    const float l2 = ((b.x - a.x) * (b.x - a.x)) + ((b.y - a.y) * (b.y - a.y));
    if (l2 < 1e-4F) {
        const float dx = p.x - a.x;
        const float dy = p.y - a.y;
        return std::sqrt((dx * dx) + (dy * dy));
    }
    const float t =
        std::clamp((((p.x - a.x) * (b.x - a.x)) + ((p.y - a.y) * (b.y - a.y))) / l2, 0.0F, 1.0F);
    const float projX = a.x + (t * (b.x - a.x));
    const float projY = a.y + (t * (b.y - a.y));
    const float dx = p.x - projX;
    const float dy = p.y - projY;
    return std::sqrt((dx * dx) + (dy * dy));
}

} // namespace

void ViewportPanel::draw(EditorContext& ctx) {
    if (!isOpen_) {
        return;
    }

    ctx.editorCamera = &camera_;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    if (ImGui::Begin(getDisplayName().data(), &isOpen_,
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 screenPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        float topBarHeight = 34.0F;
        ImVec2 canvasPos(screenPos.x, screenPos.y + topBarHeight);
        ImVec2 canvasSize(avail.x, std::max(0.0F, avail.y - topBarHeight));

        if (canvasSize.x <= 1.0F || canvasSize.y <= 1.0F) {
            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }

        // Ensure offscreen render targets match current canvas dimensions BEFORE AddImage
        const uint32_t targetWidth = static_cast<uint32_t>(canvasSize.x);
        const uint32_t targetHeight = static_cast<uint32_t>(canvasSize.y);
        if (targetWidth != ctx.sceneRenderWidth || targetHeight != ctx.sceneRenderHeight) {
            if (ctx.editorEngine != nullptr) {
                ctx.editorEngine->resizeViewport(targetWidth, targetHeight);
            }
        }

        // Synchronize camera matrices with active viewport dimensions
        camera_.updateMatrices(canvasSize.x, canvasSize.y);
        ctx.viewportCameraViewProj = camera_.getViewProjectionMatrix();

        const Vec3 canvasOrigin3D{canvasPos.x, canvasPos.y, 0.0F};
        const Vec3 canvasSize3D{canvasSize.x, canvasSize.y, 0.0F};

        const bool isGpuAvailable = (ctx.gpuRenderingActive && ctx.viewportTextureId != nullptr);

        // 1. Viewport canvas background & GPU texture
        if (isGpuAvailable && s_useGpuRenderer) {
            drawList->AddImage(
                static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(ctx.viewportTextureId)),
                canvasPos,
                ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                ImVec2(0.0F, 0.0F),
                ImVec2(1.0F, 1.0F));
        } else {
            drawList->AddRectFilled(canvasPos,
                                    ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                                    IM_COL32(19, 23, 34, 255));
        }

        // 2. Invisible button over canvas to capture mouse input
        ImGui::SetCursorScreenPos(canvasPos);
        ImGui::InvisibleButton("##ViewportCanvasInteractive", canvasSize,
                               ImGuiButtonFlags_MouseButtonLeft |
                                   ImGuiButtonFlags_MouseButtonRight |
                                   ImGuiButtonFlags_MouseButtonMiddle);
        const bool isCanvasHovered = ImGui::IsItemHovered();
        const bool isCanvasActive = ImGui::IsItemActive();
        const ImGuiIO& io = ImGui::GetIO();

        // Drag and drop target for mesh assets dropped directly into the Viewport
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload("CONTENT_BROWSER_MESH")) {
                const char* meshPath = static_cast<const char*>(payload->Data);
                if (meshPath != nullptr && ctx.world != nullptr) {
                    std::filesystem::path p(meshPath);
                    auto h = ctx.world->createEntity(p.stem().string());
                    ctx.world->setComponent(
                        h, physics::PhysicsTransformComponent{
                               .transform = Transform{camera_.getPivot()},
                           });
                    const render::AssetId id = render::makeAssetIdFromPath(meshPath);
                    ctx.world->setComponent(h, render::MeshComponent{
                                                   .meshAsset = id,
                                                   .assetPath = meshPath,
                                                   .submeshIndex = 0,
                                                   .visible = true,
                                                   .castShadows = true,
                                               });

                    Vec3 boxHalfExtents{0.5F, 0.5F, 0.5F};
                    if (ctx.assetManager != nullptr) {
                        const auto* md = ctx.assetManager->getMeshData(id);
                        if (md == nullptr) {
                            (void)ctx.assetManager->loadMesh(meshPath);
                            md = ctx.assetManager->getMeshData(id);
                        }
                        if (md != nullptr && md->bounds.isValid()) {
                            boxHalfExtents = md->bounds.getSize() * 0.5F;
                        }
                    }
                    ctx.world->setComponent(
                        h, physics::ColliderComponent{
                               .desc = physics::ColliderDesc::makeBox(boxHalfExtents),
                           });

                    if (ctx.selection != nullptr) {
                        ctx.selection->select(h);
                    }
                    ctx.selectedEntityId = h.id;
                    ctx.markDirty();
                }
            }
            ImGui::EndDragDropTarget();
        }

        // ---------------------------------------------------------------------
        // Camera Navigation & Input Processing
        // ---------------------------------------------------------------------
        if (isCanvasHovered || isCanvasActive) {
            const bool isFlyMode = (camera_.getCameraMode() == EditorCameraMode::Fly);
            const bool isRmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Right) && !io.KeyShift && !s_isDraggingGizmo;

            // First-person look orientation: RMB drag
            if (isRmbDown) {
                if (!s_wasLooking) {
                    // Reset/suppress mouse delta on first frame to prevent sudden orientation jumps
                    s_wasLooking = true;
                } else if (io.MouseDelta.x != 0.0F || io.MouseDelta.y != 0.0F) {
                    camera_.look(io.MouseDelta.x, io.MouseDelta.y);
                }
            } else {
                s_wasLooking = false;
            }

            // Fly navigation active condition
            const bool canFlyMove = !io.WantTextInput && !s_isDraggingGizmo &&
                                    (isRmbDown || (isFlyMode && (isCanvasHovered || isCanvasActive)));
            s_isFlying = canFlyMove;

            // Scroll wheel: Adjust flight speed in Fly mode or while RMB looking; otherwise zoom
            if (io.MouseWheel != 0.0F && !s_isDraggingGizmo) {
                if (isRmbDown || isFlyMode) {
                    camera_.adjustMoveSpeed(io.MouseWheel);
                } else {
                    camera_.zoom(io.MouseWheel);
                }
            }

            // Orbit Navigation: Alt + Left Drag OR Middle Mouse Drag (when not flying)
            const bool isOrbiting =
                !isRmbDown && ((ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !io.KeyShift) ||
                               (ImGui::IsMouseDown(ImGuiMouseButton_Left) && io.KeyAlt && !s_isDraggingGizmo));
            if (isOrbiting && (io.MouseDelta.x != 0.0F || io.MouseDelta.y != 0.0F)) {
                camera_.orbit(io.MouseDelta.x * 0.005F, io.MouseDelta.y * 0.005F);
            }

            // Pan Navigation: Shift + Middle Mouse Drag OR Shift + Right Drag
            const bool isPanning =
                (ImGui::IsMouseDown(ImGuiMouseButton_Middle) && io.KeyShift) ||
                (ImGui::IsMouseDown(ImGuiMouseButton_Right) && io.KeyShift && !s_isFlying);
            if (isPanning && (io.MouseDelta.x != 0.0F || io.MouseDelta.y != 0.0F)) {
                camera_.pan(io.MouseDelta.x, io.MouseDelta.y);
            }

            // First-Person WASD + QE Fly Movement
            bool isActivelyMoving = false;
            if (canFlyMove) {
                Vec3 flyMovement{0.0F, 0.0F, 0.0F};
                if (ImGui::IsKeyDown(ImGuiKey_W)) {
                    flyMovement.z += 1.0F; // Forward
                }
                if (ImGui::IsKeyDown(ImGuiKey_S)) {
                    flyMovement.z -= 1.0F; // Backward
                }
                if (ImGui::IsKeyDown(ImGuiKey_A)) {
                    flyMovement.x -= 1.0F; // Left
                }
                if (ImGui::IsKeyDown(ImGuiKey_D)) {
                    flyMovement.x += 1.0F; // Right
                }
                if (ImGui::IsKeyDown(ImGuiKey_E)) {
                    flyMovement.y += 1.0F; // Up
                }
                if (ImGui::IsKeyDown(ImGuiKey_Q)) {
                    flyMovement.y -= 1.0F; // Down
                }

                if (flyMovement.x != 0.0F || flyMovement.y != 0.0F || flyMovement.z != 0.0F) {
                    isActivelyMoving = true;
                    float speedMult = 1.0F;
                    if (io.KeyShift) {
                        speedMult = EditorCamera::kFastSpeedMultiplier;
                    } else if (io.KeyCtrl) {
                        speedMult = EditorCamera::kSlowSpeedMultiplier;
                    }
                    camera_.fly(flyMovement, io.DeltaTime, speedMult);
                }
            }

            // Update navigation state
            camera_.setNavigating(isRmbDown || isActivelyMoving || isOrbiting || isPanning);

            // Focus framing shortcut: F key (disabled when typing or actively looking)
            if (ImGui::IsKeyPressed(ImGuiKey_F) && !io.WantTextInput && !isRmbDown) {
                if (ctx.selection != nullptr && !ctx.selection->empty() && ctx.world != nullptr) {
                    auto primary = ctx.selection->getPrimarySelected();
                    if (ctx.world->isAlive(primary) &&
                        ctx.world->hasComponent<physics::PhysicsTransformComponent>(primary)) {
                        const auto* pt =
                            ctx.world->getComponent<physics::PhysicsTransformComponent>(primary);
                        if (pt != nullptr) {
                            camera_.frame(pt->transform.position, 2.5F);
                        }
                    }
                } else {
                    camera_.frame(Vec3::zero(), 5.0F);
                }
            }

            // Gizmo mode shortcuts: W (Translate), E (Rotate), R (Scale)
            // Blocked when flying or in Fly mode or when typing
            if (!s_isFlying && !isFlyMode && !io.WantTextInput) {
                if (ImGui::IsKeyPressed(ImGuiKey_W)) {
                    s_transformMode = 0;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_E)) {
                    s_transformMode = 1;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_R)) {
                    s_transformMode = 2;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_G)) {
                    s_showGrid = !s_showGrid;
                }
            } else if (isFlyMode && !s_isFlying && !io.WantTextInput) {
                // In Fly mode when not actively moving, R and G shortcuts still work
                if (ImGui::IsKeyPressed(ImGuiKey_R)) {
                    s_transformMode = 2;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_G)) {
                    s_showGrid = !s_showGrid;
                }
            }
        } else {
            s_wasLooking = false;
            camera_.setNavigating(false);
        }

        // ---------------------------------------------------------------------
        // 3. Perspective 3D Ground Grid & Coordinate Axes
        // ---------------------------------------------------------------------
        if (s_showGrid) {
            constexpr int kGridExtent = 20;
            constexpr float kStep = 1.0F;

            // X-parallel and Z-parallel grid lines
            for (int i = -kGridExtent; i <= kGridExtent; ++i) {
                const float coord = static_cast<float>(i) * kStep;
                const bool isMajor = (i % 5 == 0);
                const bool isAxis = (i == 0);

                // Grid line along X axis (varying X, constant Z)
                Vec3 p1{-static_cast<float>(kGridExtent) * kStep, 0.0F, coord};
                Vec3 p2{static_cast<float>(kGridExtent) * kStep, 0.0F, coord};
                Vec3 s1{};
                Vec3 s2{};
                float d1 = 0.0F;
                float d2 = 0.0F;

                if (camera_.project(p1, canvasOrigin3D, canvasSize3D, s1, d1) &&
                    camera_.project(p2, canvasOrigin3D, canvasSize3D, s2, d2)) {
                    ImU32 col = isAxis    ? IM_COL32(239, 68, 68, 200) // Red X axis
                                : isMajor ? IM_COL32(50, 62, 88, 180)
                                          : IM_COL32(28, 36, 52, 110);
                    float thickness = isAxis ? 1.8F : (isMajor ? 1.2F : 1.0F);
                    drawList->AddLine(ImVec2(s1.x, s1.y), ImVec2(s2.x, s2.y), col, thickness);
                }

                // Grid line along Z axis (constant X, varying Z)
                Vec3 p3{coord, 0.0F, -static_cast<float>(kGridExtent) * kStep};
                Vec3 p4{coord, 0.0F, static_cast<float>(kGridExtent) * kStep};
                Vec3 s3{};
                Vec3 s4{};
                float d3 = 0.0F;
                float d4 = 0.0F;

                if (camera_.project(p3, canvasOrigin3D, canvasSize3D, s3, d3) &&
                    camera_.project(p4, canvasOrigin3D, canvasSize3D, s4, d4)) {
                    ImU32 col = isAxis    ? IM_COL32(59, 130, 246, 200) // Blue Z axis
                                : isMajor ? IM_COL32(50, 62, 88, 180)
                                          : IM_COL32(28, 36, 52, 110);
                    float thickness = isAxis ? 1.8F : (isMajor ? 1.2F : 1.0F);
                    drawList->AddLine(ImVec2(s3.x, s3.y), ImVec2(s4.x, s4.y), col, thickness);
                }
            }

            // Vertical Green Y-axis indicator at origin
            Vec3 y0{0.0F, 0.0F, 0.0F};
            Vec3 y1{0.0F, 10.0F, 0.0F};
            Vec3 sy0{};
            Vec3 sy1{};
            float dy0 = 0.0F;
            float dy1 = 0.0F;
            if (camera_.project(y0, canvasOrigin3D, canvasSize3D, sy0, dy0) &&
                camera_.project(y1, canvasOrigin3D, canvasSize3D, sy1, dy1)) {
                drawList->AddLine(ImVec2(sy0.x, sy0.y), ImVec2(sy1.x, sy1.y),
                                  IM_COL32(34, 197, 94, 220), 1.8F);
            }

            // Center origin crosshair
            Vec3 originScreen{};
            float originDepth = 0.0F;
            if (camera_.project(Vec3::zero(), canvasOrigin3D, canvasSize3D, originScreen,
                                originDepth)) {
                drawList->AddCircleFilled(ImVec2(originScreen.x, originScreen.y), 3.5F,
                                          IM_COL32(255, 255, 255, 220));
            }
        }

        // ---------------------------------------------------------------------
        // 4. 3D Scene Entity Rendering
        // ---------------------------------------------------------------------
        world::EntityHandle primarySelectedHandle{};
        Transform primarySelectedTransform{};
        bool hasPrimarySelected = false;

        if (ctx.world != nullptr) {
            flecs::world& ecs = ctx.world->getRawWorld();
            auto aliveHandles = ctx.world->getAllEntities();

            // Directional key light vector (normalized from top-right-front)
            const Vec3 lightDir = Vec3{0.577F, 0.707F, 0.408F}.normalized();

            for (auto h : aliveHandles) {
                auto e = ecs.entity(h.id);
                if (!e.is_alive() || !e.has<physics::PhysicsTransformComponent>()) {
                    continue;
                }

                const auto* ptc = e.get<physics::PhysicsTransformComponent>();
                if (ptc == nullptr) {
                    continue;
                }

                const Transform& tr = ptc->transform;
                const bool isSelected = (ctx.selection != nullptr && ctx.selection->isSelected(h));
                if (isSelected && !hasPrimarySelected) {
                    primarySelectedHandle = h;
                    primarySelectedTransform = tr;
                    hasPrimarySelected = true;
                }

                // Determine entity geometry dimensions
                Vec3 halfExtents{0.5F, 0.5F, 0.5F};
                bool isSphere = false;
                float sphereRadius = 0.5F;
                bool hasCustomMesh = false;
                float tagYOffset = 0.9F;

                if (e.has<render::MeshComponent>() && ctx.assetManager != nullptr) {
                    const auto* mc = e.get<render::MeshComponent>();
                    if (mc != nullptr && mc->visible) {
                        const render::MeshData* meshData =
                            ctx.assetManager->getMeshData(mc->meshAsset);
                        if (meshData == nullptr && !mc->assetPath.empty()) {
                            (void)ctx.assetManager->loadMesh(mc->assetPath);
                            meshData = ctx.assetManager->getMeshData(mc->assetPath);
                        }

                        if (meshData != nullptr && !meshData->vertices.empty() &&
                            !meshData->indices.empty()) {
                            hasCustomMesh = true;
                            tagYOffset = (meshData->bounds.maxBound.y * tr.scale.y) + 0.4F;

                            // CPU Custom Mesh Software Rasterizer (Preserved Intact as Fallback)
                            if (!isGpuAvailable || !s_useGpuRenderer) {
                                // Cache projected screen vertices for this mesh instance
                                std::vector<Vec3> screenVertices(meshData->vertices.size());
                                std::vector<float> vertexDepths(meshData->vertices.size());
                                std::vector<bool> vertexVisible(meshData->vertices.size(), false);

                                for (size_t vi = 0; vi < meshData->vertices.size(); ++vi) {
                                    const Vec3 worldPos =
                                        tr.position +
                                        tr.rotation.rotate(meshData->vertices[vi].position * tr.scale);
                                    vertexVisible[vi] =
                                        camera_.project(worldPos, canvasOrigin3D, canvasSize3D,
                                                        screenVertices[vi], vertexDepths[vi]);
                                }

                                const size_t triCount = meshData->indices.size() / 3;
                                for (size_t ti = 0; ti < triCount; ++ti) {
                                    const uint32_t i0 = meshData->indices[ti * 3 + 0];
                                    const uint32_t i1 = meshData->indices[ti * 3 + 1];
                                    const uint32_t i2 = meshData->indices[ti * 3 + 2];

                                    if (i0 < meshData->vertices.size() &&
                                        i1 < meshData->vertices.size() &&
                                        i2 < meshData->vertices.size()) {
                                        if (vertexVisible[i0] || vertexVisible[i1] ||
                                            vertexVisible[i2]) {
                                            const Vec3 w0 =
                                                tr.position +
                                                tr.rotation.rotate(meshData->vertices[i0].position *
                                                                   tr.scale);
                                            const Vec3 w1 =
                                                tr.position +
                                                tr.rotation.rotate(meshData->vertices[i1].position *
                                                                   tr.scale);
                                            const Vec3 w2 =
                                                tr.position +
                                                tr.rotation.rotate(meshData->vertices[i2].position *
                                                                   tr.scale);

                                            const Vec3 triNormal =
                                                Vec3::cross(w1 - w0, w2 - w0).normalized();
                                            const Vec3 triCenter = (w0 + w1 + w2) * (1.0F / 3.0F);
                                            const Vec3 toCamera =
                                                (camera_.getPosition() - triCenter).normalized();

                                            if (Vec3::dot(triNormal, toCamera) > 0.0F) {
                                                const ImVec2 p0(screenVertices[i0].x,
                                                                screenVertices[i0].y);
                                                const ImVec2 p1(screenVertices[i1].x,
                                                                screenVertices[i1].y);
                                                const ImVec2 p2(screenVertices[i2].x,
                                                                screenVertices[i2].y);

                                                if (s_viewMode != 1) { // Lit mode
                                                    float diffuse =
                                                        std::max(0.0F, Vec3::dot(triNormal, lightDir));
                                                    float lightFactor = 0.35F + (0.65F * diffuse);

                                                    ImU32 triColor{};
                                                    if (isSelected) {
                                                        triColor = IM_COL32(
                                                            static_cast<int>(40.0F * lightFactor),
                                                            static_cast<int>(140.0F * lightFactor),
                                                            static_cast<int>(210.0F * lightFactor),
                                                            235);
                                                    } else {
                                                        triColor = IM_COL32(
                                                            static_cast<int>(80.0F * lightFactor),
                                                            static_cast<int>(95.0F * lightFactor),
                                                            static_cast<int>(125.0F * lightFactor),
                                                            220);
                                                    }
                                                    drawList->AddTriangleFilled(p0, p1, p2, triColor);
                                                }

                                                // Wireframe edges
                                                const ImU32 edgeColor =
                                                    isSelected ? IM_COL32(56, 189, 248, 255)
                                                               : IM_COL32(140, 160, 190, 160);
                                                drawList->AddTriangle(p0, p1, p2, edgeColor,
                                                                      isSelected ? 1.4F : 0.8F);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                if (e.has<physics::ColliderComponent>()) {
                    const auto* cc = e.get<physics::ColliderComponent>();
                    if (cc != nullptr) {
                        if (cc->desc.type == physics::ColliderType::Sphere) {
                            isSphere = true;
                            sphereRadius = cc->desc.radius *
                                           std::max(tr.scale.x, std::max(tr.scale.y, tr.scale.z));
                        } else {
                            halfExtents = cc->desc.halfExtents * tr.scale;
                        }
                    }
                } else {
                    halfExtents = halfExtents * tr.scale;
                }

                if (!hasCustomMesh) {
                    tagYOffset = halfExtents.y + 0.4F;
                    if (isSphere) {
                        // Render 3D Wireframe / Shaded Sphere
                        Vec3 sphereCenterScreen{};
                        float sphereDepth = 0.0F;
                        if (camera_.project(tr.position, canvasOrigin3D, canvasSize3D,
                                            sphereCenterScreen, sphereDepth)) {
                            // Project radius onto screen
                            Vec3 edgePoint = tr.position + (camera_.getRight() * sphereRadius);
                            Vec3 edgeScreen{};
                            float edgeDepth = 0.0F;
                            float screenRadius = 15.0F;
                            if (camera_.project(edgePoint, canvasOrigin3D, canvasSize3D, edgeScreen,
                                                edgeDepth)) {
                                screenRadius =
                                    std::max(3.0F, std::abs(edgeScreen.x - sphereCenterScreen.x));
                            }

                            ImU32 sphereCol = isSelected ? IM_COL32(56, 189, 248, 160)
                                                         : IM_COL32(96, 165, 250, 120);
                            ImU32 outlineCol = isSelected ? IM_COL32(56, 189, 248, 255)
                                                          : IM_COL32(147, 197, 253, 200);

                            if (s_viewMode != 1) { // Lit / Normals
                                drawList->AddCircleFilled(
                                    ImVec2(sphereCenterScreen.x, sphereCenterScreen.y),
                                    screenRadius, sphereCol, 24);
                            }
                            drawList->AddCircle(ImVec2(sphereCenterScreen.x, sphereCenterScreen.y),
                                                screenRadius, outlineCol, 24,
                                                isSelected ? 2.0F : 1.2F);

                            // Equatorial and meridian wireframe ellipses
                            constexpr int kRingSegments = 24;
                            ImVec2 ringPoints[kRingSegments];
                            for (int seg = 0; seg < kRingSegments; ++seg) {
                                float theta =
                                    (static_cast<float>(seg) / static_cast<float>(kRingSegments)) *
                                    (2.0F * std::numbers::pi_v<float>);
                                Vec3 pt =
                                    tr.position +
                                    (Vec3{std::cos(theta), 0.0F, std::sin(theta)} * sphereRadius);
                                Vec3 s{};
                                float d = 0.0F;
                                (void)camera_.project(pt, canvasOrigin3D, canvasSize3D, s, d);
                                ringPoints[seg] = ImVec2(s.x, s.y);
                            }
                            drawList->AddPolyline(ringPoints, kRingSegments, outlineCol,
                                                  ImDrawFlags_Closed, 1.0F);
                        }
                    } else {
                        // Render 3D Box / OBB
                        const Vec3 localCorners[8] = {
                            Vec3{-halfExtents.x, -halfExtents.y, -halfExtents.z},
                            Vec3{halfExtents.x, -halfExtents.y, -halfExtents.z},
                            Vec3{halfExtents.x, halfExtents.y, -halfExtents.z},
                            Vec3{-halfExtents.x, halfExtents.y, -halfExtents.z},
                            Vec3{-halfExtents.x, -halfExtents.y, halfExtents.z},
                            Vec3{halfExtents.x, -halfExtents.y, halfExtents.z},
                            Vec3{halfExtents.x, halfExtents.y, halfExtents.z},
                            Vec3{-halfExtents.x, halfExtents.y, halfExtents.z},
                        };

                        Vec3 worldCorners[8];
                        Vec3 screenCorners[8];
                        float depths[8];
                        bool allVisible = true;

                        for (size_t c = 0; c < 8; ++c) {
                            worldCorners[c] = tr.position + tr.rotation.rotate(localCorners[c]);
                            if (!camera_.project(worldCorners[c], canvasOrigin3D, canvasSize3D,
                                                 screenCorners[c], depths[c])) {
                                allVisible = false;
                            }
                        }

                        if (allVisible) {
                            struct Face {
                                int idx[4];
                                Vec3 normal;
                            };

                            const Face faces[6] = {
                                {{4, 5, 6, 7}, tr.rotation.rotate(Vec3::unitZ())},  // +Z Front
                                {{1, 0, 3, 2}, tr.rotation.rotate(-Vec3::unitZ())}, // -Z Back
                                {{3, 2, 6, 7}, tr.rotation.rotate(Vec3::unitY())},  // +Y Top
                                {{0, 1, 5, 4}, tr.rotation.rotate(-Vec3::unitY())}, // -Y Bottom
                                {{1, 5, 6, 2}, tr.rotation.rotate(Vec3::unitX())},  // +X Right
                                {{0, 4, 7, 3}, tr.rotation.rotate(-Vec3::unitX())}, // -X Left
                            };

                            for (const auto& face : faces) {
                                const Vec3 faceCenter =
                                    (worldCorners[face.idx[0]] + worldCorners[face.idx[1]] +
                                     worldCorners[face.idx[2]] + worldCorners[face.idx[3]]) *
                                    0.25F;
                                const Vec3 toCamera =
                                    (camera_.getPosition() - faceCenter).normalized();
                                const float dotCamera = Vec3::dot(face.normal, toCamera);

                                if (dotCamera > 0.0F) {
                                    ImVec2 poly[4] = {
                                        ImVec2(screenCorners[face.idx[0]].x,
                                               screenCorners[face.idx[0]].y),
                                        ImVec2(screenCorners[face.idx[1]].x,
                                               screenCorners[face.idx[1]].y),
                                        ImVec2(screenCorners[face.idx[2]].x,
                                               screenCorners[face.idx[2]].y),
                                        ImVec2(screenCorners[face.idx[3]].x,
                                               screenCorners[face.idx[3]].y),
                                    };

                                    if (s_viewMode != 1) { // Lit mode
                                        float diffuse =
                                            std::max(0.0F, Vec3::dot(face.normal, lightDir));
                                        float lightFactor = 0.35F + (0.65F * diffuse);

                                        ImU32 faceColor{};
                                        if (isSelected) {
                                            faceColor = IM_COL32(
                                                static_cast<int>(30.0F * lightFactor),
                                                static_cast<int>(120.0F * lightFactor),
                                                static_cast<int>(180.0F * lightFactor), 210);
                                        } else {
                                            faceColor = IM_COL32(
                                                static_cast<int>(70.0F * lightFactor),
                                                static_cast<int>(85.0F * lightFactor),
                                                static_cast<int>(115.0F * lightFactor), 200);
                                        }
                                        drawList->AddConvexPolyFilled(poly, 4, faceColor);
                                    }

                                    ImU32 edgeColor = isSelected ? IM_COL32(56, 189, 248, 255)
                                                                 : IM_COL32(130, 150, 180, 180);
                                    drawList->AddPolyline(poly, 4, edgeColor, ImDrawFlags_Closed,
                                                          isSelected ? 1.8F : 1.0F);
                                }
                            }
                        }
                    }
                } else if (e.has<physics::ColliderComponent>()) {
                    // Draw collider shape as a wireframe overlay so collision hull is visible
                    const Vec3 localCorners[8] = {
                        Vec3{-halfExtents.x, -halfExtents.y, -halfExtents.z},
                        Vec3{halfExtents.x, -halfExtents.y, -halfExtents.z},
                        Vec3{halfExtents.x, halfExtents.y, -halfExtents.z},
                        Vec3{-halfExtents.x, halfExtents.y, -halfExtents.z},
                        Vec3{-halfExtents.x, -halfExtents.y, halfExtents.z},
                        Vec3{halfExtents.x, -halfExtents.y, halfExtents.z},
                        Vec3{halfExtents.x, halfExtents.y, halfExtents.z},
                        Vec3{-halfExtents.x, halfExtents.y, halfExtents.z},
                    };
                    Vec3 screenCorners[8];
                    float depths[8];
                    bool allVis = true;
                    for (size_t c = 0; c < 8; ++c) {
                        const Vec3 wc = tr.position + tr.rotation.rotate(localCorners[c]);
                        if (!camera_.project(wc, canvasOrigin3D, canvasSize3D, screenCorners[c],
                                             depths[c])) {
                            allVis = false;
                        }
                    }
                    if (allVis) {
                        const int edges[12][2] = {
                            {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                            {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
                        };
                        const ImU32 colWire = IM_COL32(34, 197, 94, 180); // Green wireframe
                        for (const auto& edge : edges) {
                            drawList->AddLine(
                                ImVec2(screenCorners[edge[0]].x, screenCorners[edge[0]].y),
                                ImVec2(screenCorners[edge[1]].x, screenCorners[edge[1]].y), colWire,
                                1.0F);
                        }
                    }
                }

                // Entity name tag when selected
                if (isSelected) {
                    Vec3 tagPos = tr.position + Vec3{0.0F, tagYOffset, 0.0F};
                    Vec3 tagScreen{};
                    float tagDepth = 0.0F;
                    if (camera_.project(tagPos, canvasOrigin3D, canvasSize3D, tagScreen,
                                        tagDepth)) {
                        const char* name = e.name().c_str();
                        ImVec2 textSize = ImGui::CalcTextSize(name);
                        ImVec2 textMin(tagScreen.x - (textSize.x * 0.5F) - 4.0F,
                                       tagScreen.y - (textSize.y * 0.5F) - 2.0F);
                        ImVec2 textMax(tagScreen.x + (textSize.x * 0.5F) + 4.0F,
                                       tagScreen.y + (textSize.y * 0.5F) + 2.0F);
                        drawList->AddRectFilled(textMin, textMax, IM_COL32(15, 23, 42, 220), 3.0F);
                        drawList->AddRect(textMin, textMax, IM_COL32(56, 189, 248, 200), 3.0F);
                        drawList->AddText(ImVec2(tagScreen.x - (textSize.x * 0.5F),
                                                 tagScreen.y - (textSize.y * 0.5F)),
                                          IM_COL32(255, 255, 255, 255), name);
                    }
                }
            }
        }

        // ---------------------------------------------------------------------
        // 5. Interactive 3D Transform Gizmos (Translate / Rotate / Scale)
        // ---------------------------------------------------------------------
        GizmoAxis hoveredAxis = GizmoAxis::None;

        if (hasPrimarySelected && ctx.world != nullptr) {
            const Vec3 gizmoCenter = primarySelectedTransform.position;
            Vec3 centerScreen{};
            float centerDepth = 0.0F;

            if (camera_.project(gizmoCenter, canvasOrigin3D, canvasSize3D, centerScreen,
                                centerDepth)) {
                // Adaptive scale so gizmo remains visually constant size on screen
                const float distToCam = (camera_.getPosition() - gizmoCenter).length();
                const float gizmoWorldLength = std::max(0.5F, distToCam * 0.16F);

                // Determine axes based on World vs Local coordinate frame
                Vec3 axisX = Vec3::unitX();
                Vec3 axisY = Vec3::unitY();
                Vec3 axisZ = Vec3::unitZ();
                if (s_coordSpace == 1) { // Local
                    axisX = primarySelectedTransform.rotation.rotate(Vec3::unitX());
                    axisY = primarySelectedTransform.rotation.rotate(Vec3::unitY());
                    axisZ = primarySelectedTransform.rotation.rotate(Vec3::unitZ());
                }

                Vec3 tipX = gizmoCenter + (axisX * gizmoWorldLength);
                Vec3 tipY = gizmoCenter + (axisY * gizmoWorldLength);
                Vec3 tipZ = gizmoCenter + (axisZ * gizmoWorldLength);

                Vec3 screenTipX{};
                Vec3 screenTipY{};
                Vec3 screenTipZ{};
                float depthX = 0.0F;
                float depthY = 0.0F;
                float depthZ = 0.0F;

                const bool validX =
                    camera_.project(tipX, canvasOrigin3D, canvasSize3D, screenTipX, depthX);
                const bool validY =
                    camera_.project(tipY, canvasOrigin3D, canvasSize3D, screenTipY, depthY);
                const bool validZ =
                    camera_.project(tipZ, canvasOrigin3D, canvasSize3D, screenTipZ, depthZ);

                const ImVec2 mousePos = io.MousePos;
                const ImVec2 cPos(centerScreen.x, centerScreen.y);

                // Detect axis hover
                if (!s_isDraggingGizmo && isCanvasHovered) {
                    if (validX && distancePointToSegment(
                                      mousePos, cPos, ImVec2(screenTipX.x, screenTipX.y)) < 9.0F) {
                        hoveredAxis = GizmoAxis::X;
                    } else if (validY &&
                               distancePointToSegment(mousePos, cPos,
                                                      ImVec2(screenTipY.x, screenTipY.y)) < 9.0F) {
                        hoveredAxis = GizmoAxis::Y;
                    } else if (validZ &&
                               distancePointToSegment(mousePos, cPos,
                                                      ImVec2(screenTipZ.x, screenTipZ.y)) < 9.0F) {
                        hoveredAxis = GizmoAxis::Z;
                    }
                }

                // Handle Gizmo Dragging Initiation
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && isCanvasHovered &&
                    hoveredAxis != GizmoAxis::None && !io.KeyAlt) {
                    s_isDraggingGizmo = true;
                    s_activeGizmoAxis = hoveredAxis;
                    s_dragStartEntityPos = primarySelectedTransform.position;
                    s_dragStartEntityRot = primarySelectedTransform.rotation;
                    s_dragStartEntityScale = primarySelectedTransform.scale;

                    const Ray mouseRay = camera_.unproject(Vec3{mousePos.x, mousePos.y, 0.0F},
                                                           canvasOrigin3D, canvasSize3D);
                    auto tPlane = mouseRay.intersectPlane(gizmoCenter, -camera_.getForward());
                    if (tPlane.has_value()) {
                        s_dragStartHitPoint = mouseRay.getPoint(*tPlane);
                    }
                }

                // Handle Gizmo Dragging Execution
                if (s_isDraggingGizmo && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    hoveredAxis = s_activeGizmoAxis;
                    const Ray mouseRay = camera_.unproject(Vec3{mousePos.x, mousePos.y, 0.0F},
                                                           canvasOrigin3D, canvasSize3D);

                    Vec3 constraintAxis = (s_activeGizmoAxis == GizmoAxis::X)   ? axisX
                                          : (s_activeGizmoAxis == GizmoAxis::Y) ? axisY
                                                                                : axisZ;

                    auto tPlane = mouseRay.intersectPlane(gizmoCenter, -camera_.getForward());
                    if (tPlane.has_value()) {
                        const Vec3 currentHit = mouseRay.getPoint(*tPlane);
                        const Vec3 hitDelta = currentHit - s_dragStartHitPoint;
                        const float snapStep = getSnapStep(s_snappingMode, s_transformMode);

                        auto selectedEntities = ctx.selection->getSelectedEntities();

                        if (s_transformMode == 0) { // Translate
                            float projectedDelta = Vec3::dot(hitDelta, constraintAxis);
                            if (snapStep > 0.0F) {
                                projectedDelta = snapValue(projectedDelta, snapStep);
                            }
                            const Vec3 moveVec = constraintAxis * projectedDelta;

                            for (auto h : selectedEntities) {
                                if (ctx.world->isAlive(h) &&
                                    ctx.world->hasComponent<physics::PhysicsTransformComponent>(
                                        h)) {
                                    auto* tc = ctx.world->getMutableComponent<
                                        physics::PhysicsTransformComponent>(h);
                                    if (tc != nullptr) {
                                        tc->transform.position = s_dragStartEntityPos + moveVec;
                                    }
                                }
                            }
                            ctx.markDirty();
                        } else if (s_transformMode == 1) { // Rotate
                            float rotAngle = (io.MouseDelta.x + io.MouseDelta.y) * 0.02F;
                            if (snapStep > 0.0F) {
                                rotAngle = snapValue(rotAngle, snapStep);
                            }
                            const Quat deltaRot = Quat::fromAxisAngle(constraintAxis, rotAngle);

                            for (auto h : selectedEntities) {
                                if (ctx.world->isAlive(h) &&
                                    ctx.world->hasComponent<physics::PhysicsTransformComponent>(
                                        h)) {
                                    auto* tc = ctx.world->getMutableComponent<
                                        physics::PhysicsTransformComponent>(h);
                                    if (tc != nullptr) {
                                        tc->transform.rotation =
                                            (deltaRot * s_dragStartEntityRot).normalized();
                                    }
                                }
                            }
                            ctx.markDirty();
                        } else if (s_transformMode == 2) { // Scale
                            float scaleDelta = Vec3::dot(hitDelta, constraintAxis) * 0.5F;
                            if (snapStep > 0.0F) {
                                scaleDelta = snapValue(scaleDelta, snapStep);
                            }

                            for (auto h : selectedEntities) {
                                if (ctx.world->isAlive(h) &&
                                    ctx.world->hasComponent<physics::PhysicsTransformComponent>(
                                        h)) {
                                    auto* tc = ctx.world->getMutableComponent<
                                        physics::PhysicsTransformComponent>(h);
                                    if (tc != nullptr) {
                                        Vec3 newScale = s_dragStartEntityScale;
                                        if (s_activeGizmoAxis == GizmoAxis::X) {
                                            newScale.x = std::max(0.05F, newScale.x + scaleDelta);
                                        } else if (s_activeGizmoAxis == GizmoAxis::Y) {
                                            newScale.y = std::max(0.05F, newScale.y + scaleDelta);
                                        } else {
                                            newScale.z = std::max(0.05F, newScale.z + scaleDelta);
                                        }
                                        tc->transform.scale = newScale;
                                    }
                                }
                            }
                            ctx.markDirty();
                        }
                    }
                } else if (s_isDraggingGizmo && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    s_isDraggingGizmo = false;
                    s_activeGizmoAxis = GizmoAxis::None;
                }

                // Render Gizmo Visual Handles
                const ImU32 colX = (hoveredAxis == GizmoAxis::X) ? IM_COL32(250, 204, 21, 255)
                                                                 : IM_COL32(239, 68, 68, 255);
                const ImU32 colY = (hoveredAxis == GizmoAxis::Y) ? IM_COL32(250, 204, 21, 255)
                                                                 : IM_COL32(34, 197, 94, 255);
                const ImU32 colZ = (hoveredAxis == GizmoAxis::Z) ? IM_COL32(250, 204, 21, 255)
                                                                 : IM_COL32(59, 130, 246, 255);

                const float thX = (hoveredAxis == GizmoAxis::X) ? 3.5F : 2.2F;
                const float thY = (hoveredAxis == GizmoAxis::Y) ? 3.5F : 2.2F;
                const float thZ = (hoveredAxis == GizmoAxis::Z) ? 3.5F : 2.2F;

                if (validX) {
                    drawList->AddLine(cPos, ImVec2(screenTipX.x, screenTipX.y), colX, thX);
                    drawList->AddCircleFilled(ImVec2(screenTipX.x, screenTipX.y), 4.5F, colX);
                }
                if (validY) {
                    drawList->AddLine(cPos, ImVec2(screenTipY.x, screenTipY.y), colY, thY);
                    drawList->AddCircleFilled(ImVec2(screenTipY.x, screenTipY.y), 4.5F, colY);
                }
                if (validZ) {
                    drawList->AddLine(cPos, ImVec2(screenTipZ.x, screenTipZ.y), colZ, thZ);
                    drawList->AddCircleFilled(ImVec2(screenTipZ.x, screenTipZ.y), 4.5F, colZ);
                }
                drawList->AddCircleFilled(cPos, 4.0F, IM_COL32(240, 240, 240, 255));
            }
        }

        // ---------------------------------------------------------------------
        // 6. Viewport Ray-Picking (Selection Through Viewport)
        // ---------------------------------------------------------------------
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && isCanvasHovered && !io.KeyAlt &&
            hoveredAxis == GizmoAxis::None && !s_isDraggingGizmo && ctx.world != nullptr &&
            ctx.selection != nullptr) {
            const Ray pickRay = camera_.unproject(Vec3{io.MousePos.x, io.MousePos.y, 0.0F},
                                                  canvasOrigin3D, canvasSize3D);

            flecs::world& ecs = ctx.world->getRawWorld();
            auto aliveHandles = ctx.world->getAllEntities();

            world::EntityHandle closestHitHandle{};
            float closestDist = 1e30F;

            for (auto h : aliveHandles) {
                auto e = ecs.entity(h.id);
                if (!e.is_alive() || !e.has<physics::PhysicsTransformComponent>()) {
                    continue;
                }

                const auto* ptc = e.get<physics::PhysicsTransformComponent>();
                if (ptc == nullptr) {
                    continue;
                }

                const Transform& tr = ptc->transform;

                // Test Ray intersection against MeshComponent bounds if present
                if (e.has<render::MeshComponent>() && ctx.assetManager != nullptr) {
                    const auto* mc = e.get<render::MeshComponent>();
                    if (mc != nullptr && mc->visible) {
                        const render::MeshData* meshData =
                            ctx.assetManager->getMeshData(mc->meshAsset);
                        if (meshData == nullptr && !mc->assetPath.empty()) {
                            meshData = ctx.assetManager->getMeshData(mc->assetPath);
                        }
                        if (meshData != nullptr) {
                            const Quat invRot = tr.rotation.inverse();
                            const Vec3 safeScale{
                                std::max(0.0001F, std::abs(tr.scale.x)),
                                std::max(0.0001F, std::abs(tr.scale.y)),
                                std::max(0.0001F, std::abs(tr.scale.z)),
                            };
                            const Vec3 localOrigin =
                                invRot.rotate(pickRay.origin - tr.position) / safeScale;
                            const Vec3 localDir =
                                (invRot.rotate(pickRay.direction) / safeScale).normalized();
                            const Ray localRay(localOrigin, localDir);

                            auto tHit = localRay.intersectAABB(meshData->bounds);
                            if (tHit.has_value() && *tHit < closestDist) {
                                closestDist = *tHit;
                                closestHitHandle = h;
                            }
                            continue;
                        }
                    }
                }

                // Test Ray intersection against entity bounding volume
                if (e.has<physics::ColliderComponent>()) {
                    const auto* cc = e.get<physics::ColliderComponent>();
                    if (cc != nullptr && cc->desc.type == physics::ColliderType::Sphere) {
                        float radius = cc->desc.radius *
                                       std::max(tr.scale.x, std::max(tr.scale.y, tr.scale.z));
                        auto tHit = pickRay.intersectSphere(tr.position, radius);
                        if (tHit.has_value() && *tHit < closestDist) {
                            closestDist = *tHit;
                            closestHitHandle = h;
                        }
                        continue;
                    }
                }

                // Default OBB / AABB Ray Test
                Vec3 halfExtents{0.5F, 0.5F, 0.5F};
                if (e.has<physics::ColliderComponent>()) {
                    const auto* cc = e.get<physics::ColliderComponent>();
                    if (cc != nullptr) {
                        halfExtents = cc->desc.halfExtents;
                    }
                }
                halfExtents = halfExtents * tr.scale;

                // Transform ray into entity local space
                const Quat invRot = tr.rotation.inverse();
                const Vec3 localOrigin = invRot.rotate(pickRay.origin - tr.position);
                const Vec3 localDir = invRot.rotate(pickRay.direction).normalized();
                const Ray localRay(localOrigin, localDir);

                auto tHit = localRay.intersectAABB(-halfExtents, halfExtents);
                if (tHit.has_value() && *tHit < closestDist) {
                    closestDist = *tHit;
                    closestHitHandle = h;
                }
            }

            if (closestHitHandle.id != 0) {
                const bool isMulti = (io.KeyShift || io.KeyCtrl);
                ctx.selection->select(closestHitHandle, isMulti);
                ctx.selectedEntityId = closestHitHandle.id;
            } else {
                if (!io.KeyShift && !io.KeyCtrl) {
                    ctx.selection->clear();
                    ctx.selectedEntityId = 0;
                }
            }
        }

        // ---------------------------------------------------------------------
        // 7. Embedded Studio Top Toolbar
        // ---------------------------------------------------------------------
        ImGui::SetCursorPos(ImVec2(6.0F, 5.0F));
        const char* viewModes[] = {"Lit", "Wireframe", "Normals"};
        ImGui::SetNextItemWidth(85.0F);
        ImGui::Combo("##ViewModeCombo", &s_viewMode, viewModes, 3);

        ImGui::SameLine();
        const bool isPersp = (camera_.getProjectionMode() == CameraProjectionMode::Perspective);
        if (ImGui::Button(isPersp ? "Perspective" : "Orthographic")) {
            camera_.setProjectionMode(isPersp ? CameraProjectionMode::Orthographic
                                              : CameraProjectionMode::Perspective);
        }

        ImGui::SameLine();
        const bool isFlyMode = (camera_.getCameraMode() == EditorCameraMode::Fly);
        if (ImGui::Button(isFlyMode ? "Cam: Fly" : "Cam: Orbit")) {
            camera_.setCameraMode(isFlyMode ? EditorCameraMode::Orbit : EditorCameraMode::Fly);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Toggle Camera Mode (Current: %s)\n"
                "- Orbit: RMB + WASD Fly, MMB/Alt+LMB Orbit, Shift+MMB Pan, Scroll Zoom\n"
                "- Fly: First-person WASD + QE Fly, RMB Look, Scroll adjusts speed",
                isFlyMode ? "Fly" : "Orbit");
        }

        ImGui::SameLine();
        float currentSpeed = camera_.getMoveSpeed();
        ImGui::SetNextItemWidth(75.0F);
        if (ImGui::DragFloat("##CamSpeed", &currentSpeed, 0.2F, EditorCamera::kMinMoveSpeed,
                             EditorCamera::kMaxMoveSpeed, "%.1f m/s")) {
            camera_.setMoveSpeed(currentSpeed);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Base Fly Speed (Shift: 4x, Ctrl: 0.2x, Scroll wheel in Fly mode)");
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        const char* gizmoModes[] = {"Translate (W)", "Rotate (E)", "Scale (R)"};
        ImGui::SetNextItemWidth(115.0F);
        ImGui::Combo("##GizmoCombo", &s_transformMode, gizmoModes, 3);

        ImGui::SameLine();
        if (ImGui::Button(s_coordSpace == 0 ? "World" : "Local")) {
            s_coordSpace = (s_coordSpace == 0) ? 1 : 0;
        }

        ImGui::SameLine();
        const char* snapModes[] = {"Snap: Off", "Snap: 0.1m", "Snap: 0.5m", "Snap: 1.0m"};
        ImGui::SetNextItemWidth(100.0F);
        ImGui::Combo("##SnapCombo", &s_snappingMode, snapModes, 4);

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        if (ImGui::Button(s_showGrid ? "Grid: ON" : "Grid: OFF")) {
            s_showGrid = !s_showGrid;
        }

        ImGui::SameLine();
        if (ImGui::Button("Frame (F)")) {
            if (ctx.selection != nullptr && !ctx.selection->empty() && ctx.world != nullptr) {
                auto primary = ctx.selection->getPrimarySelected();
                if (ctx.world->isAlive(primary) &&
                    ctx.world->hasComponent<physics::PhysicsTransformComponent>(primary)) {
                    const auto* pt =
                        ctx.world->getComponent<physics::PhysicsTransformComponent>(primary);
                    if (pt != nullptr) {
                        camera_.frame(pt->transform.position, 2.5F);
                    }
                }
            } else {
                camera_.frame(Vec3::zero(), 5.0F);
            }
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        const char* rendererLabel =
            (isGpuAvailable && s_useGpuRenderer) ? "Renderer: GPU" : "Renderer: CPU";
        if (ImGui::Button(rendererLabel)) {
            s_useGpuRenderer = !s_useGpuRenderer;
        }

        // ---------------------------------------------------------------------
        // 8. 3D Viewport Orientation Tripod (Top-Right)
        // ---------------------------------------------------------------------
        const ImVec2 axisCenter(canvasPos.x + canvasSize.x - 38.0F, canvasPos.y + 38.0F);
        drawList->AddCircleFilled(axisCenter, 22.0F, IM_COL32(18, 22, 32, 210));
        drawList->AddCircle(axisCenter, 22.0F, IM_COL32(45, 55, 75, 180), 16, 1.0F);

        const Mat4& vMat = camera_.getViewMatrix();
        const Vec3 vX{vMat.at(0, 0), vMat.at(1, 0), vMat.at(2, 0)};
        const Vec3 vY{vMat.at(0, 1), vMat.at(1, 1), vMat.at(2, 1)};
        const Vec3 vZ{vMat.at(0, 2), vMat.at(1, 2), vMat.at(2, 2)};

        constexpr float kTripodLen = 15.0F;
        // X axis (Red)
        drawList->AddLine(
            axisCenter,
            ImVec2(axisCenter.x + (vX.x * kTripodLen), axisCenter.y - (vX.y * kTripodLen)),
            IM_COL32(239, 68, 68, 255), 2.0F);
        // Y axis (Green)
        drawList->AddLine(
            axisCenter,
            ImVec2(axisCenter.x + (vY.x * kTripodLen), axisCenter.y - (vY.y * kTripodLen)),
            IM_COL32(34, 197, 94, 255), 2.0F);
        // Z axis (Blue)
        drawList->AddLine(
            axisCenter,
            ImVec2(axisCenter.x + (vZ.x * kTripodLen), axisCenter.y - (vZ.y * kTripodLen)),
            IM_COL32(59, 130, 246, 255), 2.0F);

        // ---------------------------------------------------------------------
        // 9. Floating Studio Diagnostic HUD Card (Bottom-Left)
        // ---------------------------------------------------------------------
        if (s_showStats) {
            float cardWidth = 460.0F;
            float cardHeight = 65.0F;
            ImVec2 cardPos(canvasPos.x + 12.0F, canvasPos.y + canvasSize.y - cardHeight - 12.0F);

            drawList->AddRectFilled(cardPos, ImVec2(cardPos.x + cardWidth, cardPos.y + cardHeight),
                                    IM_COL32(15, 20, 28, 225), 4.0F);
            drawList->AddRect(cardPos, ImVec2(cardPos.x + cardWidth, cardPos.y + cardHeight),
                              IM_COL32(45, 55, 75, 180), 4.0F);

            ImGui::SetCursorScreenPos(ImVec2(cardPos.x + 10.0F, cardPos.y + 7.0F));
            const char* rendererTag =
                (isGpuAvailable && s_useGpuRenderer) ? " [GPU VIEWPORT]" : " [CPU RASTER]";
            ImGui::TextColored(
                ImVec4(0.38F, 0.74F, 0.98F, 1.0F),
                "3D Viewport: %.0fx%.0f (%.2f:1) | Cam: (%.1f, %.1f, %.1f)%s",
                static_cast<double>(canvasSize.x), static_cast<double>(canvasSize.y),
                canvasSize.y > 0.0F ? static_cast<double>(canvasSize.x / canvasSize.y) : 1.0,
                static_cast<double>(camera_.getPosition().x),
                static_cast<double>(camera_.getPosition().y),
                static_cast<double>(camera_.getPosition().z),
                rendererTag);

            const char* simBadge = "[EDIT MODE]";
            ImVec4 badgeColor(0.2F, 0.8F, 0.4F, 1.0F);
            if (ctx.isSimulating) {
                if (ctx.isPaused) {
                    simBadge = "[PAUSED]";
                    badgeColor = ImVec4(0.96F, 0.62F, 0.04F, 1.0F);
                } else {
                    simBadge = "[PLAYING - RUNTIME WORLD]";
                    badgeColor = ImVec4(0.38F, 0.74F, 0.98F, 1.0F);
                }
            }

            ImGui::SetCursorScreenPos(ImVec2(cardPos.x + 10.0F, cardPos.y + 26.0F));
            ImGui::TextColored(badgeColor, "%s", simBadge);
            ImGui::SameLine();
            ImGui::TextDisabled("| Dist: %.1fm", static_cast<double>(camera_.getDistance()));
            ImGui::SameLine();

            const bool isFly = (camera_.getCameraMode() == EditorCameraMode::Fly);
            const bool isNav = camera_.isNavigating();
            float effectiveSpeed = camera_.getMoveSpeed();
            if (io.KeyShift) {
                effectiveSpeed *= EditorCamera::kFastSpeedMultiplier;
            } else if (io.KeyCtrl) {
                effectiveSpeed *= EditorCamera::kSlowSpeedMultiplier;
            }

            if (isNav) {
                ImGui::TextColored(ImVec4(0.3F, 0.9F, 0.4F, 1.0F), "| [NAV] %s (%.1f m/s)",
                                   isFly ? "Fly" : "Orbit-Fly", static_cast<double>(effectiveSpeed));
            } else {
                ImGui::TextDisabled("| Cam: %s (%.1f m/s)", isFly ? "Fly" : "Orbit",
                                    static_cast<double>(camera_.getMoveSpeed()));
            }

            ImGui::SetCursorScreenPos(ImVec2(cardPos.x + 10.0F, cardPos.y + 45.0F));
            if (hasPrimarySelected) {
                ImGui::TextColored(ImVec4(0.9F, 0.8F, 0.2F, 1.0F), "Selected Entity: #%llu | Cam: %s",
                                   static_cast<unsigned long long>(primarySelectedHandle.id),
                                   isFly ? "Fly" : "Orbit");
            } else {
                ImGui::TextDisabled(
                    isFly ? "Fly: WASD+QE Fly | RMB Look | Shift Fast | Ctrl Slow | Wheel Speed"
                          : "Orbit: RMB Fly | MMB Orbit | Shift+MMB Pan | Wheel Zoom | F Frame");
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace eng::editor
