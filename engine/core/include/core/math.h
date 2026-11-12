/// @file math.h
/// @brief Fundamental 3D linear algebra and transform primitives.
///
/// Purpose:
/// Provides lightweight, zero-overhead, POD vector, quaternion, matrix, and transform types
/// for simulation, physics bridging, and skeletal animation.
///
/// Ownership and Lifetime:
/// Value types with standard layout, trivial destructibility, and zero heap allocation.
///
/// Thread-safety:
/// Pure mathematical value types; fully thread-safe for concurrent read access.
///
/// Performance:
/// Standard layout structs suitable for contiguous cache storage and SIMD layout.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>

namespace eng {

/// @struct Vec2
/// @brief 2D single-precision floating-point vector.
struct Vec2 {
    float x{0.0F};
    float y{0.0F};

    constexpr Vec2() noexcept = default;
    constexpr Vec2(float inX, float inY) noexcept : x(inX), y(inY) {}
    constexpr explicit Vec2(float scalar) noexcept : x(scalar), y(scalar) {}

    [[nodiscard]] static constexpr Vec2 zero() noexcept { return Vec2{0.0F, 0.0F}; }
    [[nodiscard]] static constexpr Vec2 one() noexcept { return Vec2{1.0F, 1.0F}; }

    [[nodiscard]] constexpr Vec2 operator+(const Vec2& rhs) const noexcept {
        return Vec2{x + rhs.x, y + rhs.y};
    }
    [[nodiscard]] constexpr Vec2 operator-(const Vec2& rhs) const noexcept {
        return Vec2{x - rhs.x, y - rhs.y};
    }
    [[nodiscard]] constexpr Vec2 operator*(float scalar) const noexcept {
        return Vec2{x * scalar, y * scalar};
    }
    [[nodiscard]] constexpr Vec2 operator/(float scalar) const noexcept {
        const float inv = 1.0F / scalar;
        return Vec2{x * inv, y * inv};
    }
    constexpr Vec2& operator+=(const Vec2& rhs) noexcept {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }
    constexpr Vec2& operator-=(const Vec2& rhs) noexcept {
        x -= rhs.x;
        y -= rhs.y;
        return *this;
    }
    constexpr Vec2& operator*=(float scalar) noexcept {
        x *= scalar;
        y *= scalar;
        return *this;
    }
    [[nodiscard]] constexpr float lengthSq() const noexcept { return (x * x) + (y * y); }
    [[nodiscard]] float length() const noexcept { return std::sqrt(lengthSq()); }
    [[nodiscard]] bool isNearlyEqual(const Vec2& rhs, float epsilon = 1e-5F) const noexcept {
        return std::abs(x - rhs.x) <= epsilon && std::abs(y - rhs.y) <= epsilon;
    }
};

/// @struct Vec3
/// @brief 3D single-precision floating-point vector.
struct Vec3 {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};

    constexpr Vec3() noexcept = default;
    constexpr Vec3(float inX, float inY, float inZ) noexcept : x(inX), y(inY), z(inZ) {}
    constexpr explicit Vec3(float scalar) noexcept : x(scalar), y(scalar), z(scalar) {}

    [[nodiscard]] static constexpr Vec3 zero() noexcept { return Vec3{0.0F, 0.0F, 0.0F}; }
    [[nodiscard]] static constexpr Vec3 one() noexcept { return Vec3{1.0F, 1.0F, 1.0F}; }
    [[nodiscard]] static constexpr Vec3 unitX() noexcept { return Vec3{1.0F, 0.0F, 0.0F}; }
    [[nodiscard]] static constexpr Vec3 unitY() noexcept { return Vec3{0.0F, 1.0F, 0.0F}; }
    [[nodiscard]] static constexpr Vec3 unitZ() noexcept { return Vec3{0.0F, 0.0F, 1.0F}; }

    [[nodiscard]] constexpr Vec3 operator+(const Vec3& rhs) const noexcept {
        return Vec3{x + rhs.x, y + rhs.y, z + rhs.z};
    }

    [[nodiscard]] constexpr Vec3 operator-(const Vec3& rhs) const noexcept {
        return Vec3{x - rhs.x, y - rhs.y, z - rhs.z};
    }

    [[nodiscard]] constexpr Vec3 operator-() const noexcept { return Vec3{-x, -y, -z}; }

    [[nodiscard]] constexpr Vec3 operator*(float scalar) const noexcept {
        return Vec3{x * scalar, y * scalar, z * scalar};
    }

    [[nodiscard]] constexpr Vec3 operator*(const Vec3& rhs) const noexcept {
        return Vec3{x * rhs.x, y * rhs.y, z * rhs.z};
    }

    [[nodiscard]] constexpr Vec3 operator/(float scalar) const noexcept {
        const float inv = 1.0F / scalar;
        return Vec3{x * inv, y * inv, z * inv};
    }

    [[nodiscard]] constexpr Vec3 operator/(const Vec3& rhs) const noexcept {
        return Vec3{x / rhs.x, y / rhs.y, z / rhs.z};
    }

    constexpr Vec3& operator+=(const Vec3& rhs) noexcept {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        return *this;
    }

    constexpr Vec3& operator-=(const Vec3& rhs) noexcept {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        return *this;
    }

    constexpr Vec3& operator*=(float scalar) noexcept {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }

    constexpr Vec3& operator/=(float scalar) noexcept {
        const float inv = 1.0F / scalar;
        x *= inv;
        y *= inv;
        z *= inv;
        return *this;
    }

    [[nodiscard]] constexpr float lengthSq() const noexcept { return (x * x) + (y * y) + (z * z); }

    [[nodiscard]] float length() const noexcept { return std::sqrt(lengthSq()); }

    [[nodiscard]] Vec3 normalized() const noexcept {
        const float lenSq = lengthSq();
        if (lenSq > 1e-12F) {
            const float invLen = 1.0F / std::sqrt(lenSq);
            return Vec3{x * invLen, y * invLen, z * invLen};
        }
        return zero();
    }

    [[nodiscard]] static constexpr float dot(const Vec3& a, const Vec3& b) noexcept {
        return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
    }

    [[nodiscard]] static constexpr Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
        return Vec3{(a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z),
                    (a.x * b.y) - (a.y * b.x)};
    }

    [[nodiscard]] static constexpr Vec3 lerp(const Vec3& a, const Vec3& b, float t) noexcept {
        return Vec3{a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t), a.z + ((b.z - a.z) * t)};
    }

    [[nodiscard]] bool isNearlyEqual(const Vec3& rhs, float epsilon = 1e-5F) const noexcept {
        return std::abs(x - rhs.x) <= epsilon && std::abs(y - rhs.y) <= epsilon &&
               std::abs(z - rhs.z) <= epsilon;
    }
};

[[nodiscard]] constexpr Vec3 operator*(float scalar, const Vec3& v) noexcept {
    return v * scalar;
}

/// @struct Vec4
/// @brief 4D single-precision floating-point vector.
struct Vec4 {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
    float w{0.0F};

    constexpr Vec4() noexcept = default;
    constexpr Vec4(float inX, float inY, float inZ, float inW) noexcept
        : x(inX), y(inY), z(inZ), w(inW) {}
    constexpr explicit Vec4(float scalar) noexcept : x(scalar), y(scalar), z(scalar), w(scalar) {}

    [[nodiscard]] static constexpr Vec4 zero() noexcept { return Vec4{0.0F, 0.0F, 0.0F, 0.0F}; }
    [[nodiscard]] static constexpr Vec4 one() noexcept { return Vec4{1.0F, 1.0F, 1.0F, 1.0F}; }

    [[nodiscard]] constexpr Vec4 operator+(const Vec4& rhs) const noexcept {
        return Vec4{x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w};
    }
    [[nodiscard]] constexpr Vec4 operator-(const Vec4& rhs) const noexcept {
        return Vec4{x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w};
    }
    [[nodiscard]] constexpr Vec4 operator*(float scalar) const noexcept {
        return Vec4{x * scalar, y * scalar, z * scalar, w * scalar};
    }
    [[nodiscard]] constexpr Vec4 operator/(float scalar) const noexcept {
        const float inv = 1.0F / scalar;
        return Vec4{x * inv, y * inv, z * inv, w * inv};
    }
    [[nodiscard]] bool isNearlyEqual(const Vec4& rhs, float epsilon = 1e-5F) const noexcept {
        return std::abs(x - rhs.x) <= epsilon && std::abs(y - rhs.y) <= epsilon &&
               std::abs(z - rhs.z) <= epsilon && std::abs(w - rhs.w) <= epsilon;
    }
};

/// @struct Quat
/// @brief Unit quaternion for 3D spatial rotations (x, y, z, w).
struct Quat {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
    float w{1.0F};

    constexpr Quat() noexcept = default;
    constexpr Quat(float inX, float inY, float inZ, float inW) noexcept
        : x(inX), y(inY), z(inZ), w(inW) {}

    [[nodiscard]] static constexpr Quat identity() noexcept { return Quat{0.0F, 0.0F, 0.0F, 1.0F}; }

    [[nodiscard]] static Quat fromAxisAngle(const Vec3& axis, float radians) noexcept {
        const float halfAngle = radians * 0.5F;
        const float sinHalf = std::sin(halfAngle);
        const Vec3 normAxis = axis.normalized();
        return Quat{normAxis.x * sinHalf, normAxis.y * sinHalf, normAxis.z * sinHalf,
                    std::cos(halfAngle)};
    }

    /// Extracts orientation quaternion from a 4x4 rotation/affine matrix.
    [[nodiscard]] static Quat fromRotationMatrix(const struct Mat4& mat) noexcept;

    [[nodiscard]] constexpr float lengthSq() const noexcept {
        return (x * x) + (y * y) + (z * z) + (w * w);
    }

    [[nodiscard]] float length() const noexcept { return std::sqrt(lengthSq()); }

    [[nodiscard]] Quat normalized() const noexcept {
        const float lenSq = lengthSq();
        if (lenSq > 1e-12F) {
            const float invLen = 1.0F / std::sqrt(lenSq);
            return Quat{x * invLen, y * invLen, z * invLen, w * invLen};
        }
        return identity();
    }

    [[nodiscard]] constexpr Quat conjugate() const noexcept { return Quat{-x, -y, -z, w}; }

    [[nodiscard]] Quat inverse() const noexcept {
        const float lenSq = lengthSq();
        if (lenSq > 1e-12F) {
            const float invLenSq = 1.0F / lenSq;
            return Quat{-x * invLenSq, -y * invLenSq, -z * invLenSq, w * invLenSq};
        }
        return identity();
    }

    [[nodiscard]] constexpr Quat operator*(const Quat& rhs) const noexcept {
        return Quat{(w * rhs.x) + (x * rhs.w) + (y * rhs.z) - (z * rhs.y),
                    (w * rhs.y) - (x * rhs.z) + (y * rhs.w) + (z * rhs.x),
                    (w * rhs.z) + (x * rhs.y) - (y * rhs.x) + (z * rhs.w),
                    (w * rhs.w) - (x * rhs.x) - (y * rhs.y) - (z * rhs.z)};
    }

    [[nodiscard]] Vec3 rotate(const Vec3& v) const noexcept {
        // v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v)
        const Vec3 qVec{x, y, z};
        const Vec3 t = 2.0F * Vec3::cross(qVec, v);
        return v + (w * t) + Vec3::cross(qVec, t);
    }

    [[nodiscard]] static constexpr float dot(const Quat& a, const Quat& b) noexcept {
        return (a.x * b.x) + (a.y * b.y) + (a.z * b.z) + (a.w * b.w);
    }

    [[nodiscard]] constexpr float dot(const Quat& other) const noexcept {
        return dot(*this, other);
    }

    /// Spherical linear interpolation between two quaternions.
    [[nodiscard]] static Quat slerp(const Quat& a, const Quat& b, float t) noexcept {
        float cosTheta = dot(a, b);
        Quat target = b;

        // Take shortest path
        if (cosTheta < 0.0F) {
            cosTheta = -cosTheta;
            target = Quat{-b.x, -b.y, -b.z, -b.w};
        }

        // Clamp to prevent numerical domain error in acos
        cosTheta = std::clamp(cosTheta, -1.0F, 1.0F);

        // If quaternions are very close, use linear interpolation to avoid division by zero
        if (cosTheta > 0.9995F) {
            return nlerp(a, target, t);
        }

        const float theta = std::acos(cosTheta);
        const float sinTheta = std::sin(theta);
        const float w1 = std::sin((1.0F - t) * theta) / sinTheta;
        const float w2 = std::sin(t * theta) / sinTheta;

        return Quat{(a.x * w1) + (target.x * w2), (a.y * w1) + (target.y * w2),
                    (a.z * w1) + (target.z * w2), (a.w * w1) + (target.w * w2)};
    }

    /// Normalized linear interpolation between two quaternions.
    [[nodiscard]] static Quat nlerp(const Quat& a, const Quat& b, float t) noexcept {
        float cosTheta = dot(a, b);
        Quat target = b;
        if (cosTheta < 0.0F) {
            target = Quat{-b.x, -b.y, -b.z, -b.w};
        }

        Quat result{a.x + ((target.x - a.x) * t), a.y + ((target.y - a.y) * t),
                    a.z + ((target.z - a.z) * t), a.w + ((target.w - a.w) * t)};
        return result.normalized();
    }

    [[nodiscard]] bool isNearlyEqual(const Quat& rhs, float epsilon = 1e-5F) const noexcept {
        return (std::abs(x - rhs.x) <= epsilon && std::abs(y - rhs.y) <= epsilon &&
                std::abs(z - rhs.z) <= epsilon && std::abs(w - rhs.w) <= epsilon) ||
               (std::abs(x + rhs.x) <= epsilon && std::abs(y + rhs.y) <= epsilon &&
                std::abs(z + rhs.z) <= epsilon && std::abs(w + rhs.w) <= epsilon);
    }
};

/// @struct Mat4
/// @brief 4x4 column-major single-precision transformation matrix.
struct Mat4 {
    // Array order: [col0_x, col0_y, col0_z, col0_w, col1_x, ...]
    float m[16]{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
                0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};

    constexpr Mat4() noexcept = default;

    [[nodiscard]] static constexpr Mat4 identity() noexcept { return Mat4{}; }

    [[nodiscard]] static constexpr Mat4 fromTranslation(const Vec3& translation) noexcept {
        Mat4 res{};
        res.m[12] = translation.x;
        res.m[13] = translation.y;
        res.m[14] = translation.z;
        return res;
    }

    [[nodiscard]] static constexpr Mat4 fromScale(const Vec3& scale) noexcept {
        Mat4 res{};
        res.m[0] = scale.x;
        res.m[5] = scale.y;
        res.m[10] = scale.z;
        return res;
    }

    [[nodiscard]] static Mat4 fromRotation(const Quat& q) noexcept {
        const Quat n = q.normalized();
        const float xx = n.x * n.x;
        const float yy = n.y * n.y;
        const float zz = n.z * n.z;
        const float xy = n.x * n.y;
        const float xz = n.x * n.z;
        const float yz = n.y * n.z;
        const float wx = n.w * n.x;
        const float wy = n.w * n.y;
        const float wz = n.w * n.z;

        Mat4 res{};
        res.m[0] = 1.0F - (2.0F * (yy + zz));
        res.m[1] = 2.0F * (xy + wz);
        res.m[2] = 2.0F * (xz - wy);
        res.m[3] = 0.0F;

        res.m[4] = 2.0F * (xy - wz);
        res.m[5] = 1.0F - (2.0F * (xx + zz));
        res.m[6] = 2.0F * (yz + wx);
        res.m[7] = 0.0F;

        res.m[8] = 2.0F * (xz + wy);
        res.m[9] = 2.0F * (yz - wx);
        res.m[10] = 1.0F - (2.0F * (xx + yy));
        res.m[11] = 0.0F;

        res.m[12] = 0.0F;
        res.m[13] = 0.0F;
        res.m[14] = 0.0F;
        res.m[15] = 1.0F;
        return res;
    }

    [[nodiscard]] static Mat4 fromTransform(const Vec3& translation, const Quat& rotation,
                                            const Vec3& scale) noexcept {
        Mat4 r = fromRotation(rotation);

        // Apply scale to columns 0, 1, 2
        r.m[0] *= scale.x;
        r.m[1] *= scale.x;
        r.m[2] *= scale.x;

        r.m[4] *= scale.y;
        r.m[5] *= scale.y;
        r.m[6] *= scale.y;

        r.m[8] *= scale.z;
        r.m[9] *= scale.z;
        r.m[10] *= scale.z;

        // Apply translation to column 3
        r.m[12] = translation.x;
        r.m[13] = translation.y;
        r.m[14] = translation.z;

        return r;
    }

    [[nodiscard]] Mat4 operator*(const Mat4& rhs) const noexcept {
        Mat4 res{};
        for (size_t c = 0; c < 4; ++c) {
            for (size_t r = 0; r < 4; ++r) {
                res.m[(c * 4) + r] = (m[r] * rhs.m[c * 4]) + (m[4 + r] * rhs.m[(c * 4) + 1]) +
                                     (m[8 + r] * rhs.m[(c * 4) + 2]) +
                                     (m[12 + r] * rhs.m[(c * 4) + 3]);
            }
        }
        return res;
    }

    [[nodiscard]] Vec3 transformPoint(const Vec3& p) const noexcept {
        return Vec3{(m[0] * p.x) + (m[4] * p.y) + (m[8] * p.z) + m[12],
                    (m[1] * p.x) + (m[5] * p.y) + (m[9] * p.z) + m[13],
                    (m[2] * p.x) + (m[6] * p.y) + (m[10] * p.z) + m[14]};
    }

    [[nodiscard]] Vec3 transformVector(const Vec3& v) const noexcept {
        return Vec3{(m[0] * v.x) + (m[4] * v.y) + (m[8] * v.z),
                    (m[1] * v.x) + (m[5] * v.y) + (m[9] * v.z),
                    (m[2] * v.x) + (m[6] * v.y) + (m[10] * v.z)};
    }

    /// Fast inverse for affine transform matrices (orthogonal 3x3 rotation/scale + translation).
    [[nodiscard]] Mat4 inverseAffine() const noexcept {
        Mat4 res{};
        // Transpose 3x3 upper left
        res.m[0] = m[0];
        res.m[1] = m[4];
        res.m[2] = m[8];

        res.m[4] = m[1];
        res.m[5] = m[5];
        res.m[6] = m[9];

        res.m[8] = m[2];
        res.m[9] = m[6];
        res.m[10] = m[10];

        // Inverse translation: -R^T * T
        const Vec3 t{m[12], m[13], m[14]};
        res.m[12] = -((res.m[0] * t.x) + (res.m[4] * t.y) + (res.m[8] * t.z));
        res.m[13] = -((res.m[1] * t.x) + (res.m[5] * t.y) + (res.m[9] * t.z));
        res.m[14] = -((res.m[2] * t.x) + (res.m[6] * t.y) + (res.m[10] * t.z));
        res.m[15] = 1.0F;

        return res;
    }

    [[nodiscard]] constexpr float at(size_t row, size_t col) const noexcept {
        return m[(col * 4) + row];
    }

    [[nodiscard]] bool isNearlyEqual(const Mat4& rhs, float epsilon = 1e-5F) const noexcept {
        for (size_t i = 0; i < 16; ++i) {
            if (std::abs(m[i] - rhs.m[i]) > epsilon) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool operator==(const Mat4& rhs) const noexcept {
        for (size_t i = 0; i < 16; ++i) {
            if (m[i] != rhs.m[i]) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool operator!=(const Mat4& rhs) const noexcept { return !(*this == rhs); }

    /// Creates a perspective projection matrix (Vulkan [0, 1] depth range).
    [[nodiscard]] static Mat4 perspective(float fovYRadians, float aspect, float zNear,
                                          float zFar) noexcept {
        const float tanHalfFov = std::tan(fovYRadians * 0.5F);
        const float f = (tanHalfFov > 1e-6F) ? (1.0F / tanHalfFov) : 1.0F;
        Mat4 res{};
        std::fill(std::begin(res.m), std::end(res.m), 0.0F);

        res.m[0] = f / (aspect > 1e-6F ? aspect : 1.0F);
        res.m[5] = f;
        res.m[10] = zFar / (zNear - zFar);
        res.m[11] = -1.0F;
        res.m[14] = (zNear * zFar) / (zNear - zFar);
        return res;
    }

    /// Creates an orthographic projection matrix (Vulkan [0, 1] depth range).
    [[nodiscard]] static Mat4 orthographic(float left, float right, float bottom, float top,
                                           float zNear, float zFar) noexcept {
        Mat4 res{};
        std::fill(std::begin(res.m), std::end(res.m), 0.0F);

        res.m[0] = 2.0F / (right - left);
        res.m[5] = 2.0F / (top - bottom);
        res.m[10] = 1.0F / (zNear - zFar);
        res.m[12] = -(right + left) / (right - left);
        res.m[13] = -(top + bottom) / (top - bottom);
        res.m[14] = zNear / (zNear - zFar);
        res.m[15] = 1.0F;
        return res;
    }

    /// Creates a right-handed view matrix looking from eye towards target.
    [[nodiscard]] static Mat4 lookAt(const Vec3& eye, const Vec3& target,
                                     const Vec3& up = Vec3::unitY()) noexcept {
        const Vec3 f = (target - eye).normalized();
        const Vec3 r = Vec3::cross(f, up).normalized();
        const Vec3 u = Vec3::cross(r, f);

        Mat4 res{};
        res.m[0] = r.x;
        res.m[1] = u.x;
        res.m[2] = -f.x;
        res.m[3] = 0.0F;

        res.m[4] = r.y;
        res.m[5] = u.y;
        res.m[6] = -f.y;
        res.m[7] = 0.0F;

        res.m[8] = r.z;
        res.m[9] = u.z;
        res.m[10] = -f.z;
        res.m[11] = 0.0F;

        res.m[12] = -Vec3::dot(r, eye);
        res.m[13] = -Vec3::dot(u, eye);
        res.m[14] = Vec3::dot(f, eye);
        res.m[15] = 1.0F;
        return res;
    }

    /// Transforms a 3D point and returns the projected homogeneous w coordinate.
    [[nodiscard]] Vec3 transformPointWithW(const Vec3& p, float& outW) const noexcept {
        outW = (m[3] * p.x) + (m[7] * p.y) + (m[11] * p.z) + m[15];
        return Vec3{(m[0] * p.x) + (m[4] * p.y) + (m[8] * p.z) + m[12],
                    (m[1] * p.x) + (m[5] * p.y) + (m[9] * p.z) + m[13],
                    (m[2] * p.x) + (m[6] * p.y) + (m[10] * p.z) + m[14]};
    }

    /// Full general 4x4 matrix inversion. Returns std::nullopt if matrix is singular.
    [[nodiscard]] std::optional<Mat4> inverse() const noexcept {
        const float s0 = (m[0] * m[5]) - (m[4] * m[1]);
        const float s1 = (m[0] * m[6]) - (m[4] * m[2]);
        const float s2 = (m[0] * m[7]) - (m[4] * m[3]);
        const float s3 = (m[1] * m[6]) - (m[5] * m[2]);
        const float s4 = (m[1] * m[7]) - (m[5] * m[3]);
        const float s5 = (m[2] * m[7]) - (m[6] * m[3]);

        const float c5 = (m[10] * m[15]) - (m[14] * m[11]);
        const float c4 = (m[9] * m[15]) - (m[13] * m[11]);
        const float c3 = (m[9] * m[14]) - (m[13] * m[10]);
        const float c2 = (m[8] * m[15]) - (m[12] * m[11]);
        const float c1 = (m[8] * m[14]) - (m[12] * m[10]);
        const float c0 = (m[8] * m[13]) - (m[12] * m[9]);

        const float det = (s0 * c5) - (s1 * c4) + (s2 * c3) + (s3 * c2) - (s4 * c1) + (s5 * c0);
        if (std::abs(det) < 1e-12F) {
            return std::nullopt;
        }

        const float invDet = 1.0F / det;
        Mat4 res{};

        res.m[0] = ((m[5] * c5) - (m[6] * c4) + (m[7] * c3)) * invDet;
        res.m[1] = ((-m[1] * c5) + (m[2] * c4) - (m[3] * c3)) * invDet;
        res.m[2] = ((m[13] * s5) - (m[14] * s4) + (m[15] * s3)) * invDet;
        res.m[3] = ((-m[9] * s5) + (m[10] * s4) - (m[11] * s3)) * invDet;

        res.m[4] = ((-m[4] * c5) + (m[6] * c2) - (m[7] * c1)) * invDet;
        res.m[5] = ((m[0] * c5) - (m[2] * c2) + (m[3] * c1)) * invDet;
        res.m[6] = ((-m[12] * s5) + (m[14] * s2) - (m[15] * s1)) * invDet;
        res.m[7] = ((m[8] * s5) - (m[10] * s2) + (m[11] * s1)) * invDet;

        res.m[8] = ((m[4] * c4) - (m[5] * c2) + (m[7] * c0)) * invDet;
        res.m[9] = ((-m[0] * c4) + (m[1] * c2) - (m[3] * c0)) * invDet;
        res.m[10] = ((m[12] * s4) - (m[13] * s2) + (m[15] * s0)) * invDet;
        res.m[11] = ((-m[8] * s4) + (m[9] * s2) - (m[11] * s0)) * invDet;

        res.m[12] = ((-m[4] * c3) + (m[5] * c1) - (m[6] * c0)) * invDet;
        res.m[13] = ((m[0] * c3) - (m[1] * c1) + (m[2] * c0)) * invDet;
        res.m[14] = ((-m[12] * s3) + (m[13] * s1) - (m[14] * s0)) * invDet;
        res.m[15] = ((m[8] * s3) - (m[9] * s1) + (m[10] * s0)) * invDet;

        return res;
    }

    /// Decomposes affine matrix into position, orientation quaternion, and scale.
    [[nodiscard]] std::optional<struct Transform> decompose() const noexcept;
};

/// @struct Ray
/// @brief 3D mathematical ray with origin and normalized direction.
struct Ray {
    Vec3 origin{Vec3::zero()};
    Vec3 direction{Vec3::unitZ()};

    constexpr Ray() noexcept = default;
    constexpr Ray(const Vec3& o, const Vec3& d) noexcept : origin(o), direction(d) {}

    [[nodiscard]] Vec3 getPoint(float t) const noexcept { return origin + (direction * t); }

    /// Tests intersection with a plane defined by a point and normal.
    [[nodiscard]] std::optional<float> intersectPlane(const Vec3& planePoint,
                                                      const Vec3& planeNormal) const noexcept {
        const float denom = Vec3::dot(direction, planeNormal);
        if (std::abs(denom) < 1e-6F) {
            return std::nullopt; // Parallel
        }
        const float t = Vec3::dot(planePoint - origin, planeNormal) / denom;
        if (t < 0.0F) {
            return std::nullopt; // Behind ray origin
        }
        return t;
    }

    /// Tests intersection with a sphere (center, radius).
    [[nodiscard]] std::optional<float> intersectSphere(const Vec3& center,
                                                       float radius) const noexcept {
        const Vec3 oc = origin - center;
        const float b = Vec3::dot(oc, direction);
        const float c = Vec3::dot(oc, oc) - (radius * radius);
        const float discriminant = (b * b) - c;

        if (discriminant < 0.0F) {
            return std::nullopt;
        }

        const float sqrtDisc = std::sqrt(discriminant);
        const float t0 = -b - sqrtDisc;
        if (t0 >= 0.0F) {
            return t0;
        }
        const float t1 = -b + sqrtDisc;
        if (t1 >= 0.0F) {
            return t1;
        }
        return std::nullopt;
    }

    /// Tests intersection with an Axis-Aligned Bounding Box (AABB) [minBound, maxBound].
    [[nodiscard]] std::optional<float> intersectAABB(const Vec3& minBound,
                                                     const Vec3& maxBound) const noexcept {
        float tMin = 0.0F;
        float tMax = 1e30F;

        const float origins[3] = {origin.x, origin.y, origin.z};
        const float dirs[3] = {direction.x, direction.y, direction.z};
        const float mins[3] = {minBound.x, minBound.y, minBound.z};
        const float maxs[3] = {maxBound.x, maxBound.y, maxBound.z};

        for (int i = 0; i < 3; ++i) {
            if (std::abs(dirs[i]) < 1e-6F) {
                if (origins[i] < mins[i] || origins[i] > maxs[i]) {
                    return std::nullopt;
                }
            } else {
                const float invD = 1.0F / dirs[i];
                float t0 = (mins[i] - origins[i]) * invD;
                float t1 = (maxs[i] - origins[i]) * invD;
                if (t0 > t1) {
                    std::swap(t0, t1);
                }
                tMin = std::max(tMin, t0);
                tMax = std::min(tMax, t1);
                if (tMax < tMin) {
                    return std::nullopt;
                }
            }
        }
        return tMin;
    }

    /// Tests intersection with an AABB struct.
    [[nodiscard]] std::optional<float> intersectAABB(const struct AABB& aabb) const noexcept;
};

/// @struct AABB
/// @brief 3D Axis-Aligned Bounding Box.
struct AABB {
    Vec3 minBound{1e30F, 1e30F, 1e30F};
    Vec3 maxBound{-1e30F, -1e30F, -1e30F};

    constexpr AABB() noexcept = default;
    constexpr AABB(const Vec3& minB, const Vec3& maxB) noexcept : minBound(minB), maxBound(maxB) {}

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return minBound.x <= maxBound.x && minBound.y <= maxBound.y && minBound.z <= maxBound.z;
    }

    [[nodiscard]] constexpr Vec3 getCenter() const noexcept { return (minBound + maxBound) * 0.5F; }

    [[nodiscard]] constexpr Vec3 getExtents() const noexcept {
        return (maxBound - minBound) * 0.5F;
    }

    [[nodiscard]] constexpr Vec3 getSize() const noexcept { return maxBound - minBound; }

    void expand(const Vec3& point) noexcept {
        minBound.x = std::min(minBound.x, point.x);
        minBound.y = std::min(minBound.y, point.y);
        minBound.z = std::min(minBound.z, point.z);

        maxBound.x = std::max(maxBound.x, point.x);
        maxBound.y = std::max(maxBound.y, point.y);
        maxBound.z = std::max(maxBound.z, point.z);
    }

    void expand(const AABB& other) noexcept {
        if (!other.isValid()) {
            return;
        }
        expand(other.minBound);
        expand(other.maxBound);
    }

    [[nodiscard]] AABB transform(const Mat4& m) const noexcept {
        if (!isValid()) {
            return *this;
        }
        AABB res;
        const Vec3 corners[8] = {
            Vec3{minBound.x, minBound.y, minBound.z}, Vec3{maxBound.x, minBound.y, minBound.z},
            Vec3{maxBound.x, maxBound.y, minBound.z}, Vec3{minBound.x, maxBound.y, minBound.z},
            Vec3{minBound.x, minBound.y, maxBound.z}, Vec3{maxBound.x, minBound.y, maxBound.z},
            Vec3{maxBound.x, maxBound.y, maxBound.z}, Vec3{minBound.x, maxBound.y, maxBound.z},
        };
        for (const auto& c : corners) {
            res.expand(m.transformPoint(c));
        }
        return res;
    }
};

inline std::optional<float> Ray::intersectAABB(const AABB& aabb) const noexcept {
    if (!aabb.isValid()) {
        return std::nullopt;
    }
    return intersectAABB(aabb.minBound, aabb.maxBound);
}

/// @struct Plane
/// @brief 3D plane in Hessian normal form: normal.dot(p) + distance = 0.
struct Plane {
    Vec3 normal{0.0F, 1.0F, 0.0F};
    float distance{0.0F};

    constexpr Plane() noexcept = default;
    constexpr Plane(const Vec3& inNormal, float inDistance) noexcept
        : normal(inNormal), distance(inDistance) {}
    constexpr Plane(float inA, float inB, float inC, float inD) noexcept
        : normal(inA, inB, inC), distance(inD) {}

    [[nodiscard]] Plane normalized() const noexcept {
        const float lenSq = normal.lengthSq();
        if (lenSq > 1e-12F) {
            const float invLen = 1.0F / std::sqrt(lenSq);
            return Plane{normal * invLen, distance * invLen};
        }
        return *this;
    }

    [[nodiscard]] constexpr float signedDistance(const Vec3& point) const noexcept {
        return Vec3::dot(normal, point) + distance;
    }
};

/// @struct Frustum
/// @brief Six-plane viewing frustum for CPU-side visibility and frustum culling.
struct Frustum {
    enum PlaneIndex : uint8_t {
        Left = 0,
        Right = 1,
        Bottom = 2,
        Top = 3,
        Near = 4,
        Far = 5,
        Count = 6
    };

    Plane planes[6]{};

    constexpr Frustum() noexcept = default;

    /// Extracts normalized frustum planes from a column-major view-projection matrix (Vulkan [0, 1]
    /// depth range).
    [[nodiscard]] static Frustum fromViewProjection(const Mat4& m) noexcept {
        Frustum f;
        // In column-major Mat4:
        // row 0: m[0], m[4], m[8], m[12]
        // row 1: m[1], m[5], m[9], m[13]
        // row 2: m[2], m[6], m[10], m[14]
        // row 3: m[3], m[7], m[11], m[15]

        // Left plane: row 3 + row 0 (x >= -w)
        f.planes[Left] =
            Plane(m.m[3] + m.m[0], m.m[7] + m.m[4], m.m[11] + m.m[8], m.m[15] + m.m[12])
                .normalized();
        // Right plane: row 3 - row 0 (x <= w)
        f.planes[Right] =
            Plane(m.m[3] - m.m[0], m.m[7] - m.m[4], m.m[11] - m.m[8], m.m[15] - m.m[12])
                .normalized();
        // Bottom plane: row 3 + row 1 (y >= -w)
        f.planes[Bottom] =
            Plane(m.m[3] + m.m[1], m.m[7] + m.m[5], m.m[11] + m.m[9], m.m[15] + m.m[13])
                .normalized();
        // Top plane: row 3 - row 1 (y <= w)
        f.planes[Top] = Plane(m.m[3] - m.m[1], m.m[7] - m.m[5], m.m[11] - m.m[9], m.m[15] - m.m[13])
                            .normalized();
        // Near plane: For Vulkan [0, 1] depth range (z >= 0 -> row 2)
        f.planes[Near] = Plane(m.m[2], m.m[6], m.m[10], m.m[14]).normalized();
        // Far plane: row 3 - row 2 (z <= w)
        f.planes[Far] =
            Plane(m.m[3] - m.m[2], m.m[7] - m.m[6], m.m[11] - m.m[10], m.m[15] - m.m[14])
                .normalized();

        return f;
    }

    /// Evaluates whether an AABB intersects or is inside the frustum.
    /// If the AABB is invalid or uninitialized, returns true (conservative fallback).
    [[nodiscard]] bool intersectsAABB(const AABB& aabb) const noexcept {
        if (!aabb.isValid()) {
            return true;
        }
        const Vec3 center = aabb.getCenter();
        const Vec3 extents = aabb.getExtents();

        for (const auto& plane : planes) {
            const float r = (std::abs(plane.normal.x) * extents.x) +
                            (std::abs(plane.normal.y) * extents.y) +
                            (std::abs(plane.normal.z) * extents.z);
            const float d = plane.signedDistance(center);
            if (d < -r) {
                return false;
            }
        }
        return true;
    }

    /// Evaluates whether a bounding sphere intersects or is inside the frustum.
    [[nodiscard]] bool intersectsSphere(const Vec3& center, float radius) const noexcept {
        for (const auto& plane : planes) {
            if (plane.signedDistance(center) < -radius) {
                return false;
            }
        }
        return true;
    }
};

/// @struct Transform
/// @brief Spatial transformation combining position, orientation, and non-uniform scale.
struct Transform {
    Vec3 position{Vec3::zero()};
    Quat rotation{Quat::identity()};
    Vec3 scale{Vec3::one()};

    constexpr Transform() noexcept = default;
    constexpr explicit Transform(const Vec3& pos) noexcept : position(pos) {}
    constexpr Transform(const Vec3& pos, const Quat& rot, const Vec3& scl = Vec3::one()) noexcept
        : position(pos), rotation(rot), scale(scl) {}

    [[nodiscard]] static constexpr Transform identity() noexcept { return Transform{}; }

    [[nodiscard]] Mat4 toMatrix() const noexcept {
        return Mat4::fromTransform(position, rotation, scale);
    }

    [[nodiscard]] Vec3 transformPoint(const Vec3& p) const noexcept {
        return position + rotation.rotate(scale * p);
    }

    [[nodiscard]] Vec3 transformVector(const Vec3& v) const noexcept {
        return rotation.rotate(scale * v);
    }

    /// Combines parent transform with a child local transform (this = parent, child = local).
    [[nodiscard]] Transform combine(const Transform& child) const noexcept {
        return Transform{
            position + rotation.rotate(scale * child.position),
            (rotation * child.rotation).normalized(),
            scale * child.scale,
        };
    }

    /// Combines parent transform with child transform (equivalent to combine).
    [[nodiscard]] Transform operator*(const Transform& child) const noexcept {
        return combine(child);
    }

    /// Linearly interpolates between two transforms.
    [[nodiscard]] static Transform lerp(const Transform& a, const Transform& b, float t) noexcept {
        return Transform{
            Vec3::lerp(a.position, b.position, t),
            Quat::slerp(a.rotation, b.rotation, t),
            Vec3::lerp(a.scale, b.scale, t),
        };
    }
};

inline Quat Quat::fromRotationMatrix(const Mat4& mat) noexcept {
    const float r00 = mat.m[0];
    const float r01 = mat.m[4];
    const float r02 = mat.m[8];
    const float r10 = mat.m[1];
    const float r11 = mat.m[5];
    const float r12 = mat.m[9];
    const float r20 = mat.m[2];
    const float r21 = mat.m[6];
    const float r22 = mat.m[10];

    const float tr = r00 + r11 + r22;
    float qx = 0.0F;
    float qy = 0.0F;
    float qz = 0.0F;
    float qw = 1.0F;

    if (tr > 0.0F) {
        const float s = std::sqrt(tr + 1.0F) * 2.0F;
        qw = 0.25F * s;
        qx = (r21 - r12) / s;
        qy = (r02 - r20) / s;
        qz = (r10 - r01) / s;
    } else if ((r00 > r11) && (r00 > r22)) {
        const float s = std::sqrt(1.0F + r00 - r11 - r22) * 2.0F;
        qw = (r21 - r12) / s;
        qx = 0.25F * s;
        qy = (r01 + r10) / s;
        qz = (r02 + r20) / s;
    } else if (r11 > r22) {
        const float s = std::sqrt(1.0F + r11 - r00 - r22) * 2.0F;
        qw = (r02 - r20) / s;
        qx = (r01 + r10) / s;
        qy = 0.25F * s;
        qz = (r12 + r21) / s;
    } else {
        const float s = std::sqrt(1.0F + r22 - r00 - r11) * 2.0F;
        qw = (r10 - r01) / s;
        qx = (r02 + r20) / s;
        qy = (r12 + r21) / s;
        qz = 0.25F * s;
    }

    return Quat{qx, qy, qz, qw}.normalized();
}

inline std::optional<Transform> Mat4::decompose() const noexcept {
    const Vec3 translation{m[12], m[13], m[14]};

    Vec3 col0{m[0], m[1], m[2]};
    Vec3 col1{m[4], m[5], m[6]};
    Vec3 col2{m[8], m[9], m[10]};

    float scaleX = col0.length();
    float scaleY = col1.length();
    float scaleZ = col2.length();

    if (scaleX < 1e-6F || scaleY < 1e-6F || scaleZ < 1e-6F) {
        return std::nullopt;
    }

    // Check determinant to preserve reflection/negative scale
    const Vec3 cross12 = Vec3::cross(col1, col2);
    if (Vec3::dot(col0, cross12) < 0.0F) {
        scaleX = -scaleX;
        col0 = -col0;
    }

    col0 = col0 / scaleX;
    col1 = col1 / scaleY;
    col2 = col2 / scaleZ;

    Mat4 rotMat{};
    rotMat.m[0] = col0.x;
    rotMat.m[1] = col0.y;
    rotMat.m[2] = col0.z;
    rotMat.m[4] = col1.x;
    rotMat.m[5] = col1.y;
    rotMat.m[6] = col1.z;
    rotMat.m[8] = col2.x;
    rotMat.m[9] = col2.y;
    rotMat.m[10] = col2.z;
    rotMat.m[15] = 1.0F;

    const Quat rotation = Quat::fromRotationMatrix(rotMat);
    return Transform{translation, rotation, Vec3{scaleX, scaleY, scaleZ}};
}

} // namespace eng
