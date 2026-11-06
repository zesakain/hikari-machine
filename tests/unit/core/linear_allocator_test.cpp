/// @file linear_allocator_test.cpp
/// @brief Comprehensive unit tests for eng::LinearAllocator.

#include <array>
#include <core/align.h>
#include <core/linear_allocator.h>
#include <cstddef>
#include <cstdint>

namespace {

#define TEST_CHECK(condition)                                                                      \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

int testNormalAllocation() {
    alignas(64) std::array<std::byte, 1024> memory{};
    eng::LinearAllocator allocator(memory);

    TEST_CHECK(allocator.capacity() == 1024);
    TEST_CHECK(allocator.allocatedBytes() == 0);
    TEST_CHECK(allocator.remainingBytes() == 1024);
    TEST_CHECK(allocator.isEmpty());

    void* p1 = allocator.allocate(128, 8);
    TEST_CHECK(p1 != nullptr);
    TEST_CHECK(allocator.allocatedBytes() == 128);
    TEST_CHECK(allocator.remainingBytes() == 1024 - 128);
    TEST_CHECK(!allocator.isEmpty());
    TEST_CHECK(eng::isAligned(p1, 8));

    void* p2 = allocator.allocate(64, 16);
    TEST_CHECK(p2 != nullptr);
    TEST_CHECK(eng::isAligned(p2, 16));
    TEST_CHECK(p2 >= static_cast<void*>(static_cast<std::byte*>(p1) + 128));

    return 0;
}

int testAlignmentMultiples() {
    alignas(128) std::array<std::byte, 2048> memory{};
    eng::LinearAllocator allocator(memory);

    constexpr size_t kAlignments[] = {1, 2, 4, 8, 16, 32, 64, 128};
    for (size_t align : kAlignments) {
        void* ptr = allocator.allocate(13, align); // 13 bytes to induce odd padding
        TEST_CHECK(ptr != nullptr);
        TEST_CHECK(eng::isAligned(ptr, align));
        TEST_CHECK(ptr >= allocator.data());
        TEST_CHECK(static_cast<std::byte*>(ptr) + 13 <= allocator.data() + allocator.capacity());
    }

    return 0;
}

int testCapacityExhaustion() {
    alignas(16) std::array<std::byte, 128> memory{};
    eng::LinearAllocator allocator(memory);

    void* p1 = allocator.allocate(100, 8);
    TEST_CHECK(p1 != nullptr);

    // Requesting more than remaining capacity must fail and return nullptr
    void* p2 = allocator.allocate(50, 8);
    TEST_CHECK(p2 == nullptr);

    // Allocated bytes must remain unchanged upon failed allocation
    TEST_CHECK(allocator.allocatedBytes() == 100);

    // Fitting remaining allocation must succeed
    void* p3 = allocator.allocate(28, 1);
    TEST_CHECK(p3 != nullptr);
    TEST_CHECK(allocator.allocatedBytes() == 128);
    TEST_CHECK(allocator.remainingBytes() == 0);

    // Further allocations must fail
    void* p4 = allocator.allocate(1, 1);
    TEST_CHECK(p4 == nullptr);

    return 0;
}

int testZeroSizeAndInvalidAlignment() {
    alignas(16) std::array<std::byte, 256> memory{};
    eng::LinearAllocator allocator(memory);

    // Zero size returns nullptr
    void* pZero = allocator.allocate(0, 8);
    TEST_CHECK(pZero == nullptr);
    TEST_CHECK(allocator.allocatedBytes() == 0);

    // Zero alignment returns nullptr
    void* pZeroAlign = allocator.allocate(32, 0);
    TEST_CHECK(pZeroAlign == nullptr);
    TEST_CHECK(allocator.allocatedBytes() == 0);

    // Non-power-of-two alignment returns nullptr
    void* pBadAlign = allocator.allocate(32, 3);
    TEST_CHECK(pBadAlign == nullptr);
    TEST_CHECK(allocator.allocatedBytes() == 0);

    return 0;
}

int testOverflowProtection() {
    alignas(16) std::array<std::byte, 256> memory{};
    eng::LinearAllocator allocator(memory);

    // Excessive size close to SIZE_MAX must return nullptr safely without crash
    void* pHuge = allocator.allocate(SIZE_MAX - 16, 8);
    TEST_CHECK(pHuge == nullptr);
    TEST_CHECK(allocator.allocatedBytes() == 0);

    // Type-safe allocation with count overflow
    auto* pArray = allocator.allocate<uint64_t>(SIZE_MAX / sizeof(uint64_t) + 1);
    TEST_CHECK(pArray == nullptr);
    TEST_CHECK(allocator.allocatedBytes() == 0);

    return 0;
}

int testReset() {
    alignas(32) std::array<std::byte, 512> memory{};
    eng::LinearAllocator allocator(memory);

    void* p1 = allocator.allocate(200, 16);
    TEST_CHECK(p1 != nullptr);
    TEST_CHECK(allocator.allocatedBytes() >= 200);

    allocator.reset();
    TEST_CHECK(allocator.allocatedBytes() == 0);
    TEST_CHECK(allocator.remainingBytes() == 512);
    TEST_CHECK(allocator.isEmpty());

    // After reset, fresh allocation from offset 0 must succeed
    void* p2 = allocator.allocate(200, 16);
    TEST_CHECK(p2 != nullptr);
    TEST_CHECK(p2 == p1); // Offset rewound to start of buffer

    return 0;
}

int testMoveSemantics() {
    alignas(16) std::array<std::byte, 256> memory{};
    eng::LinearAllocator a(memory);

    void* p1 = a.allocate(64, 8);
    TEST_CHECK(p1 != nullptr);
    TEST_CHECK(a.allocatedBytes() == 64);

    eng::LinearAllocator b(std::move(a));
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    TEST_CHECK(a.capacity() == 0);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    TEST_CHECK(a.allocatedBytes() == 0);
    TEST_CHECK(b.capacity() == 256);
    TEST_CHECK(b.allocatedBytes() == 64);

    void* p2 = b.allocate(64, 8);
    TEST_CHECK(p2 != nullptr);
    TEST_CHECK(b.allocatedBytes() == 128);

    return 0;
}

} // namespace

int main() {
    if (testNormalAllocation() != 0) {
        return 1;
    }
    if (testAlignmentMultiples() != 0) {
        return 2;
    }
    if (testCapacityExhaustion() != 0) {
        return 3;
    }
    if (testZeroSizeAndInvalidAlignment() != 0) {
        return 4;
    }
    if (testOverflowProtection() != 0) {
        return 5;
    }
    if (testReset() != 0) {
        return 6;
    }
    if (testMoveSemantics() != 0) {
        return 7;
    }

    return 0;
}
