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

namespace eng {

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

} // namespace eng
