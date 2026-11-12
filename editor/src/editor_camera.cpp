/// @file editor_camera.cpp
/// @brief Independent 3D editor camera implementation with studio navigation controls.

#include "editor/editor_camera.h"

#include <algorithm>
#include <cmath>

namespace eng::editor {

namespace {
constexpr float kMinPitch = -1.553343F; // -89.0 degrees
constexpr float kMaxPitch = 1.553343F;  // +89.0 degrees
constexpr float kMinDistance = 0.2F;
constexpr float kMaxDistance = 1000.0F;
} // namespace

EditorCamera::EditorCamera() noexcept {
    resetToDefault();
}

void EditorCamera::resetToDefault() noexcept {
    pivot_ = Vec3{0.0F, 0.0F, 0.0F};
    distance_ = 10.0F;
    yaw_ = -0.785398F;   // -45 degrees
    pitch_ = -0.400000F; // ~ -23 degrees looking towards origin
    cameraMode_ = EditorCameraMode::Orbit;
    isNavigating_ = false;
    moveSpeed_ = kDefaultMoveSpeed;
    mouseSensitivity_ = kDefaultMouseSensitivity;

    updateVectorsFromAngles();
    updatePositionFromPivot();
    updateMatrices(1280.0F, 720.0F);
}

void EditorCamera::lookAt(const Vec3& eye, const Vec3& target) noexcept {
    pivot_ = target;
    position_ = eye;
    updateVectorsFromPivot();
    updateMatrices(1280.0F, 720.0F);
}

void EditorCamera::frame(const Vec3& target, float radius) noexcept {
    pivot_ = target;
    distance_ = std::clamp(std::max(2.0F, radius * 2.5F), kMinDistance, kMaxDistance);
    updatePositionFromPivot();
    updateMatrices(1280.0F, 720.0F);
}

void EditorCamera::setDistance(float dist) noexcept {
    distance_ = std::clamp(dist, kMinDistance, kMaxDistance);
    updatePositionFromPivot();
}

void EditorCamera::orbit(float deltaYaw, float deltaPitch) noexcept {
    yaw_ += deltaYaw;
    pitch_ = std::clamp(pitch_ + deltaPitch, kMinPitch, kMaxPitch);

    updateVectorsFromAngles();
    updatePositionFromPivot();
}

void EditorCamera::pan(float deltaX, float deltaY) noexcept {
    // Movement scaled proportionally to distance to pivot
    const float panFactor = distance_ * 0.0015F;
    const Vec3 panOffset = (-right_ * (deltaX * panFactor)) + (up_ * (deltaY * panFactor));

    pivot_ += panOffset;
    position_ += panOffset;
}

void EditorCamera::zoom(float scrollDelta) noexcept {
    // Exponential zooming for smooth close-up and far distances
    const float zoomFactor = 1.0F - (scrollDelta * 0.12F);
    distance_ = std::clamp(distance_ * zoomFactor, kMinDistance, kMaxDistance);
    updatePositionFromPivot();
}

void EditorCamera::look(float deltaYaw, float deltaPitch) noexcept {
    yaw_ += deltaYaw * mouseSensitivity_;
    pitch_ = std::clamp(pitch_ - (deltaPitch * mouseSensitivity_), kMinPitch, kMaxPitch);

    updateVectorsFromAngles();
    // Maintain pivot at fixed distance in front of camera
    pivot_ = position_ + (forward_ * distance_);
}

void EditorCamera::setMoveSpeed(float speed) noexcept {
    moveSpeed_ = std::clamp(speed, kMinMoveSpeed, kMaxMoveSpeed);
}

void EditorCamera::adjustMoveSpeed(float delta) noexcept {
    if (delta > 0.0F) {
        moveSpeed_ = std::clamp(moveSpeed_ * 1.15F, kMinMoveSpeed, kMaxMoveSpeed);
    } else if (delta < 0.0F) {
        moveSpeed_ = std::clamp(moveSpeed_ * 0.85F, kMinMoveSpeed, kMaxMoveSpeed);
    }
}

void EditorCamera::fly(const Vec3& localMovement, float deltaTime, float fastMultiplier) noexcept {
    Vec3 dir = localMovement;
    const float lenSq = dir.lengthSq();
    if (lenSq > 1e-6F) {
        dir = dir / std::sqrt(lenSq);
    }
    const float speed = moveSpeed_ * fastMultiplier * deltaTime;
    const Vec3 move = (right_ * dir.x) + (Vec3::unitY() * dir.y) + (forward_ * dir.z);

    position_ += move * speed;
    pivot_ += move * speed;
}

void EditorCamera::updateMatrices(float viewportWidth, float viewportHeight) noexcept {
    if (viewportHeight > 0.0F && viewportWidth > 0.0F) {
        aspectRatio_ = viewportWidth / viewportHeight;
    }

    viewMatrix_ = Mat4::lookAt(position_, position_ + forward_, Vec3::unitY());

    if (projectionMode_ == CameraProjectionMode::Perspective) {
        projectionMatrix_ = Mat4::perspective(fovY_, aspectRatio_, nearPlane_, farPlane_);
    } else {
        const float halfW = orthoSize_ * aspectRatio_ * 0.5F;
        const float halfH = orthoSize_ * 0.5F;
        projectionMatrix_ = Mat4::orthographic(-halfW, halfW, -halfH, halfH, nearPlane_, farPlane_);
    }

    viewProjectionMatrix_ = projectionMatrix_ * viewMatrix_;

    auto invOpt = viewProjectionMatrix_.inverse();
    if (invOpt.has_value()) {
        invViewProjectionMatrix_ = *invOpt;
    }
}

Ray EditorCamera::unproject(const Vec3& screenPoint, const Vec3& canvasOrigin,
                            const Vec3& canvasSize) const noexcept {
    if (canvasSize.x <= 0.0F || canvasSize.y <= 0.0F) {
        return Ray(position_, forward_);
    }

    // Convert screen coordinates to NDC [-1, 1]
    const float ndcX = ((screenPoint.x - canvasOrigin.x) / canvasSize.x * 2.0F) - 1.0F;
    const float ndcY = 1.0F - ((screenPoint.y - canvasOrigin.y) / canvasSize.y * 2.0F);

    float wNear = 0.0F;
    float wFar = 0.0F;

    Vec3 ptNear = invViewProjectionMatrix_.transformPointWithW(Vec3{ndcX, ndcY, 0.0F}, wNear);
    if (std::abs(wNear) > 1e-6F) {
        ptNear = ptNear / wNear;
    }

    Vec3 ptFar = invViewProjectionMatrix_.transformPointWithW(Vec3{ndcX, ndcY, 1.0F}, wFar);
    if (std::abs(wFar) > 1e-6F) {
        ptFar = ptFar / wFar;
    }

    const Vec3 dir = (ptFar - ptNear).normalized();
    return Ray(ptNear, dir);
}

bool EditorCamera::project(const Vec3& worldPoint, const Vec3& canvasOrigin, const Vec3& canvasSize,
                           Vec3& outScreenPos, float& outDepth) const noexcept {
    float w = 0.0F;
    const Vec3 clip = viewProjectionMatrix_.transformPointWithW(worldPoint, w);

    if (w <= 0.001F) {
        return false; // Point is behind the camera or on near clipping plane
    }

    outDepth = w;
    const float ndcX = clip.x / w;
    const float ndcY = clip.y / w;

    outScreenPos.x = canvasOrigin.x + (((ndcX + 1.0F) * 0.5F) * canvasSize.x);
    outScreenPos.y = canvasOrigin.y + (((1.0F - ndcY) * 0.5F) * canvasSize.y);
    outScreenPos.z = clip.z / w;
    return true;
}

void EditorCamera::updateVectorsFromAngles() noexcept {
    const float cosPitch = std::cos(pitch_);
    const float sinPitch = std::sin(pitch_);
    const float cosYaw = std::cos(yaw_);
    const float sinYaw = std::sin(yaw_);

    forward_ = Vec3{cosPitch * sinYaw, sinPitch, -cosPitch * cosYaw}.normalized();
    right_ = Vec3::cross(forward_, Vec3::unitY()).normalized();
    up_ = Vec3::cross(right_, forward_).normalized();
}

void EditorCamera::updatePositionFromPivot() noexcept {
    position_ = pivot_ - (forward_ * distance_);
}

void EditorCamera::updateVectorsFromPivot() noexcept {
    const Vec3 dir = pivot_ - position_;
    const float len = dir.length();
    if (len > 1e-4F) {
        distance_ = std::clamp(len, kMinDistance, kMaxDistance);
        forward_ = dir / len;
        pitch_ = std::clamp(std::asin(forward_.y), kMinPitch, kMaxPitch);
        yaw_ = std::atan2(forward_.x, -forward_.z);
        right_ = Vec3::cross(forward_, Vec3::unitY()).normalized();
        up_ = Vec3::cross(right_, forward_).normalized();
    }
}

} // namespace eng::editor
