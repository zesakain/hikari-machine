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

static int testVec2Vec4AndAABB() {
    Vec2 v2a{1.0F, 2.0F};
    Vec2 v2b{3.0F, 4.0F};
    Vec2 v2Sum = v2a + v2b;
    TEST_CHECK(v2Sum.x == 4.0F && v2Sum.y == 6.0F);
    TEST_CHECK(v2a.lengthSq() == 5.0F);

    Vec4 v4a{1.0F, 2.0F, 3.0F, 4.0F};
    Vec4 v4b{0.5F, 1.0F, 1.5F, 2.0F};
    Vec4 v4Sub = v4a - v4b;
    TEST_CHECK(v4Sub.isNearlyEqual(Vec4{0.5F, 1.0F, 1.5F, 2.0F}));

    AABB aabb(Vec3{-1.0F, -2.0F, -3.0F}, Vec3{1.0F, 2.0F, 3.0F});
    TEST_CHECK(aabb.isValid());
    TEST_CHECK(aabb.getCenter().isNearlyEqual(Vec3::zero()));
    TEST_CHECK(aabb.getSize().isNearlyEqual(Vec3{2.0F, 4.0F, 6.0F}));

    aabb.expand(Vec3{5.0F, 0.0F, 0.0F});
    TEST_CHECK(aabb.maxBound.x == 5.0F);

    Ray ray(Vec3{0.0F, 0.0F, 10.0F}, Vec3{0.0F, 0.0F, -1.0F});
    auto tHit = ray.intersectAABB(aabb);
    TEST_CHECK(tHit.has_value());
    TEST_CHECK(std::abs(*tHit - 7.0F) < 1e-4F); // Hits z = 3 at dist 7

    return 0;
}

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

static int testPerspectiveLookAtInverseAndRay() {
    // 1. Perspective Matrix projection
    const float fovY = std::numbers::pi_v<float> / 3.0F; // 60 degrees
    const float aspect = 16.0F / 9.0F;
    const float zNear = 0.1F;
    const float zFar = 100.0F;
    Mat4 proj = Mat4::perspective(fovY, aspect, zNear, zFar);

    // 2. View Matrix lookAt
    Vec3 eye{0.0F, 5.0F, 10.0F};
    Vec3 target{0.0F, 0.0F, 0.0F};
    Mat4 view = Mat4::lookAt(eye, target, Vec3::unitY());

    Mat4 vp = proj * view;

    // Center target (0,0,0) projected should have NDC x = 0, y close to center
    float wTarget = 0.0F;
    Vec3 pNdc = vp.transformPointWithW(target, wTarget);
    TEST_CHECK(wTarget > 0.0F);
    float ndcX = pNdc.x / wTarget;
    float ndcY = pNdc.y / wTarget;
    TEST_CHECK(std::abs(ndcX) < 1e-4F);
    TEST_CHECK(std::abs(ndcY) < 0.8F);

    // 3. Matrix Inverse
    auto invOpt = vp.inverse();
    TEST_CHECK(invOpt.has_value());
    Mat4 invVp = *invOpt;
    Mat4 ident = vp * invVp;
    for (size_t r = 0; r < 4; ++r) {
        for (size_t c = 0; c < 4; ++c) {
            float expected = (r == c) ? 1.0F : 0.0F;
            TEST_CHECK(std::abs(ident.at(r, c) - expected) < 1e-3F);
        }
    }

    // Unproject NDC center back to 3D line
    float wNear = 0.0F;
    float wFar = 0.0F;
    Vec3 ptNearWorld = invVp.transformPointWithW(Vec3{0.0F, 0.0F, 0.0F}, wNear);
    ptNearWorld = ptNearWorld / wNear;
    Vec3 ptFarWorld = invVp.transformPointWithW(Vec3{0.0F, 0.0F, 1.0F}, wFar);
    ptFarWorld = ptFarWorld / wFar;
    Vec3 rayDir = (ptFarWorld - ptNearWorld).normalized();
    Vec3 expectedDir = (target - eye).normalized();
    TEST_CHECK(rayDir.isNearlyEqual(expectedDir, 1e-3F));

    // 4. Ray Intersection Tests
    Ray ray(eye, (target - eye).normalized());

    // Plane intersection (Y = 0)
    auto tPlane = ray.intersectPlane(Vec3::zero(), Vec3::unitY());
    TEST_CHECK(tPlane.has_value());
    Vec3 hitPlane = ray.getPoint(*tPlane);
    TEST_CHECK(hitPlane.isNearlyEqual(target, 1e-3F));

    // Sphere intersection at target with radius 1.5
    auto tSphere = ray.intersectSphere(target, 1.5F);
    TEST_CHECK(tSphere.has_value());
    TEST_CHECK(*tSphere < (target - eye).length());

    // AABB intersection [-1, -1, -1] to [1, 1, 1]
    auto tAABB = ray.intersectAABB(Vec3{-1.0F, -1.0F, -1.0F}, Vec3{1.0F, 1.0F, 1.0F});
    TEST_CHECK(tAABB.has_value());
    TEST_CHECK(*tAABB > 0.0F);

    // Miss AABB test
    auto tMiss = ray.intersectAABB(Vec3{10.0F, 10.0F, 10.0F}, Vec3{12.0F, 12.0F, 12.0F});
    TEST_CHECK(!tMiss.has_value());

    return 0;
}

int testFrustumAndPlane() {
    // Mat4 equality & isNearlyEqual
    Mat4 m1 = Mat4::identity();
    Mat4 m2 = Mat4::identity();
    TEST_CHECK(m1 == m2);
    TEST_CHECK(m1.isNearlyEqual(m2));
    m2.m[0] = 2.0F;
    TEST_CHECK(m1 != m2);
    TEST_CHECK(!m1.isNearlyEqual(m2));

    // Plane distance & normalization
    Plane p(Vec3(0.0F, 2.0F, 0.0F), -4.0F); // y = 2
    Plane normP = p.normalized();
    TEST_CHECK(std::abs(normP.normal.y - 1.0F) < 1e-5F);
    TEST_CHECK(std::abs(normP.distance - (-2.0F)) < 1e-5F);
    TEST_CHECK(std::abs(normP.signedDistance(Vec3(0.0F, 5.0F, 0.0F)) - 3.0F) < 1e-5F);
    TEST_CHECK(std::abs(normP.signedDistance(Vec3(0.0F, -1.0F, 0.0F)) - (-3.0F)) < 1e-5F);

    // Camera view-projection frustum
    constexpr float kPi = 3.14159265358979323846F;
    Mat4 view = Mat4::lookAt(Vec3(0.0F, 0.0F, 0.0F), Vec3(0.0F, 0.0F, -1.0F), Vec3::unitY());
    Mat4 proj = Mat4::perspective(60.0F * (kPi / 180.0F), 1.0F, 0.1F, 100.0F);
    Mat4 vp = proj * view;

    Frustum frustum = Frustum::fromViewProjection(vp);

    // Box right in front of camera
    AABB boxFront(Vec3(-1.0F, -1.0F, -10.0F), Vec3(1.0F, 1.0F, -8.0F));
    TEST_CHECK(frustum.intersectsAABB(boxFront));

    // Box behind camera
    AABB boxBehind(Vec3(-1.0F, -1.0F, 5.0F), Vec3(1.0F, 1.0F, 10.0F));
    TEST_CHECK(!frustum.intersectsAABB(boxBehind));

    // Box far off to the right
    AABB boxRight(Vec3(100.0F, -1.0F, -10.0F), Vec3(110.0F, 1.0F, -8.0F));
    TEST_CHECK(!frustum.intersectsAABB(boxRight));

    // Box beyond far plane (z < -100)
    AABB boxBeyondFar(Vec3(-1.0F, -1.0F, -120.0F), Vec3(1.0F, 1.0F, -105.0F));
    TEST_CHECK(!frustum.intersectsAABB(boxBeyondFar));

    // Huge box enclosing entire frustum
    AABB boxEnclosing(Vec3(-500.0F, -500.0F, -500.0F), Vec3(500.0F, 500.0F, 500.0F));
    TEST_CHECK(frustum.intersectsAABB(boxEnclosing));

    // Invalid AABB should conservatively return true
    AABB invalidBox;
    TEST_CHECK(frustum.intersectsAABB(invalidBox));

    // Sphere tests
    TEST_CHECK(frustum.intersectsSphere(Vec3(0.0F, 0.0F, -10.0F), 2.0F));
    TEST_CHECK(!frustum.intersectsSphere(Vec3(0.0F, 0.0F, 10.0F), 2.0F));

    return 0;
}

int main() {
    std::cout << "Running Math Tests...\n";

    if (testVec2Vec4AndAABB() != 0) {
        return 1;
    }
    if (testVec3() != 0) {
        return 1;
    }
    if (testQuat() != 0) {
        return 1;
    }
    if (testMat4AndTransform() != 0) {
        return 1;
    }
    if (testPerspectiveLookAtInverseAndRay() != 0) {
        return 1;
    }
    if (testFrustumAndPlane() != 0) {
        return 1;
    }

    std::cout << "All Math Tests Passed!\n";
    return 0;
}
