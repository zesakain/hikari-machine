/// @file pool_allocator_test.cpp
/// @brief Comprehensive unit tests for eng::PoolAllocator.

#include <array>
#include <core/align.h>
#include <core/pool_allocator.h>
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

struct TestParticle {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
    uint32_t id{0};
};

struct TrackedObject {
    static inline int sDestructCount{0};
    int val{0};

    explicit TrackedObject(int v) : val(v) {}
    ~TrackedObject() { ++sDestructCount; }
};

int testNormalAllocationAndDeallocation() {
    alignas(64) std::array<std::byte, 512> memory{};
    constexpr size_t kBlockSize = 32;
    constexpr size_t kAlignment = 16;

    eng::PoolAllocator pool(memory, kBlockSize, kAlignment);
    TEST_CHECK(pool.capacity() > 0);
    TEST_CHECK(pool.allocatedCount() == 0);
    TEST_CHECK(pool.availableCount() == pool.capacity());
    TEST_CHECK(pool.isEmpty());
    TEST_CHECK(!pool.isFull());

    void* p1 = pool.allocate();
    TEST_CHECK(p1 != nullptr);
    TEST_CHECK(eng::isAligned(p1, kAlignment));
    TEST_CHECK(pool.allocatedCount() == 1);
    TEST_CHECK(!pool.isEmpty());

    void* p2 = pool.allocate();
    TEST_CHECK(p2 != nullptr);
    TEST_CHECK(p2 != p1);
    TEST_CHECK(eng::isAligned(p2, kAlignment));
    TEST_CHECK(pool.allocatedCount() == 2);

    // Deallocate p1
    TEST_CHECK(pool.deallocate(p1));
    TEST_CHECK(pool.allocatedCount() == 1);
    TEST_CHECK(pool.availableCount() == pool.capacity() - 1);

    // Deallocate p2
    TEST_CHECK(pool.deallocate(p2));
    TEST_CHECK(pool.allocatedCount() == 0);
    TEST_CHECK(pool.isEmpty());

    return 0;
}

int testExhaustionAndBlockReuse() {
    alignas(64) std::array<std::byte, 256> memory{};
    eng::PoolAllocator pool(memory, 32, 16);
    const size_t cap = pool.capacity();
    TEST_CHECK(cap > 0);

    std::array<void*, 32> blocks{};
    for (size_t i = 0; i < cap; ++i) {
        blocks[i] = pool.allocate();
        TEST_CHECK(blocks[i] != nullptr);
    }

    TEST_CHECK(pool.isFull());
    TEST_CHECK(pool.availableCount() == 0);
    TEST_CHECK(pool.allocate() == nullptr); // Exhausted

    // Free block 0
    void* freedBlock = blocks[0];
    TEST_CHECK(pool.deallocate(freedBlock));
    TEST_CHECK(!pool.isFull());
    TEST_CHECK(pool.availableCount() == 1);

    // Subsequent allocate must immediately reuse the freed block (LIFO head)
    void* reusedBlock = pool.allocate();
    TEST_CHECK(reusedBlock == freedBlock);
    TEST_CHECK(pool.isFull());

    // Free all
    for (size_t i = 0; i < cap; ++i) {
        TEST_CHECK(pool.deallocate(blocks[i]));
    }
    TEST_CHECK(pool.isEmpty());

    return 0;
}

int testAlignmentAndMinBlockSize() {
    alignas(128) std::array<std::byte, 1024> memory{};

    // Requesting block size 4 and alignment 4: must be clamped to min 16 bytes and 8 alignment
    eng::PoolAllocator pool(memory, 4, 4);
    TEST_CHECK(pool.blockSize() == 4);
    TEST_CHECK(pool.effectiveBlockSize() >= eng::PoolAllocator::kMinBlockSize);
    TEST_CHECK(pool.alignment() >= eng::PoolAllocator::kMinAlignment);

    void* p = pool.allocate();
    TEST_CHECK(p != nullptr);
    TEST_CHECK(eng::isAligned(p, pool.alignment()));
    TEST_CHECK(pool.deallocate(p));

    return 0;
}

int testSafetyAndRejection() {
    alignas(64) std::array<std::byte, 512> memory{};
    alignas(64) std::array<std::byte, 512> foreignMemory{};

    eng::PoolAllocator pool(memory, 32, 16);
    void* p1 = pool.allocate();
    TEST_CHECK(p1 != nullptr);
    const size_t allocCountBefore = pool.allocatedCount();

    // 1. Nullptr deallocation
    TEST_CHECK(!pool.deallocate(nullptr));
    TEST_CHECK(pool.allocatedCount() == allocCountBefore);

    // 2. Foreign pointer deallocation
    void* foreignPtr = foreignMemory.data();
    TEST_CHECK(!pool.deallocate(foreignPtr));
    TEST_CHECK(pool.allocatedCount() == allocCountBefore);

    // 3. Interior pointer deallocation (misaligned offset within a block)
    auto* interiorPtr = static_cast<void*>(static_cast<std::byte*>(p1) + 4);
    TEST_CHECK(!pool.deallocate(interiorPtr));
    TEST_CHECK(pool.allocatedCount() == allocCountBefore);

    // 4. Double deallocation rejection
    TEST_CHECK(pool.deallocate(p1));  // First dealloc: valid
    TEST_CHECK(!pool.deallocate(p1)); // Second dealloc: safely rejected
    TEST_CHECK(pool.allocatedCount() == 0);

    // 5. Free-list integrity after rejected operations
    // Ensure subsequent allocations and deallocations succeed cleanly without cycles or corruption
    void* freshP1 = pool.allocate();
    void* freshP2 = pool.allocate();
    TEST_CHECK(freshP1 != nullptr);
    TEST_CHECK(freshP2 != nullptr);
    TEST_CHECK(freshP1 != freshP2);
    TEST_CHECK(pool.deallocate(freshP1));
    TEST_CHECK(pool.deallocate(freshP2));
    TEST_CHECK(pool.isEmpty());

    return 0;
}

int testResetBehavior() {
    alignas(64) std::array<std::byte, 512> memory{};
    eng::PoolAllocator pool(memory, 32, 16);

    void* p1 = pool.allocate();
    void* p2 = pool.allocate();
    TEST_CHECK(p1 != nullptr);
    TEST_CHECK(p2 != nullptr);
    TEST_CHECK(pool.allocatedCount() == 2);

    pool.reset();
    TEST_CHECK(pool.allocatedCount() == 0);
    TEST_CHECK(pool.isEmpty());
    TEST_CHECK(pool.availableCount() == pool.capacity());

    // After reset, fresh allocations start from the beginning of the pool
    void* freshP1 = pool.allocate();
    TEST_CHECK(freshP1 == p1); // Reset rewound free list to first block

    return 0;
}

int testBoundaryAndOverflowConditions() {
    alignas(16) std::array<std::byte, 64> memory{};

    // Zero capacity / empty span
    std::span<std::byte> emptySpan{};
    eng::PoolAllocator poolEmpty(emptySpan, 16, 8);
    TEST_CHECK(poolEmpty.capacity() == 0);
    TEST_CHECK(poolEmpty.allocate() == nullptr);

    // Zero block size
    eng::PoolAllocator poolZeroBlock(memory, 0, 8);
    TEST_CHECK(poolZeroBlock.capacity() == 0);
    TEST_CHECK(poolZeroBlock.allocate() == nullptr);

    // Non-power-of-two alignment
    eng::PoolAllocator poolBadAlign(memory, 16, 7);
    TEST_CHECK(poolBadAlign.capacity() == 0);
    TEST_CHECK(poolBadAlign.allocate() == nullptr);

    // Null buffer with positive capacity
    eng::PoolAllocator poolNull(nullptr, 128, 16, 8);
    TEST_CHECK(poolNull.capacity() == 0);
    TEST_CHECK(poolNull.allocate() == nullptr);

    // Capacity smaller than single block
    eng::PoolAllocator poolTooSmall(memory.data(), 8, 32, 16);
    TEST_CHECK(poolTooSmall.capacity() == 0);
    TEST_CHECK(poolTooSmall.allocate() == nullptr);

    return 0;
}

int testMoveSemantics() {
    alignas(64) std::array<std::byte, 512> memory{};
    eng::PoolAllocator a(memory, 32, 16);

    void* p1 = a.allocate();
    TEST_CHECK(p1 != nullptr);
    const size_t cap = a.capacity();

    eng::PoolAllocator b(std::move(a));
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    TEST_CHECK(a.capacity() == 0);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    TEST_CHECK(a.allocatedCount() == 0);

    TEST_CHECK(b.capacity() == cap);
    TEST_CHECK(b.allocatedCount() == 1);

    // Deallocate p1 through b
    TEST_CHECK(b.deallocate(p1));
    TEST_CHECK(b.isEmpty());

    return 0;
}

int testTypedHelperAndLifetimeContract() {
    alignas(64) std::array<std::byte, 512> memory{};
    eng::PoolAllocator pool(memory, sizeof(TrackedObject), alignof(TrackedObject));

    TrackedObject::sDestructCount = 0;

    // Type-safe allocation
    auto* storage = pool.allocate<TrackedObject>();
    TEST_CHECK(storage != nullptr);
    TEST_CHECK(TrackedObject::sDestructCount == 0); // Pool does not run constructor

    // Construct in-place
    auto* obj = ::new (static_cast<void*>(storage)) TrackedObject(99);
    TEST_CHECK(obj->val == 99);

    // Caller manually destroys non-trivial object before deallocation
    std::destroy_at(obj);
    TEST_CHECK(TrackedObject::sDestructCount == 1);

    TEST_CHECK(pool.deallocate(storage));
    TEST_CHECK(pool.isEmpty());

    // Typed size mismatch protection: allocating type larger than block size returns nullptr
    struct BigStruct {
        std::byte data[256];
    };
    TEST_CHECK(pool.allocate<BigStruct>() == nullptr);

    return 0;
}

} // namespace

int main() {
    if (testNormalAllocationAndDeallocation() != 0) {
        return 1;
    }
    if (testExhaustionAndBlockReuse() != 0) {
        return 2;
    }
    if (testAlignmentAndMinBlockSize() != 0) {
        return 3;
    }
    if (testSafetyAndRejection() != 0) {
        return 4;
    }
    if (testResetBehavior() != 0) {
        return 5;
    }
    if (testBoundaryAndOverflowConditions() != 0) {
        return 6;
    }
    if (testMoveSemantics() != 0) {
        return 7;
    }
    if (testTypedHelperAndLifetimeContract() != 0) {
        return 8;
    }

    return 0;
}
