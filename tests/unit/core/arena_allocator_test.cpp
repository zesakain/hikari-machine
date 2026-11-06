/// @file arena_allocator_test.cpp
/// @brief Comprehensive unit tests for eng::ArenaAllocator.

#include <array>
#include <core/align.h>
#include <core/arena_allocator.h>
#include <cstddef>
#include <cstdint>

namespace {

#define TEST_CHECK(condition)                                                                      \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

struct TestStruct {
    uint32_t a{0};
    uint64_t b{0};
    float c{0.0F};
};

int testNormalAllocation() {
    alignas(64) std::array<std::byte, 1024> memory{};
    eng::ArenaAllocator arena(memory);

    TEST_CHECK(arena.capacity() == 1024);
    TEST_CHECK(arena.allocatedBytes() == 0);
    TEST_CHECK(arena.remainingBytes() == 1024);
    TEST_CHECK(arena.isEmpty());

    auto* items = arena.allocate<TestStruct>(4);
    TEST_CHECK(items != nullptr);
    TEST_CHECK(eng::isAligned(items, alignof(TestStruct)));
    TEST_CHECK(arena.allocatedBytes() >= sizeof(TestStruct) * 4);
    TEST_CHECK(!arena.isEmpty());

    return 0;
}

int testMarkerSaveAndRestore() {
    alignas(64) std::array<std::byte, 1024> memory{};
    eng::ArenaAllocator arena(memory);

    void* pA = arena.allocate(128, 16);
    TEST_CHECK(pA != nullptr);
    const size_t offsetAfterA = arena.allocatedBytes();

    // Checkpoint
    const eng::ArenaAllocator::Marker marker = arena.save();
    TEST_CHECK(marker == offsetAfterA);

    // Speculative allocations after checkpoint
    void* pB = arena.allocate(256, 16);
    TEST_CHECK(pB != nullptr);
    TEST_CHECK(arena.allocatedBytes() >= offsetAfterA + 256);

    // Roll back to checkpoint
    arena.restore(marker);
    TEST_CHECK(arena.allocatedBytes() == offsetAfterA);
    TEST_CHECK(arena.remainingBytes() == 1024 - offsetAfterA);

    // Subsequent allocation must reuse memory after marker
    void* pC = arena.allocate(256, 16);
    TEST_CHECK(pC != nullptr);
    TEST_CHECK(pC == pB); // Memory location reused

    return 0;
}

int testBulkReset() {
    alignas(64) std::array<std::byte, 512> memory{};
    eng::ArenaAllocator arena(memory);

    void* p1 = arena.allocate(128, 8);
    void* p2 = arena.allocate(128, 8);
    TEST_CHECK(p1 != nullptr);
    TEST_CHECK(p2 != nullptr);
    TEST_CHECK(arena.allocatedBytes() >= 256);

    arena.reset();
    TEST_CHECK(arena.allocatedBytes() == 0);
    TEST_CHECK(arena.remainingBytes() == 512);
    TEST_CHECK(arena.isEmpty());

    void* p3 = arena.allocate(128, 8);
    TEST_CHECK(p3 == p1); // Rewound to start of buffer

    return 0;
}

int testExhaustionAndEdgeCases() {
    alignas(16) std::array<std::byte, 64> memory{};
    eng::ArenaAllocator arena(memory);

    // Zero-size allocation
    TEST_CHECK(arena.allocate(0, 8) == nullptr);
    TEST_CHECK(arena.allocatedBytes() == 0);

    // Invalid alignment (non-power-of-two)
    TEST_CHECK(arena.allocate(16, 7) == nullptr);
    TEST_CHECK(arena.allocatedBytes() == 0);

    // Overflow protection
    TEST_CHECK(arena.allocate(SIZE_MAX - 32, 8) == nullptr);
    TEST_CHECK(arena.allocate<uint64_t>(SIZE_MAX / sizeof(uint64_t) + 1) == nullptr);

    // Exhaust capacity
    void* p1 = arena.allocate(48, 8);
    TEST_CHECK(p1 != nullptr);

    // Fails because remaining is < 32
    void* pTooBig = arena.allocate(32, 8);
    TEST_CHECK(pTooBig == nullptr);

    // Succeeds for fitting size
    void* pFits = arena.allocate(16, 1);
    TEST_CHECK(pFits != nullptr);
    TEST_CHECK(arena.remainingBytes() == 0);

    return 0;
}

int testMoveSemantics() {
    alignas(32) std::array<std::byte, 256> memory{};
    eng::ArenaAllocator a(memory);

    void* p1 = a.allocate(64, 16);
    TEST_CHECK(p1 != nullptr);

    eng::ArenaAllocator b(std::move(a));
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    TEST_CHECK(a.capacity() == 0);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    TEST_CHECK(a.allocatedBytes() == 0);

    return 0;
}

} // namespace

int main() {
    if (testNormalAllocation() != 0) {
        return 1;
    }
    if (testMarkerSaveAndRestore() != 0) {
        return 2;
    }
    if (testBulkReset() != 0) {
        return 3;
    }
    if (testExhaustionAndEdgeCases() != 0) {
        return 4;
    }
    if (testMoveSemantics() != 0) {
        return 5;
    }

    return 0;
}
