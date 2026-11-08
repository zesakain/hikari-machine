/// @file editor_camera.h
/// @brief Independent 3D editor camera with perspective projection and studio navigation.
///
/// Purpose:
/// Provides professional 3D viewport navigation (Orbit, Pan, Zoom, and First-Person Fly)
/// completely decoupled from gameplay ECS entities and runtime modes.
///
/// Ownership and Lifetime:
/// Owned by ViewportPanel or EditorWorkspace. Value-semantic or reference usage.
///
/// Thread Safety:
/// Main thread only. Zero dynamic heap allocations in hot-path projection calculations.

#pragma once

#include "core/math.h"

namespace eng::editor {

/// @enum CameraProjectionMode
/// @brief Camera projection type for the editor viewport.
enum class CameraProjectionMode : uint8_t {
    Perspective = 0,
    Orthographic = 1,
};

/// @class EditorCamera
/// @brief Editor-only interactive 3D camera.
class EditorCamera {
public:
    EditorCamera() noexcept;

    /// Resets camera to default framing looking at the world origin.
    void resetToDefault() noexcept;

    /// Frames the camera on a specific world-space target with a bounding radius.
    void frame(const Vec3& target, float radius = 2.0F) noexcept;

    /// Orbits around the current pivot point.
    ///
    /// @param deltaYaw Horizontal mouse delta in radians.
    /// @param deltaPitch Vertical mouse delta in radians.
    void orbit(float deltaYaw, float deltaPitch) noexcept;

    /// Pans the camera and pivot along the camera's local Right and Up plane.
    ///
    /// @param deltaX Horizontal screen delta in pixels.
    /// @param deltaY Vertical screen delta in pixels.
    void pan(float deltaX, float deltaY) noexcept;

    /// Zooms towards or away from the pivot.
    ///
    /// @param scrollDelta Mouse wheel delta.
    void zoom(float scrollDelta) noexcept;

    /// First-person free-look rotation (modifies look direction without orbiting pivot).
    void look(float deltaYaw, float deltaPitch) noexcept;

    /// Free flight movement along local camera axes.
    ///
    /// @param localMovement Vector (X = Right, Y = World Up, Z = Forward).
    /// @param deltaTime Frame duration in seconds.
    /// @param fastMultiplier Multiplier when Shift is held.
    void fly(const Vec3& localMovement, float deltaTime, float fastMultiplier = 1.0F) noexcept;

    /// Updates view, projection, and inverse matrices based on current viewport dimensions.
    void updateMatrices(float viewportWidth, float viewportHeight) noexcept;

    /// Unprojects screen coordinates inside the viewport canvas into a 3D world-space Ray.
    [[nodiscard]] Ray unproject(const Vec3& screenPoint, const Vec3& canvasOrigin,
                                const Vec3& canvasSize) const noexcept;

    /// Projects a 3D world-space point into 2D viewport screen coordinates.
    ///
    /// @param worldPoint Point in world space.
    /// @param canvasOrigin Top-left coordinate of the viewport canvas.
    /// @param canvasSize Dimensions of the viewport canvas.
    /// @param outScreenPos Projected screen coordinates in pixels.
    /// @param outDepth Projected depth (clip space w coordinate).
    /// @return True if point is in front of the camera near plane; false if clipped.
    [[nodiscard]] bool project(const Vec3& worldPoint, const Vec3& canvasOrigin,
                               const Vec3& canvasSize, Vec3& outScreenPos,
                               float& outDepth) const noexcept;

    // --- Accessors & Mutators ---

    [[nodiscard]] const Vec3& getPosition() const noexcept { return position_; }
    void setPosition(const Vec3& pos) noexcept {
        position_ = pos;
        updateVectorsFromPivot();
    }

    [[nodiscard]] const Vec3& getPivot() const noexcept { return pivot_; }
    void setPivot(const Vec3& pivot) noexcept {
        pivot_ = pivot;
        updatePositionFromPivot();
    }

    [[nodiscard]] float getDistance() const noexcept { return distance_; }
    void setDistance(float dist) noexcept;

    [[nodiscard]] float getYaw() const noexcept { return yaw_; }
    [[nodiscard]] float getPitch() const noexcept { return pitch_; }

    [[nodiscard]] const Vec3& getForward() const noexcept { return forward_; }
    [[nodiscard]] const Vec3& getRight() const noexcept { return right_; }
    [[nodiscard]] const Vec3& getUp() const noexcept { return up_; }

    [[nodiscard]] const Mat4& getViewMatrix() const noexcept { return viewMatrix_; }
    [[nodiscard]] const Mat4& getProjectionMatrix() const noexcept { return projectionMatrix_; }
    [[nodiscard]] const Mat4& getViewProjectionMatrix() const noexcept {
        return viewProjectionMatrix_;
    }
    [[nodiscard]] const Mat4& getInverseViewProjectionMatrix() const noexcept {
        return invViewProjectionMatrix_;
    }

    [[nodiscard]] CameraProjectionMode getProjectionMode() const noexcept {
        return projectionMode_;
    }
    void setProjectionMode(CameraProjectionMode mode) noexcept { projectionMode_ = mode; }

    [[nodiscard]] float getFovY() const noexcept { return fovY_; }
    void setFovY(float fovRadians) noexcept { fovY_ = fovRadians; }

    [[nodiscard]] float getNearPlane() const noexcept { return nearPlane_; }
    void setNearPlane(float nearPlane) noexcept { nearPlane_ = nearPlane; }

    [[nodiscard]] float getFarPlane() const noexcept { return farPlane_; }
    void setFarPlane(float farPlane) noexcept { farPlane_ = farPlane; }

    [[nodiscard]] float getMoveSpeed() const noexcept { return moveSpeed_; }
    void setMoveSpeed(float speed) noexcept { moveSpeed_ = speed; }

    [[nodiscard]] float getMouseSensitivity() const noexcept { return mouseSensitivity_; }
    void setMouseSensitivity(float sens) noexcept { mouseSensitivity_ = sens; }

private:
    void updateVectorsFromAngles() noexcept;
    void updatePositionFromPivot() noexcept;
    void updateVectorsFromPivot() noexcept;

    Vec3 position_{5.0F, 5.0F, 8.0F};
    Vec3 pivot_{0.0F, 0.0F, 0.0F};
    Vec3 forward_{0.0F, 0.0F, -1.0F};
    Vec3 right_{1.0F, 0.0F, 0.0F};
    Vec3 up_{0.0F, 1.0F, 0.0F};

    float distance_{10.0F};
    float yaw_{-0.785398F};   // -45 degrees
    float pitch_{-0.400000F}; // ~ -23 degrees

    CameraProjectionMode projectionMode_{CameraProjectionMode::Perspective};
    float fovY_{1.047198F}; // 60 degrees in radians
    float orthoSize_{10.0F};
    float nearPlane_{0.1F};
    float farPlane_{1000.0F};
    float aspectRatio_{16.0F / 9.0F};

    float moveSpeed_{6.0F};          // meters / second
    float mouseSensitivity_{0.003F}; // radians / pixel

    Mat4 viewMatrix_{Mat4::identity()};
    Mat4 projectionMatrix_{Mat4::identity()};
    Mat4 viewProjectionMatrix_{Mat4::identity()};
    Mat4 invViewProjectionMatrix_{Mat4::identity()};
};

} // namespace eng::editor
