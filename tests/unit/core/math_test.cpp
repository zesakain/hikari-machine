/// @file math_test.cpp
/// @brief Unit tests for Vec3, Quat, Mat4, and Transform mathematical primitives.

#include "core/assert.h"
#include "core/math.h"

#include <cmath>
#include <iostream>
#include <numbers>

using namespace eng;

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << '\n'; \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

static int testVec3() {
    Vec3 a{1.0F, 2.0F, 3.0F};
    Vec3 b{4.0F, 5.0F, 6.0F};

    Vec3 add = a + b;
    TEST_CHECK(add.x == 5.0F);
    TEST_CHECK(add.y == 7.0F);
    TEST_CHECK(add.z == 9.0F);

    Vec3 sub = b - a;
    TEST_CHECK(sub.x == 3.0F);
    TEST_CHECK(sub.y == 3.0F);
    TEST_CHECK(sub.z == 3.0F);

    Vec3 mul = a * 2.0F;
    TEST_CHECK(mul.x == 2.0F);
    TEST_CHECK(mul.y == 4.0F);
    TEST_CHECK(mul.z == 6.0F);

    float d = Vec3::dot(a, b);
    TEST_CHECK(d == (4.0F + 10.0F + 18.0F)); // 32.0F

    Vec3 cross = Vec3::cross(Vec3::unitX(), Vec3::unitY());
    TEST_CHECK(cross.isNearlyEqual(Vec3::unitZ()));

    Vec3 norm = Vec3{0.0F, 3.0F, 4.0F}.normalized();
    TEST_CHECK(norm.isNearlyEqual(Vec3{0.0F, 0.6F, 0.8F}));

    Vec3 lerped = Vec3::lerp(Vec3::zero(), Vec3{10.0F, 20.0F, 30.0F}, 0.5F);
    TEST_CHECK(lerped.isNearlyEqual(Vec3{5.0F, 10.0F, 15.0F}));

    return 0;
}

static int testQuat() {
    Quat qId = Quat::identity();
    TEST_CHECK(qId.x == 0.0F);
    TEST_CHECK(qId.y == 0.0F);
    TEST_CHECK(qId.z == 0.0F);
    TEST_CHECK(qId.w == 1.0F);

    // 90 degrees around Z axis: (0, 0, 1) * pi/2
    const float halfPi = std::numbers::pi_v<float> * 0.5F;
    Quat qRotZ = Quat::fromAxisAngle(Vec3::unitZ(), halfPi);

    // Rotate unitX by 90 deg around Z -> should be unitY
    Vec3 rotated = qRotZ.rotate(Vec3::unitX());
    TEST_CHECK(rotated.isNearlyEqual(Vec3::unitY(), 1e-4F));

    // Inverse of rotation
    Quat qInv = qRotZ.inverse();
    Vec3 back = qInv.rotate(rotated);
    TEST_CHECK(back.isNearlyEqual(Vec3::unitX(), 1e-4F));

    // Slerp test
    Quat qHalf = Quat::slerp(Quat::identity(), qRotZ, 0.5F);
    Vec3 rotatedHalf = qHalf.rotate(Vec3::unitX());
    const float rootHalf = 0.70710678F;
    TEST_CHECK(rotatedHalf.isNearlyEqual(Vec3{rootHalf, rootHalf, 0.0F}, 1e-4F));

    return 0;
}

static int testMat4AndTransform() {
    Transform t{};
    t.position = Vec3{10.0F, 20.0F, 30.0F};
    t.rotation =
        Quat::fromAxisAngle(Vec3::unitY(), std::numbers::pi_v<float> * 0.5F); // 90 deg around Y
    t.scale = Vec3{2.0F, 2.0F, 2.0F};

    Mat4 m = t.toMatrix();

    // Point (1, 0, 0): scaled to (2, 0, 0), rotated 90 around Y to (0, 0, -2), translated to (10,
    // 20, 28)
    Vec3 p = t.transformPoint(Vec3{1.0F, 0.0F, 0.0F});
    Vec3 pMat = m.transformPoint(Vec3{1.0F, 0.0F, 0.0F});
    TEST_CHECK(p.isNearlyEqual(Vec3{10.0F, 20.0F, 28.0F}, 1e-4F));
    TEST_CHECK(pMat.isNearlyEqual(p, 1e-4F));

    // Fast affine inverse
    Transform tUniform{Vec3{5.0F, -3.0F, 2.0F}, Quat::fromAxisAngle(Vec3::unitZ(), 1.0F),
                       Vec3{1.0F, 1.0F, 1.0F}};
    Mat4 mUniform = tUniform.toMatrix();
    Mat4 invM = mUniform.inverseAffine();
    Mat4 ident = mUniform * invM;
    TEST_CHECK(
        ident.transformPoint(Vec3{1.0F, 2.0F, 3.0F}).isNearlyEqual(Vec3{1.0F, 2.0F, 3.0F}, 1e-4F));

    // Transform combination (parent-child)
    Transform parent{Vec3{10.0F, 0.0F, 0.0F}, Quat::identity(), Vec3{1.0F, 1.0F, 1.0F}};
    Transform child{Vec3{0.0F, 5.0F, 0.0F}, Quat::identity(), Vec3{1.0F, 1.0F, 1.0F}};
    Transform combined = parent.combine(child);
    TEST_CHECK(combined.position.isNearlyEqual(Vec3{10.0F, 5.0F, 0.0F}));

    return 0;
}

int main() {
    std::cout << "Running Math Tests...\n";

    if (testVec3() != 0) {
        return 1;
    }
    if (testQuat() != 0) {
        return 1;
    }
    if (testMat4AndTransform() != 0) {
        return 1;
    }

    std::cout << "All Math Tests Passed!\n";
    return 0;
}
