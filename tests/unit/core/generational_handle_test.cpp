/// @file generational_handle_test.cpp
/// @brief Comprehensive unit tests for eng::GenerationalHandle.

#include <core/generational_handle.h>
#include <cstdint>
#include <type_traits>

namespace {

#define TEST_CHECK(condition)                                                                      \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

int testTypeProperties() {
    // Exactly 64 bits (8 bytes)
    static_assert(sizeof(eng::GenerationalHandle) == 8);
    static_assert(sizeof(eng::GenerationalHandle::index) == 4);
    static_assert(sizeof(eng::GenerationalHandle::generation) == 4);

    // Standard layout and trivially copyable
    static_assert(std::is_trivially_copyable_v<eng::GenerationalHandle>);
    static_assert(std::is_standard_layout_v<eng::GenerationalHandle>);

    return 0;
}

int testDefaultAndInvalidSentinels() {
    // Default constructor yields invalid handle
    const eng::GenerationalHandle hDefault;
    TEST_CHECK(!hDefault.isValid());
    TEST_CHECK(!static_cast<bool>(hDefault));
    TEST_CHECK(hDefault.index == eng::GenerationalHandle::kInvalidIndex);
    TEST_CHECK(hDefault.generation == eng::GenerationalHandle::kInvalidGeneration);

    // Explicit invalid() factory
    const eng::GenerationalHandle hInvalid = eng::GenerationalHandle::invalid();
    TEST_CHECK(!hInvalid.isValid());
    TEST_CHECK(!hInvalid);
    TEST_CHECK(hDefault == hInvalid);

    // Zero generation makes handle invalid even if index is valid
    const eng::GenerationalHandle hZeroGen(10, 0);
    TEST_CHECK(!hZeroGen.isValid());
    TEST_CHECK(!hZeroGen);

    // Invalid index makes handle invalid even if generation is non-zero
    const eng::GenerationalHandle hBadIndex(eng::GenerationalHandle::kInvalidIndex, 5);
    TEST_CHECK(!hBadIndex.isValid());
    TEST_CHECK(!hBadIndex);

    return 0;
}

int testValidHandlePreservation() {
    constexpr uint32_t kIndex = 42;
    constexpr uint32_t kGen = 7;
    const eng::GenerationalHandle h(kIndex, kGen);

    TEST_CHECK(h.isValid());
    TEST_CHECK(static_cast<bool>(h));
    TEST_CHECK(h.index == kIndex);
    TEST_CHECK(h.generation == kGen);

    // Copy semantics
    eng::GenerationalHandle copy = h;
    TEST_CHECK(copy == h);
    TEST_CHECK(copy.index == kIndex);
    TEST_CHECK(copy.generation == kGen);

    return 0;
}

int testEqualityAndInequality() {
    const eng::GenerationalHandle h1(10, 1);
    const eng::GenerationalHandle h2(10, 1);
    const eng::GenerationalHandle hDifferentIndex(11, 1);
    const eng::GenerationalHandle hDifferentGen(10, 2);

    TEST_CHECK(h1 == h2);
    TEST_CHECK(!(h1 != h2));

    TEST_CHECK(h1 != hDifferentIndex);
    TEST_CHECK(!(h1 == hDifferentIndex));

    TEST_CHECK(h1 != hDifferentGen);
    TEST_CHECK(!(h1 == hDifferentGen));

    return 0;
}

int testBitPacking() {
    constexpr uint32_t kIndex = 0x12345678;
    constexpr uint32_t kGen = 0x9ABCDEF0;
    const eng::GenerationalHandle original(kIndex, kGen);

    const uint64_t bits = original.toBits();
    const eng::GenerationalHandle restored = eng::GenerationalHandle::fromBits(bits);

    TEST_CHECK(restored == original);
    TEST_CHECK(restored.index == kIndex);
    TEST_CHECK(restored.generation == kGen);

    // Invalid handle packing
    const uint64_t invalidBits = eng::GenerationalHandle::invalid().toBits();
    const eng::GenerationalHandle restoredInvalid = eng::GenerationalHandle::fromBits(invalidBits);
    TEST_CHECK(!restoredInvalid.isValid());
    TEST_CHECK(restoredInvalid == eng::GenerationalHandle::invalid());

    return 0;
}

} // namespace

int main() {
    if (testTypeProperties() != 0) {
        return 1;
    }
    if (testDefaultAndInvalidSentinels() != 0) {
        return 2;
    }
    if (testValidHandlePreservation() != 0) {
        return 3;
    }
    if (testEqualityAndInequality() != 0) {
        return 4;
    }
    if (testBitPacking() != 0) {
        return 5;
    }

    return 0;
}
