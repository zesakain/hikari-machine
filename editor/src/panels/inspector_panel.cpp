/// @file inspector_panel.cpp
/// @brief Entity inspector and component editor implementation with professional property grid.

#include "panels/inspector_panel.h"

#include "core/log.h"
#include "editor/editor_theme.h"
#include "editor/selection_model.h"
#include "physics/physics_components.h"
#include "script/script_components.h"
#include "world/world.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <imgui.h>

namespace eng::editor {

namespace {

void quatToEulerDegrees(const Quat& qIn, float outEuler[3]) {
    Quat q = qIn.normalized();
    constexpr float kRadToDeg = 57.29577951308232F;

    // Pitch (X-axis rotation)
    float sinp = 2.0F * ((q.w * q.x) - (q.y * q.z));
    if (std::abs(sinp) >= 1.0F) {
        outEuler[0] = std::copysign(90.0F, sinp);
    } else {
        outEuler[0] = std::asin(sinp) * kRadToDeg;
    }

    // Yaw (Y-axis rotation)
    float siny_cosp = 2.0F * ((q.w * q.y) + (q.x * q.z));
    float cosy_cosp = 1.0F - (2.0F * ((q.x * q.x) + (q.y * q.y)));
    outEuler[1] = std::atan2(siny_cosp, cosy_cosp) * kRadToDeg;

    // Roll (Z-axis rotation)
    float sinr_cosp = 2.0F * ((q.w * q.z) + (q.x * q.y));
    float cosr_cosp = 1.0F - (2.0F * ((q.x * q.x) + (q.z * q.z)));
    outEuler[2] = std::atan2(sinr_cosp, cosr_cosp) * kRadToDeg;
}

Quat eulerDegreesToQuat(const float euler[3]) {
    constexpr float kDegToRad = 0.017453292519943295F;
    Quat qx = Quat::fromAxisAngle(Vec3(1.0F, 0.0F, 0.0F), euler[0] * kDegToRad);
    Quat qy = Quat::fromAxisAngle(Vec3(0.0F, 1.0F, 0.0F), euler[1] * kDegToRad);
    Quat qz = Quat::fromAxisAngle(Vec3(0.0F, 0.0F, 1.0F), euler[2] * kDegToRad);
    return (qy * qx * qz).normalized();
}

struct Vec3MixedState {
    bool x{false};
    bool y{false};
    bool z{false};
};

bool drawVec3Property(const char* label, float values[3], float resetValue = 0.0F,
                      float speed = 0.1F, float minValue = -1e9F, float maxValue = 1e9F,
                      Vec3MixedState mixed = {}, bool* outModX = nullptr, bool* outModY = nullptr,
                      bool* outModZ = nullptr) {
    bool modX = false;
    bool modY = false;
    bool modZ = false;
    ImGui::PushID(label);
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, ThemeMetrics::kPropertyLabelWidth);
    ImGui::TextDisabled("%s", label);
    ImGui::NextColumn();

    float availW = ImGui::GetContentRegionAvail().x;
    float itemW = (availW - 70.0F) / 3.0F;
    if (itemW < 35.0F) {
        itemW = 35.0F;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0F, 0.0F));

    // X Axis (Red)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85F, 0.20F, 0.20F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95F, 0.30F, 0.30F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70F, 0.15F, 0.15F, 1.0F));
    if (ImGui::Button("X", ImVec2(18.0F, 0.0F))) {
        values[0] = resetValue;
        modX = true;
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(itemW);
    const char* fmtX = mixed.x ? "---" : "%.2f";
    if (ImGui::DragFloat("##X", &values[0], speed, minValue, maxValue, fmtX)) {
        modX = true;
    }
    ImGui::SameLine();

    // Y Axis (Green)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15F, 0.70F, 0.25F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25F, 0.80F, 0.35F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10F, 0.60F, 0.20F, 1.0F));
    if (ImGui::Button("Y", ImVec2(18.0F, 0.0F))) {
        values[1] = resetValue;
        modY = true;
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(itemW);
    const char* fmtY = mixed.y ? "---" : "%.2f";
    if (ImGui::DragFloat("##Y", &values[1], speed, minValue, maxValue, fmtY)) {
        modY = true;
    }
    ImGui::SameLine();

    // Z Axis (Blue)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20F, 0.45F, 0.85F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30F, 0.55F, 0.95F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15F, 0.35F, 0.75F, 1.0F));
    if (ImGui::Button("Z", ImVec2(18.0F, 0.0F))) {
        values[2] = resetValue;
        modZ = true;
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(itemW);
    const char* fmtZ = mixed.z ? "---" : "%.2f";
    if (ImGui::DragFloat("##Z", &values[2], speed, minValue, maxValue, fmtZ)) {
        modZ = true;
    }

    ImGui::PopStyleVar();
    ImGui::Columns(1);
    ImGui::PopID();

    if (outModX != nullptr) {
        *outModX = modX;
    }
    if (outModY != nullptr) {
        *outModY = modY;
    }
    if (outModZ != nullptr) {
        *outModZ = modZ;
    }

    return modX || modY || modZ;
}

} // namespace

void InspectorPanel::draw(EditorContext& ctx) {
    if (!isOpen_) {
        return;
    }

    if (ImGui::Begin(getDisplayName().data(), &isOpen_)) {
        // Filter alive selected entities
        std::vector<world::EntityHandle> selected;
        if (ctx.world != nullptr && ctx.selection != nullptr) {
            for (auto h : ctx.selection->getSelectedEntities()) {
                if (h.isValid() && ctx.world->isAlive(h)) {
                    selected.push_back(h);
                }
            }
        }

        if (selected.size() > 1) {
            flecs::world& ecs = ctx.world->getRawWorld();

            // Multi-selection Header
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0F, 4.0F));
            ImGui::TextDisabled("Multi-Selection");
            ImGui::SameLine();
            ImGui::Text("%zu Entities Selected", selected.size());
            ImGui::PopStyleVar();

            ImGui::Separator();
            ImGui::Spacing();

            // Collapsible entities list
            if (ImGui::CollapsingHeader("Selected Entities List")) {
                ImGui::BeginChild("MultiSelectionList", ImVec2(0.0F, 110.0F), true);
                for (auto h : selected) {
                    flecs::entity e = ecs.entity(h.id);
                    const char* entityName = e.name().c_str();
                    char fallbackName[32];
                    if (entityName == nullptr || entityName[0] == '\0') {
                        std::snprintf(fallbackName, sizeof(fallbackName), "Entity #%llu",
                                      static_cast<unsigned long long>(h.id));
                        entityName = fallbackName;
                    }
                    ImGui::BulletText("%s (#%llu)", entityName,
                                      static_cast<unsigned long long>(h.id));
                }
                ImGui::EndChild();
                ImGui::Spacing();
            }

            // Common components detection
            bool allHaveTransform = true;
            bool allHaveRigidBody = true;
            bool allHaveCollider = true;
            bool allHaveScript = true;

            for (auto h : selected) {
                flecs::entity e = ecs.entity(h.id);
                if (!e.has<physics::PhysicsTransformComponent>()) {
                    allHaveTransform = false;
                }
                if (!e.has<physics::RigidBodyComponent>()) {
                    allHaveRigidBody = false;
                }
                if (!e.has<physics::ColliderComponent>()) {
                    allHaveCollider = false;
                }
                if (!e.has<script::ScriptComponent>()) {
                    allHaveScript = false;
                }
            }

            // 1. Multi-Selection Transform Card
            if (allHaveTransform) {
                bool removeMultiTransform = false;
                bool headerOpen =
                    ImGui::CollapsingHeader("Transform##Multi", ImGuiTreeNodeFlags_DefaultOpen);
                if (ImGui::BeginPopupContextItem("TransformMultiCtx")) {
                    if (ImGui::MenuItem("Reset Transform for All")) {
                        for (auto h : selected) {
                            ecs.entity(h.id).set<physics::PhysicsTransformComponent>({});
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Remove Component from All")) {
                        removeMultiTransform = true;
                    }
                    ImGui::EndPopup();
                }

                if (headerOpen) {
                    auto* firstTc =
                        ecs.entity(selected[0].id).get<physics::PhysicsTransformComponent>();
                    Vec3 firstPos = firstTc->transform.position;
                    Quat firstRot = firstTc->transform.rotation;
                    Vec3 firstScl = firstTc->transform.scale;

                    Vec3MixedState mixedPos{false, false, false};
                    Vec3MixedState mixedRot{false, false, false};
                    Vec3MixedState mixedScl{false, false, false};

                    float firstRotEuler[3];
                    quatToEulerDegrees(firstRot, firstRotEuler);

                    for (size_t i = 1; i < selected.size(); ++i) {
                        auto* tc =
                            ecs.entity(selected[i].id).get<physics::PhysicsTransformComponent>();
                        if (std::abs(tc->transform.position.x - firstPos.x) > 1e-4F) {
                            mixedPos.x = true;
                        }
                        if (std::abs(tc->transform.position.y - firstPos.y) > 1e-4F) {
                            mixedPos.y = true;
                        }
                        if (std::abs(tc->transform.position.z - firstPos.z) > 1e-4F) {
                            mixedPos.z = true;
                        }

                        float euler[3];
                        quatToEulerDegrees(tc->transform.rotation, euler);
                        if (std::abs(euler[0] - firstRotEuler[0]) > 0.1F) {
                            mixedRot.x = true;
                        }
                        if (std::abs(euler[1] - firstRotEuler[1]) > 0.1F) {
                            mixedRot.y = true;
                        }
                        if (std::abs(euler[2] - firstRotEuler[2]) > 0.1F) {
                            mixedRot.z = true;
                        }

                        if (std::abs(tc->transform.scale.x - firstScl.x) > 1e-4F) {
                            mixedScl.x = true;
                        }
                        if (std::abs(tc->transform.scale.y - firstScl.y) > 1e-4F) {
                            mixedScl.y = true;
                        }
                        if (std::abs(tc->transform.scale.z - firstScl.z) > 1e-4F) {
                            mixedScl.z = true;
                        }
                    }

                    float posVals[3] = {firstPos.x, firstPos.y, firstPos.z};
                    bool modPosX = false;
                    bool modPosY = false;
                    bool modPosZ = false;
                    if (drawVec3Property("Position", posVals, 0.0F, 0.05F, -1e9F, 1e9F, mixedPos,
                                         &modPosX, &modPosY, &modPosZ)) {
                        for (auto h : selected) {
                            auto* tc =
                                ecs.entity(h.id).get_mut<physics::PhysicsTransformComponent>();
                            if (modPosX) {
                                tc->transform.position.x = posVals[0];
                            }
                            if (modPosY) {
                                tc->transform.position.y = posVals[1];
                            }
                            if (modPosZ) {
                                tc->transform.position.z = posVals[2];
                            }
                            ecs.entity(h.id).modified<physics::PhysicsTransformComponent>();
                        }
                        ctx.markDirty();
                    }

                    float rotVals[3] = {firstRotEuler[0], firstRotEuler[1], firstRotEuler[2]};
                    bool modRotX = false;
                    bool modRotY = false;
                    bool modRotZ = false;
                    if (drawVec3Property("Rotation", rotVals, 0.0F, 0.5F, -1e9F, 1e9F, mixedRot,
                                         &modRotX, &modRotY, &modRotZ)) {
                        for (auto h : selected) {
                            auto* tc =
                                ecs.entity(h.id).get_mut<physics::PhysicsTransformComponent>();
                            float curEuler[3];
                            quatToEulerDegrees(tc->transform.rotation, curEuler);
                            if (modRotX) {
                                curEuler[0] = rotVals[0];
                            }
                            if (modRotY) {
                                curEuler[1] = rotVals[1];
                            }
                            if (modRotZ) {
                                curEuler[2] = rotVals[2];
                            }
                            tc->transform.rotation = eulerDegreesToQuat(curEuler);
                            ecs.entity(h.id).modified<physics::PhysicsTransformComponent>();
                        }
                        ctx.markDirty();
                    }

                    float sclVals[3] = {firstScl.x, firstScl.y, firstScl.z};
                    bool modSclX = false;
                    bool modSclY = false;
                    bool modSclZ = false;
                    if (drawVec3Property("Scale", sclVals, 1.0F, 0.05F, 0.001F, 1000.0F, mixedScl,
                                         &modSclX, &modSclY, &modSclZ)) {
                        for (auto h : selected) {
                            auto* tc =
                                ecs.entity(h.id).get_mut<physics::PhysicsTransformComponent>();
                            if (modSclX) {
                                tc->transform.scale.x = std::max(0.001F, sclVals[0]);
                            }
                            if (modSclY) {
                                tc->transform.scale.y = std::max(0.001F, sclVals[1]);
                            }
                            if (modSclZ) {
                                tc->transform.scale.z = std::max(0.001F, sclVals[2]);
                            }
                            ecs.entity(h.id).modified<physics::PhysicsTransformComponent>();
                        }
                        ctx.markDirty();
                    }

                    ImGui::Spacing();
                    if (ImGui::SmallButton("Remove Transform from All")) {
                        removeMultiTransform = true;
                    }
                }
                if (removeMultiTransform) {
                    for (auto h : selected) {
                        ecs.entity(h.id).remove<physics::PhysicsTransformComponent>();
                    }
                    ctx.markDirty();
                }
            }

            // 2. Multi-Selection Rigid Body Card
            if (allHaveRigidBody) {
                bool removeMultiRb = false;
                bool headerOpen =
                    ImGui::CollapsingHeader("Rigid Body##Multi", ImGuiTreeNodeFlags_DefaultOpen);
                if (ImGui::BeginPopupContextItem("RigidBodyMultiCtx")) {
                    if (ImGui::MenuItem("Reset to Dynamic for All")) {
                        for (auto h : selected) {
                            ecs.entity(h.id).set<physics::RigidBodyComponent>({
                                .motionType = physics::MotionType::Dynamic,
                                .collisionLayer = physics::CollisionLayer::kMoving,
                            });
                        }
                        ctx.markDirty();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Remove Component from All")) {
                        removeMultiRb = true;
                    }
                    ImGui::EndPopup();
                }

                if (headerOpen) {
                    auto* firstRb = ecs.entity(selected[0].id).get<physics::RigidBodyComponent>();
                    bool sameMotion = true;
                    bool sameLayer = true;
                    for (size_t i = 1; i < selected.size(); ++i) {
                        auto* rb = ecs.entity(selected[i].id).get<physics::RigidBodyComponent>();
                        if (rb->motionType != firstRb->motionType) {
                            sameMotion = false;
                        }
                        if (rb->collisionLayer != firstRb->collisionLayer) {
                            sameLayer = false;
                        }
                    }

                    ImGui::Columns(2, nullptr, false);
                    ImGui::SetColumnWidth(0, ThemeMetrics::kPropertyLabelWidth);
                    ImGui::TextDisabled("Motion Type");
                    ImGui::NextColumn();

                    int motionIdx =
                        sameMotion ? ((firstRb->motionType == physics::MotionType::Static)      ? 0
                                      : (firstRb->motionType == physics::MotionType::Kinematic) ? 1
                                                                                                : 2)
                                   : 3;
                    const char* motions[] = {"Static", "Kinematic", "Dynamic", "--- (Mixed)"};
                    ImGui::SetNextItemWidth(-1.0F);
                    if (ImGui::Combo("##MotionTypeComboMulti", &motionIdx, motions,
                                     sameMotion ? 3 : 4)) {
                        if (motionIdx < 3) {
                            auto newMotion = (motionIdx == 0)   ? physics::MotionType::Static
                                             : (motionIdx == 1) ? physics::MotionType::Kinematic
                                                                : physics::MotionType::Dynamic;
                            for (auto h : selected) {
                                auto* rb = ecs.entity(h.id).get_mut<physics::RigidBodyComponent>();
                                rb->motionType = newMotion;
                                ecs.entity(h.id).modified<physics::RigidBodyComponent>();
                            }
                            ctx.markDirty();
                        }
                    }
                    ImGui::Columns(1);

                    ImGui::Columns(2, nullptr, false);
                    ImGui::SetColumnWidth(0, ThemeMetrics::kPropertyLabelWidth);
                    ImGui::TextDisabled("Collision Layer");
                    ImGui::NextColumn();

                    int layerIdx =
                        sameLayer ? std::min(static_cast<int>(firstRb->collisionLayer), 3) : 4;
                    const char* layers[] = {"0: Static Geometry", "1: Moving Dynamics", "2: Player",
                                            "3: Trigger Volumes", "--- (Mixed)"};
                    ImGui::SetNextItemWidth(-1.0F);
                    if (ImGui::Combo("##CollisionLayerComboMulti", &layerIdx, layers,
                                     sameLayer ? 4 : 5)) {
                        if (layerIdx < 4) {
                            for (auto h : selected) {
                                auto* rb = ecs.entity(h.id).get_mut<physics::RigidBodyComponent>();
                                rb->collisionLayer = static_cast<uint16_t>(layerIdx);
                                ecs.entity(h.id).modified<physics::RigidBodyComponent>();
                            }
                            ctx.markDirty();
                        }
                    }
                    ImGui::Columns(1);

                    ImGui::Spacing();
                    if (ImGui::SmallButton("Remove Rigid Body from All")) {
                        removeMultiRb = true;
                    }
                }
                if (removeMultiRb) {
                    for (auto h : selected) {
                        ecs.entity(h.id).remove<physics::RigidBodyComponent>();
                    }
                    ctx.markDirty();
                }
            }

            // 3. Multi-Selection Collider Card
            if (allHaveCollider) {
                bool removeMultiCollider = false;
                bool headerOpen =
                    ImGui::CollapsingHeader("Collider##Multi", ImGuiTreeNodeFlags_DefaultOpen);
                if (ImGui::BeginPopupContextItem("ColliderMultiCtx")) {
                    if (ImGui::MenuItem("Reset to Default Box for All")) {
                        for (auto h : selected) {
                            ecs.entity(h.id).set<physics::ColliderComponent>({
                                .desc = physics::ColliderDesc::makeBox(Vec3(0.5F, 0.5F, 0.5F)),
                            });
                        }
                        ctx.markDirty();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Remove Component from All")) {
                        removeMultiCollider = true;
                    }
                    ImGui::EndPopup();
                }

                if (headerOpen) {
                    auto* firstCc = ecs.entity(selected[0].id).get<physics::ColliderComponent>();
                    bool sameShape = true;
                    for (size_t i = 1; i < selected.size(); ++i) {
                        auto* cc = ecs.entity(selected[i].id).get<physics::ColliderComponent>();
                        if (cc->desc.type != firstCc->desc.type) {
                            sameShape = false;
                        }
                    }

                    ImGui::Columns(2, nullptr, false);
                    ImGui::SetColumnWidth(0, ThemeMetrics::kPropertyLabelWidth);
                    ImGui::TextDisabled("Shape");
                    ImGui::NextColumn();

                    int shapeIdx =
                        sameShape ? ((firstCc->desc.type == physics::ColliderType::Box)      ? 0
                                     : (firstCc->desc.type == physics::ColliderType::Sphere) ? 1
                                                                                             : 2)
                                  : 3;
                    const char* shapes[] = {"Box", "Sphere", "Capsule", "--- (Mixed)"};
                    ImGui::SetNextItemWidth(-1.0F);
                    if (ImGui::Combo("##ColliderShapeComboMulti", &shapeIdx, shapes,
                                     sameShape ? 3 : 4)) {
                        if (shapeIdx < 3) {
                            auto newType = (shapeIdx == 0)   ? physics::ColliderType::Box
                                           : (shapeIdx == 1) ? physics::ColliderType::Sphere
                                                             : physics::ColliderType::Capsule;
                            for (auto h : selected) {
                                auto* cc = ecs.entity(h.id).get_mut<physics::ColliderComponent>();
                                cc->desc.type = newType;
                                ecs.entity(h.id).modified<physics::ColliderComponent>();
                            }
                            ctx.markDirty();
                        }
                    }
                    ImGui::Columns(1);

                    ImGui::Spacing();
                    if (ImGui::SmallButton("Remove Collider from All")) {
                        removeMultiCollider = true;
                    }
                }
                if (removeMultiCollider) {
                    for (auto h : selected) {
                        ecs.entity(h.id).remove<physics::ColliderComponent>();
                    }
                    ctx.markDirty();
                }
            }

            // 4. Multi-Selection Script Card
            if (allHaveScript) {
                bool removeMultiScript = false;
                bool headerOpen = ImGui::CollapsingHeader("Script Component##Multi",
                                                          ImGuiTreeNodeFlags_DefaultOpen);
                if (ImGui::BeginPopupContextItem("ScriptMultiCtx")) {
                    if (ImGui::MenuItem("Clear Routine for All")) {
                        for (auto h : selected) {
                            auto* sc = ecs.entity(h.id).get_mut<script::ScriptComponent>();
                            sc->scriptName[0] = '\0';
                            ecs.entity(h.id).modified<script::ScriptComponent>();
                        }
                        ctx.markDirty();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Remove Component from All")) {
                        removeMultiScript = true;
                    }
                    ImGui::EndPopup();
                }

                if (headerOpen) {
                    auto* firstSc = ecs.entity(selected[0].id).get<script::ScriptComponent>();
                    bool sameRoutine = true;
                    bool sameEnabled = true;
                    for (size_t i = 1; i < selected.size(); ++i) {
                        auto* sc = ecs.entity(selected[i].id).get<script::ScriptComponent>();
                        if (std::strcmp(sc->scriptName, firstSc->scriptName) != 0) {
                            sameRoutine = false;
                        }
                        if (sc->enabled != firstSc->enabled) {
                            sameEnabled = false;
                        }
                    }

                    // Routine Name
                    char scriptBuf[64];
                    std::strncpy(scriptBuf, sameRoutine ? firstSc->scriptName : "",
                                 sizeof(scriptBuf) - 1);
                    scriptBuf[sizeof(scriptBuf) - 1] = '\0';

                    ImGui::Columns(2, nullptr, false);
                    ImGui::SetColumnWidth(0, ThemeMetrics::kPropertyLabelWidth);
                    ImGui::TextDisabled("Routine");
                    ImGui::NextColumn();
                    ImGui::SetNextItemWidth(-1.0F);
                    if (ImGui::InputTextWithHint("##ScriptRoutineInputMulti",
                                                 sameRoutine ? "e.g. on_update"
                                                             : "--- (Mixed Values) ---",
                                                 scriptBuf, sizeof(scriptBuf))) {
                        for (auto h : selected) {
                            auto* sc = ecs.entity(h.id).get_mut<script::ScriptComponent>();
                            std::strncpy(sc->scriptName, scriptBuf, sizeof(sc->scriptName) - 1);
                            sc->scriptName[sizeof(sc->scriptName) - 1] = '\0';
                            ecs.entity(h.id).modified<script::ScriptComponent>();
                        }
                        ctx.markDirty();
                    }
                    ImGui::Columns(1);

                    // Status
                    ImGui::Columns(2, nullptr, false);
                    ImGui::SetColumnWidth(0, ThemeMetrics::kPropertyLabelWidth);
                    ImGui::TextDisabled("Status");
                    ImGui::NextColumn();
                    bool enabled = sameEnabled ? firstSc->enabled : false;
                    if (ImGui::Checkbox("Enabled##ScriptActiveMulti", &enabled)) {
                        for (auto h : selected) {
                            auto* sc = ecs.entity(h.id).get_mut<script::ScriptComponent>();
                            sc->enabled = enabled;
                            ecs.entity(h.id).modified<script::ScriptComponent>();
                        }
                        ctx.markDirty();
                    }
                    ImGui::SameLine();
                    if (!sameEnabled) {
                        ImGui::TextColored(ImVec4(0.85F, 0.75F, 0.25F, 1.0F), "[Mixed]");
                    } else if (!enabled) {
                        ImGui::TextColored(ImVec4(0.85F, 0.40F, 0.20F, 1.0F), "[Disabled]");
                    } else {
                        ImGui::TextColored(ImVec4(0.30F, 0.85F, 0.35F, 1.0F), "[Active]");
                    }
                    ImGui::Columns(1);

                    ImGui::Spacing();
                    if (ImGui::SmallButton("Remove Script Component from All")) {
                        removeMultiScript = true;
                    }
                }
                if (removeMultiScript) {
                    for (auto h : selected) {
                        ecs.entity(h.id).remove<script::ScriptComponent>();
                    }
                    ctx.markDirty();
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Batch Add Component Dropdown Button
            if (ImGui::Button("+ Add Component (To All)", ImVec2(-1.0F, 28.0F))) {
                ImGui::OpenPopup("AddMultiComponentPopup");
            }
            if (ImGui::BeginPopup("AddMultiComponentPopup")) {
                ImGui::TextDisabled("Physics");
                if (ImGui::MenuItem("Transform", nullptr, false, !allHaveTransform)) {
                    for (auto h : selected) {
                        auto e = ecs.entity(h.id);
                        if (!e.has<physics::PhysicsTransformComponent>()) {
                            e.set<physics::PhysicsTransformComponent>({});
                        }
                    }
                    ctx.markDirty();
                }
                if (ImGui::MenuItem("Rigid Body", nullptr, false, !allHaveRigidBody)) {
                    for (auto h : selected) {
                        auto e = ecs.entity(h.id);
                        if (!e.has<physics::RigidBodyComponent>()) {
                            e.set<physics::RigidBodyComponent>({
                                .motionType = physics::MotionType::Dynamic,
                                .collisionLayer = physics::CollisionLayer::kMoving,
                            });
                        }
                    }
                    ctx.markDirty();
                }
                if (ImGui::MenuItem("Collider", nullptr, false, !allHaveCollider)) {
                    for (auto h : selected) {
                        auto e = ecs.entity(h.id);
                        if (!e.has<physics::ColliderComponent>()) {
                            e.set<physics::ColliderComponent>({
                                .desc = physics::ColliderDesc::makeBox(Vec3(0.5F, 0.5F, 0.5F)),
                            });
                        }
                    }
                    ctx.markDirty();
                }

                ImGui::Separator();
                ImGui::TextDisabled("Scripting");
                if (ImGui::MenuItem("Script Component", nullptr, false, !allHaveScript)) {
                    for (auto h : selected) {
                        auto e = ecs.entity(h.id);
                        if (!e.has<script::ScriptComponent>()) {
                            e.set<script::ScriptComponent>({
                                .scriptName = "",
                                .enabled = true,
                                .updateInterval = 0.0f,
                                .timeSinceLastUpdate = 0.0f,
                            });
                        }
                    }
                    ctx.markDirty();
                }
                ImGui::EndPopup();
            }

            ImGui::Spacing();
            if (ImGui::Button("Clear Selection", ImVec2(-1.0F, 24.0F))) {
                ctx.selection->clear();
                ctx.selectedEntityId = 0;
            }

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55F, 0.15F, 0.15F, 1.0F));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75F, 0.20F, 0.20F, 1.0F));
            char deleteBtnBuf[64];
            std::snprintf(deleteBtnBuf, sizeof(deleteBtnBuf), "Delete Selected Entities (%zu)",
                          selected.size());
            if (ImGui::Button(deleteBtnBuf, ImVec2(-1.0F, 26.0F))) {
                for (auto h : selected) {
                    ctx.world->destroyEntity(h);
                }
                ctx.selection->clear();
                ctx.selectedEntityId = 0;
                ctx.markDirty();
            }
            ImGui::PopStyleColor(2);
        } else if (ctx.world != nullptr && (selected.size() == 1 || ctx.selectedEntityId != 0)) {
            uint64_t targetId = (selected.size() == 1) ? selected[0].id : ctx.selectedEntityId;
            ctx.selectedEntityId = targetId;
            world::EntityHandle selectedHandle(targetId);
            if (ctx.world->isAlive(selectedHandle)) {
                flecs::world& ecs = ctx.world->getRawWorld();
                flecs::entity e = ecs.entity(targetId);

                // Entity Header
                const char* entityName = e.name().c_str();
                if (entityName == nullptr || entityName[0] == '\0') {
                    entityName = "Unnamed Entity";
                }

                char nameBuf[64];
                std::snprintf(nameBuf, sizeof(nameBuf), "%s", entityName);

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0F, 4.0F));
                ImGui::TextDisabled("Entity");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90.0F);
                if (ImGui::InputText("##EntityNameInput", nameBuf, sizeof(nameBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue) ||
                    ImGui::IsItemDeactivatedAfterEdit()) {
                    ctx.world->setEntityName(world::EntityHandle(e.id()), nameBuf);
                    ctx.markDirty();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("#%llu", static_cast<unsigned long long>(e.id()));
                ImGui::PopStyleVar();

                // Component Chips Tag List
                ImGui::Spacing();
                ImGui::TextDisabled("Components:");
                ImGui::SameLine();
                if (e.has<physics::PhysicsTransformComponent>()) {
                    ImGui::TextColored(ImVec4(0.40F, 0.75F, 1.0F, 1.0F), "[Transform]");
                    ImGui::SameLine();
                }
                if (e.has<physics::RigidBodyComponent>()) {
                    ImGui::TextColored(ImVec4(0.95F, 0.65F, 0.30F, 1.0F), "[RigidBody]");
                    ImGui::SameLine();
                }
                if (e.has<physics::ColliderComponent>()) {
                    ImGui::TextColored(ImVec4(0.85F, 0.45F, 0.85F, 1.0F), "[Collider]");
                    ImGui::SameLine();
                }
                if (e.has<script::ScriptComponent>()) {
                    ImGui::TextColored(ImVec4(0.45F, 0.85F, 0.45F, 1.0F), "[Script]");
                    ImGui::SameLine();
                }
                ImGui::NewLine();

                ImGui::Separator();
                ImGui::Spacing();

                // 1. Transform Component Card
                bool removeTransform = false;
                if (e.has<physics::PhysicsTransformComponent>()) {
                    bool headerOpen =
                        ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen);
                    if (ImGui::BeginPopupContextItem("TransformHeaderCtx")) {
                        if (ImGui::MenuItem("Reset Transform")) {
                            e.set<physics::PhysicsTransformComponent>({});
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Remove Component")) {
                            removeTransform = true;
                        }
                        ImGui::EndPopup();
                    }

                    if (headerOpen) {
                        auto* tc = e.get_mut<physics::PhysicsTransformComponent>();
                        bool changed = false;

                        // Position
                        float pos[3] = {tc->transform.position.x, tc->transform.position.y,
                                        tc->transform.position.z};
                        if (drawVec3Property("Position", pos, 0.0F, 0.05F)) {
                            tc->transform.position = Vec3(pos[0], pos[1], pos[2]);
                            changed = true;
                        }

                        // Display Euler angles in degrees
                        float rot[3]{0.0F, 0.0F, 0.0F};
                        quatToEulerDegrees(tc->transform.rotation, rot);
                        if (drawVec3Property("Rotation", rot, 0.0F, 0.5F)) {
                            tc->transform.rotation = eulerDegreesToQuat(rot);
                            changed = true;
                        }

                        // Scale with minimum positive threshold
                        float scl[3] = {tc->transform.scale.x, tc->transform.scale.y,
                                        tc->transform.scale.z};
                        if (drawVec3Property("Scale", scl, 1.0F, 0.05F, 0.001F, 1000.0F)) {
                            if (scl[0] < 0.001F) {
                                scl[0] = 0.001F;
                            }
                            if (scl[1] < 0.001F) {
                                scl[1] = 0.001F;
                            }
                            if (scl[2] < 0.001F) {
                                scl[2] = 0.001F;
                            }
                            tc->transform.scale = Vec3(scl[0], scl[1], scl[2]);
                            changed = true;
                        }

                        if (changed) {
                            e.modified<physics::PhysicsTransformComponent>();
                            ctx.markDirty();
                        }

                        ImGui::Spacing();
                        if (ImGui::SmallButton("Remove Transform")) {
                            removeTransform = true;
                        }
                    }
                }
                if (removeTransform) {
                    e.remove<physics::PhysicsTransformComponent>();
                    ctx.markDirty();
                }

                // 2. Rigid Body Component Card
                bool removeRigidBody = false;
                if (e.has<physics::RigidBodyComponent>()) {
                    bool headerOpen =
                        ImGui::CollapsingHeader("Rigid Body", ImGuiTreeNodeFlags_DefaultOpen);
                    if (ImGui::BeginPopupContextItem("RigidBodyHeaderCtx")) {
                        if (ImGui::MenuItem("Reset to Dynamic")) {
                            e.set<physics::RigidBodyComponent>({
                                .motionType = physics::MotionType::Dynamic,
                                .collisionLayer = physics::CollisionLayer::kMoving,
                            });
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Remove Component")) {
                            removeRigidBody = true;
                        }
                        ImGui::EndPopup();
                    }

                    if (headerOpen) {
                        auto* rb = e.get_mut<physics::RigidBodyComponent>();

                        ImGui::Columns(2, nullptr, false);
                        ImGui::SetColumnWidth(0, ThemeMetrics::kPropertyLabelWidth);
                        ImGui::TextDisabled("Motion Type");
                        ImGui::NextColumn();

                        int motionIdx = (rb->motionType == physics::MotionType::Static)      ? 0
                                        : (rb->motionType == physics::MotionType::Kinematic) ? 1
                                                                                             : 2;
                        const char* motions[] = {"Static", "Kinematic", "Dynamic"};
                        ImGui::SetNextItemWidth(-1.0F);
                        if (ImGui::Combo("##MotionTypeCombo", &motionIdx, motions, 3)) {
                            rb->motionType = (motionIdx == 0)   ? physics::MotionType::Static
                                             : (motionIdx == 1) ? physics::MotionType::Kinematic
                                                                : physics::MotionType::Dynamic;
                            e.modified<physics::RigidBodyComponent>();
                            ctx.markDirty();
                        }
                        ImGui::Columns(1);

                        ImGui::Columns(2, nullptr, false);
                        ImGui::SetColumnWidth(0, ThemeMetrics::kPropertyLabelWidth);
                        ImGui::TextDisabled("Collision Layer");
                        ImGui::NextColumn();

                        const char* layers[] = {"0: Static Geometry", "1: Moving Dynamics",
                                                "2: Player", "3: Trigger Volumes"};
                        int layerIdx = std::min(static_cast<int>(rb->collisionLayer), 3);
                        ImGui::SetNextItemWidth(-1.0F);
                        if (ImGui::Combo("##CollisionLayerCombo", &layerIdx, layers, 4)) {
                            rb->collisionLayer = static_cast<uint16_t>(layerIdx);
                            e.modified<physics::RigidBodyComponent>();
                            ctx.markDirty();
                        }
                        ImGui::Columns(1);

                        ImGui::Spacing();
                        if (ImGui::SmallButton("Remove Rigid Body")) {
                            removeRigidBody = true;
                        }
                    }
                }
                if (removeRigidBody) {
                    e.remove<physics::RigidBodyComponent>();
                    ctx.markDirty();
                }

                // 3. Collider Component Card
                bool removeCollider = false;
                if (e.has<physics::ColliderComponent>()) {
                    bool headerOpen =
                        ImGui::CollapsingHeader("Collider", ImGuiTreeNodeFlags_DefaultOpen);
                    if (ImGui::BeginPopupContextItem("ColliderHeaderCtx")) {
                        if (ImGui::MenuItem("Reset to Default Box")) {
                            e.set<physics::ColliderComponent>({
                                .desc = physics::ColliderDesc::makeBox(Vec3(0.5F, 0.5F, 0.5F)),
                            });
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Remove Component")) {
                            removeCollider = true;
                        }
                        ImGui::EndPopup();
                    }

                    if (headerOpen) {
                        auto* cc = e.get_mut<physics::ColliderComponent>();
                        bool changed = false;

                        // Collider Type Combo
                        ImGui::Columns(2, nullptr, false);
                        ImGui::SetColumnWidth(0, ThemeMetrics::kPropertyLabelWidth);
                        ImGui::TextDisabled("Shape");
                        ImGui::NextColumn();

                        int shapeIdx = (cc->desc.type == physics::ColliderType::Box)      ? 0
                                       : (cc->desc.type == physics::ColliderType::Sphere) ? 1
                                                                                          : 2;
                        const char* shapes[] = {"Box", "Sphere", "Capsule"};
                        ImGui::SetNextItemWidth(-1.0F);
                        if (ImGui::Combo("##ColliderShapeCombo", &shapeIdx, shapes, 3)) {
                            cc->desc.type = (shapeIdx == 0)   ? physics::ColliderType::Box
                                            : (shapeIdx == 1) ? physics::ColliderType::Sphere
                                                              : physics::ColliderType::Capsule;
                            changed = true;
                        }
                        ImGui::Columns(1);

                        // Shape-specific parameters
                        if (cc->desc.type == physics::ColliderType::Box) {
                            float ext[3] = {cc->desc.halfExtents.x, cc->desc.halfExtents.y,
                                            cc->desc.halfExtents.z};
                            if (drawVec3Property("Half Extents", ext, 0.5F, 0.05F, 0.01F,
                                                 1000.0F)) {
                                if (ext[0] < 0.01F) {
                                    ext[0] = 0.01F;
                                }
                                if (ext[1] < 0.01F) {
                                    ext[1] = 0.01F;
                                }
                                if (ext[2] < 0.01F) {
                                    ext[2] = 0.01F;
                                }
                                cc->desc.halfExtents = Vec3(ext[0], ext[1], ext[2]);
                                changed = true;
                            }
                        } else if (cc->desc.type == physics::ColliderType::Sphere) {
                            ImGui::Columns(2, nullptr, false);
                            ImGui::SetColumnWidth(0, ThemeMetrics::kPropertyLabelWidth);
                            ImGui::TextDisabled("Radius");
                            ImGui::NextColumn();
                            ImGui::SetNextItemWidth(-1.0F);
                            float radius = cc->desc.radius;
                            if (ImGui::DragFloat("##ColliderRadius", &radius, 0.05F, 0.01F, 1000.0F,
                                                 "%.2f m")) {
                                if (radius < 0.01F) {
                                    radius = 0.01F;
                                }
                                cc->desc.radius = radius;
                                changed = true;
                            }
                            ImGui::Columns(1);
                        } else if (cc->desc.type == physics::ColliderType::Capsule) {
                            ImGui::Columns(2, nullptr, false);
                            ImGui::SetColumnWidth(0, ThemeMetrics::kPropertyLabelWidth);
                            ImGui::TextDisabled("Radius");
                            ImGui::NextColumn();
                            ImGui::SetNextItemWidth(-1.0F);
                            float radius = cc->desc.radius;
                            if (ImGui::DragFloat("##CapsuleRadius", &radius, 0.05F, 0.01F, 1000.0F,
                                                 "%.2f m")) {
                                if (radius < 0.01F) {
                                    radius = 0.01F;
                                }
                                cc->desc.radius = radius;
                                changed = true;
                            }
                            ImGui::Columns(1);

                            ImGui::Columns(2, nullptr, false);
                            ImGui::SetColumnWidth(0, ThemeMetrics::kPropertyLabelWidth);
                            ImGui::TextDisabled("Half Height");
                            ImGui::NextColumn();
                            ImGui::SetNextItemWidth(-1.0F);
                            float halfH = cc->desc.halfHeight;
                            if (ImGui::DragFloat("##CapsuleHalfH", &halfH, 0.05F, 0.01F, 1000.0F,
                                                 "%.2f m")) {
                                if (halfH < 0.01F) {
                                    halfH = 0.01F;
                                }
                                cc->desc.halfHeight = halfH;
                                changed = true;
                            }
                            ImGui::Columns(1);
                        }

                        // Local Center Offset
                        float offset[3] = {cc->desc.offset.x, cc->desc.offset.y, cc->desc.offset.z};
                        if (drawVec3Property("Center Offset", offset, 0.0F, 0.05F)) {
                            cc->desc.offset = Vec3(offset[0], offset[1], offset[2]);
                            changed = true;
                        }

                        if (changed) {
                            e.modified<physics::ColliderComponent>();
                            ctx.markDirty();
                        }

                        ImGui::Spacing();
                        if (ImGui::SmallButton("Remove Collider")) {
                            removeCollider = true;
                        }
                    }
                }
                if (removeCollider) {
                    e.remove<physics::ColliderComponent>();
                    ctx.markDirty();
                }

                // 4. Script Component Card
                bool removeScript = false;
                if (e.has<script::ScriptComponent>()) {
                    bool headerOpen =
                        ImGui::CollapsingHeader("Script Component", ImGuiTreeNodeFlags_DefaultOpen);
                    if (ImGui::BeginPopupContextItem("ScriptHeaderCtx")) {
                        if (ImGui::MenuItem("Clear Routine")) {
                            auto* sc = e.get_mut<script::ScriptComponent>();
                            sc->scriptName[0] = '\0';
                            e.modified<script::ScriptComponent>();
                            ctx.markDirty();
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Remove Component")) {
                            removeScript = true;
                        }
                        ImGui::EndPopup();
                    }

                    if (headerOpen) {
                        auto* sc = e.get_mut<script::ScriptComponent>();

                        // Script Routine Name Input
                        char scriptBuf[64];
                        std::strncpy(scriptBuf, sc->scriptName, sizeof(scriptBuf) - 1);
                        scriptBuf[sizeof(scriptBuf) - 1] = '\0';

                        ImGui::Columns(2, nullptr, false);
                        ImGui::SetColumnWidth(0, ThemeMetrics::kPropertyLabelWidth);
                        ImGui::TextDisabled("Routine");
                        ImGui::NextColumn();
                        ImGui::SetNextItemWidth(-1.0F);
                        if (ImGui::InputTextWithHint("##ScriptRoutineInput", "e.g. on_update",
                                                     scriptBuf, sizeof(scriptBuf))) {
                            std::strncpy(sc->scriptName, scriptBuf, sizeof(sc->scriptName) - 1);
                            sc->scriptName[sizeof(sc->scriptName) - 1] = '\0';
                            e.modified<script::ScriptComponent>();
                            ctx.markDirty();
                        }
                        ImGui::Columns(1);

                        // Script Enabled Checkbox and State Badge
                        ImGui::Columns(2, nullptr, false);
                        ImGui::SetColumnWidth(0, ThemeMetrics::kPropertyLabelWidth);
                        ImGui::TextDisabled("Status");
                        ImGui::NextColumn();
                        bool enabled = sc->enabled;
                        if (ImGui::Checkbox("Enabled##ScriptActive", &enabled)) {
                            sc->enabled = enabled;
                            e.modified<script::ScriptComponent>();
                            ctx.markDirty();
                        }
                        ImGui::SameLine();
                        if (!sc->enabled) {
                            ImGui::TextColored(ImVec4(0.85F, 0.40F, 0.20F, 1.0F), "[Disabled]");
                        } else if (sc->scriptName[0] == '\0') {
                            ImGui::TextColored(ImVec4(0.85F, 0.75F, 0.25F, 1.0F), "[Unassigned]");
                        } else {
                            ImGui::TextColored(ImVec4(0.30F, 0.85F, 0.35F, 1.0F), "[Active]");
                        }
                        ImGui::Columns(1);

                        // Update Interval
                        ImGui::Columns(2, nullptr, false);
                        ImGui::SetColumnWidth(0, ThemeMetrics::kPropertyLabelWidth);
                        ImGui::TextDisabled("Interval");
                        ImGui::NextColumn();
                        ImGui::SetNextItemWidth(-1.0F);
                        float interval = sc->updateInterval;
                        if (ImGui::DragFloat("##ScriptInterval", &interval, 0.01F, 0.0F, 10.0F,
                                             "%.3f s")) {
                            if (interval < 0.0F) {
                                interval = 0.0F;
                            }
                            sc->updateInterval = interval;
                            e.modified<script::ScriptComponent>();
                            ctx.markDirty();
                        }
                        ImGui::Columns(1);

                        ImGui::Spacing();
                        if (ImGui::SmallButton("Remove Script Component")) {
                            removeScript = true;
                        }
                    }
                }
                if (removeScript) {
                    e.remove<script::ScriptComponent>();
                    ctx.markDirty();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Add Component Dropdown Button
                if (ImGui::Button("+ Add Component", ImVec2(-1.0F, 28.0F))) {
                    ImGui::OpenPopup("AddComponentPopup");
                }
                if (ImGui::BeginPopup("AddComponentPopup")) {
                    bool hasTransform = e.has<physics::PhysicsTransformComponent>();
                    bool hasRigidBody = e.has<physics::RigidBodyComponent>();
                    bool hasCollider = e.has<physics::ColliderComponent>();
                    bool hasScript = e.has<script::ScriptComponent>();

                    ImGui::TextDisabled("Physics");
                    if (ImGui::MenuItem("Transform", nullptr, false, !hasTransform)) {
                        e.set<physics::PhysicsTransformComponent>({});
                        ctx.markDirty();
                    }
                    if (ImGui::MenuItem("Rigid Body", nullptr, false, !hasRigidBody)) {
                        e.set<physics::RigidBodyComponent>({
                            .motionType = physics::MotionType::Dynamic,
                            .collisionLayer = physics::CollisionLayer::kMoving,
                        });
                        ctx.markDirty();
                    }
                    if (ImGui::MenuItem("Collider", nullptr, false, !hasCollider)) {
                        e.set<physics::ColliderComponent>({
                            .desc = physics::ColliderDesc::makeBox(Vec3(0.5F, 0.5F, 0.5F)),
                        });
                        ctx.markDirty();
                    }

                    ImGui::Separator();
                    ImGui::TextDisabled("Scripting");
                    if (ImGui::MenuItem("Script Component", nullptr, false, !hasScript)) {
                        e.set<script::ScriptComponent>({
                            .scriptName = "",
                            .enabled = true,
                            .updateInterval = 0.0f,
                            .timeSinceLastUpdate = 0.0f,
                        });
                        ctx.markDirty();
                    }
                    ImGui::EndPopup();
                }

                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55F, 0.15F, 0.15F, 1.0F));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75F, 0.20F, 0.20F, 1.0F));
                if (ImGui::Button("Delete Entity", ImVec2(-1.0F, 24.0F))) {
                    world::EntityHandle h(e.id());
                    ctx.world->destroyEntity(h);
                    if (ctx.selection != nullptr) {
                        ctx.selection->deselect(h);
                    }
                    ctx.selectedEntityId = 0;
                    ctx.markDirty();
                }
                ImGui::PopStyleColor(2);
            } else {
                ImGui::TextDisabled("Selected entity is no longer alive.");
                if (ctx.selection != nullptr) {
                    ctx.selection->deselect(world::EntityHandle(ctx.selectedEntityId));
                }
                ctx.selectedEntityId = 0;
            }
        } else {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65F, 0.65F, 0.70F, 1.0F));
            ImGui::TextDisabled("No Entity Selected");
            ImGui::PopStyleColor();
            ImGui::Spacing();
            ImGui::TextWrapped(
                "Select an entity from the Scene Hierarchy to inspect its components, "
                "spatial transforms, collision geometry, and Lua script routines.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextDisabled("Inspector Shortcuts & Interaction:");
            ImGui::BulletText("Ctrl+Click: Add/remove entity in multi-selection");
            ImGui::BulletText("Shift+Click: Contiguous range selection");
            ImGui::BulletText("Right-click header: Reset component or remove");
        }
    }
    ImGui::End();
}

} // namespace eng::editor
