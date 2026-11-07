/// @file arena_allocator_test.cpp
/// @brief Comprehensive unit tests for eng::ArenaAllocator.

#include <array>
#include <core/align.h>
#include <core/arena_allocator.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

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

struct TrackedObject {
    static inline int sDestructCount{0};
    int value{0};

    explicit TrackedObject(int v) : value(v) {}
    ~TrackedObject() { ++sDestructCount; }
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
    TEST_CHECK(marker.offset == offsetAfterA);

    // Speculative allocations after checkpoint
    void* pB = arena.allocate(256, 16);
    TEST_CHECK(pB != nullptr);
    TEST_CHECK(arena.allocatedBytes() >= offsetAfterA + 256);

    // Roll back to checkpoint
    TEST_CHECK(arena.restore(marker));
    TEST_CHECK(arena.allocatedBytes() == offsetAfterA);
    TEST_CHECK(arena.remainingBytes() == 1024 - offsetAfterA);

    // Subsequent allocation must reuse memory after marker
    void* pC = arena.allocate(256, 16);
    TEST_CHECK(pC != nullptr);
    TEST_CHECK(pC == pB); // Memory location reused

    return 0;
}

int testMarkerRejectionAndSafety() {
    alignas(64) std::array<std::byte, 512> memA{};
    alignas(64) std::array<std::byte, 512> memB{};
    eng::ArenaAllocator arenaA(memA);
    eng::ArenaAllocator arenaB(memB);

    void* pA = arenaA.allocate(64, 8);
    TEST_CHECK(pA != nullptr);
    const auto markerA = arenaA.save();

    void* pB = arenaB.allocate(128, 8);
    TEST_CHECK(pB != nullptr);

    // A. Foreign marker rejection: marker from arenaA cannot corrupt arenaB
    TEST_CHECK(!arenaB.restore(markerA));
    TEST_CHECK(arenaB.allocatedBytes() == 128); // arenaB remains uncorrupted

    // B. Ahead / out-of-range marker rejection: fabricated marker ahead of current offset
    const eng::ArenaAllocator::Marker forwardMarker{
        .arenaId = &arenaA, .offset = 256, .resetId = markerA.resetId};
    TEST_CHECK(!arenaA.restore(forwardMarker));
    TEST_CHECK(arenaA.allocatedBytes() == 64); // arenaA remains uncorrupted

    // C. Abandoned forward marker rejection after rollback:
    // If m1 is at 64, m2 is at 192; rewinding to m1 invalidates future restoration of m2
    void* pA2 = arenaA.allocate(128, 8);
    TEST_CHECK(pA2 != nullptr);
    const auto markerA2 = arenaA.save();
    TEST_CHECK(arenaA.allocatedBytes() >= 192);

    TEST_CHECK(arenaA.restore(markerA));
    TEST_CHECK(arenaA.allocatedBytes() == 64);

    // Restoring markerA2 now must fail because markerA2.offset > current offset_ (192 > 64)
    TEST_CHECK(!arenaA.restore(markerA2));
    TEST_CHECK(arenaA.allocatedBytes() == 64); // State preserved safely

    // D. Stale marker rejection across reset():
    // Resetting arenaA increments reset generation, invalidating markerA
    arenaA.reset();
    TEST_CHECK(arenaA.isEmpty());
    TEST_CHECK(!arenaA.restore(markerA)); // Stale reset generation, safely rejected
    TEST_CHECK(arenaA.isEmpty());

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

    // Zero count typed allocation
    TEST_CHECK(arena.allocate<uint32_t>(0) == nullptr);
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
    const auto markerA = a.save();

    eng::ArenaAllocator b(std::move(a));
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    TEST_CHECK(a.capacity() == 0);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    TEST_CHECK(a.allocatedBytes() == 0);

    // Marker captured on 'a' before move is not accepted on 'b' (instance mismatch)
    TEST_CHECK(!b.restore(markerA));
    TEST_CHECK(b.capacity() == 256);
    TEST_CHECK(b.allocatedBytes() == 64);

    return 0;
}

int testObjectLifetimeContract() {
    alignas(64) std::array<std::byte, 512> memory{};
    eng::ArenaAllocator arena(memory);

    TrackedObject::sDestructCount = 0;

    // Allocate uninitialized storage for 2 TrackedObjects
    auto* storage = arena.allocate<TrackedObject>(2);
    TEST_CHECK(storage != nullptr);
    TEST_CHECK(TrackedObject::sDestructCount == 0); // No constructor/destructor run by allocator

    // Caller manually constructs objects via placement new
    auto* obj1 = ::new (static_cast<void*>(&storage[0])) TrackedObject(10);
    auto* obj2 = ::new (static_cast<void*>(&storage[1])) TrackedObject(20);
    TEST_CHECK(obj1->value == 10);
    TEST_CHECK(obj2->value == 20);

    // Caller is strictly responsible for manually destroying objects before reset() or restore()
    std::destroy_at(obj2);
    std::destroy_at(obj1);
    TEST_CHECK(TrackedObject::sDestructCount == 2);

    arena.reset();
    TEST_CHECK(arena.isEmpty());

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
    if (testMarkerRejectionAndSafety() != 0) {
        return 3;
    }
    if (testBulkReset() != 0) {
        return 4;
    }
    if (testExhaustionAndEdgeCases() != 0) {
        return 5;
    }
    if (testMoveSemantics() != 0) {
        return 6;
    }
    if (testObjectLifetimeContract() != 0) {
        return 7;
    }

    return 0;
}
