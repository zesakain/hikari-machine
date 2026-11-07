/// @file slot_map_test.cpp
/// @brief Comprehensive unit tests for eng::SlotMap<T>.

#include <array>
#include <core/generational_handle.h>
#include <core/slot_map.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace {

#define TEST_CHECK(condition)                                                                      \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

struct Point {
    int x{0};
    int y{0};
};

struct TrackedObject {
    static inline int sConstructed{0};
    static inline int sDestructed{0};

    int value{0};

    static void reset() noexcept {
        sConstructed = 0;
        sDestructed = 0;
    }

    static int aliveCount() noexcept { return sConstructed - sDestructed; }

    explicit TrackedObject(int v) noexcept : value(v) { ++sConstructed; }

    ~TrackedObject() noexcept { ++sDestructed; }

    TrackedObject(const TrackedObject&) = delete;
    TrackedObject& operator=(const TrackedObject&) = delete;
    TrackedObject(TrackedObject&&) = delete;
    TrackedObject& operator=(TrackedObject&&) = delete;
};

struct alignas(64) OverAligned64 {
    uint64_t data[8]{};
    uint64_t tag{0};
};

int testInsertLookupErase() {
    constexpr size_t kCapacity = 8;
    alignas(eng::SlotMap<Point>::requiredAlignment())
        std::array<std::byte, eng::SlotMap<Point>::requiredBytes(kCapacity)>
            buffer{};

    eng::SlotMap<Point> map(buffer, kCapacity);
    TEST_CHECK(map.capacity() == kCapacity);
    TEST_CHECK(map.size() == 0);
    TEST_CHECK(map.available() == kCapacity);
    TEST_CHECK(map.isEmpty());
    TEST_CHECK(!map.isFull());

    // Insert
    const eng::GenerationalHandle h1 = map.emplace(10, 20);
    const eng::GenerationalHandle h2 = map.emplace(30, 40);
    const eng::GenerationalHandle h3 = map.emplace(50, 60);

    TEST_CHECK(h1.isValid());
    TEST_CHECK(h2.isValid());
    TEST_CHECK(h3.isValid());
    TEST_CHECK(map.size() == 3);
    TEST_CHECK(map.available() == kCapacity - 3);

    // Contains & Lookup
    TEST_CHECK(map.contains(h1));
    TEST_CHECK(map.contains(h2));
    TEST_CHECK(map.contains(h3));

    Point* p1 = map.get(h1);
    Point* p2 = map.get(h2);
    Point* p3 = map.get(h3);

    TEST_CHECK(p1 != nullptr);
    TEST_CHECK(p1->x == 10);
    TEST_CHECK(p1->y == 20);

    TEST_CHECK(p2 != nullptr);
    TEST_CHECK(p2->x == 30);
    TEST_CHECK(p2->y == 40);

    TEST_CHECK(p3 != nullptr);
    TEST_CHECK(p3->x == 50);
    TEST_CHECK(p3->y == 60);

    // Mutation
    p1->x = 100;
    TEST_CHECK(map.get(h1)->x == 100);

    // Const lookup
    const auto& constMap = map;
    const Point* cp2 = constMap.get(h2);
    TEST_CHECK(cp2 != nullptr);
    TEST_CHECK(cp2->x == 30);
    TEST_CHECK(cp2->y == 40);

    // Erase middle element
    TEST_CHECK(map.erase(h2));
    TEST_CHECK(!map.contains(h2));
    TEST_CHECK(map.get(h2) == nullptr);
    TEST_CHECK(map.size() == 2);
    TEST_CHECK(map.available() == kCapacity - 2);

    // Remaining elements intact
    TEST_CHECK(map.contains(h1));
    TEST_CHECK(map.get(h1)->x == 100);
    TEST_CHECK(map.contains(h3));
    TEST_CHECK(map.get(h3)->x == 50);

    return 0;
}

int testSlotReuseWithDifferentGeneration() {
    constexpr size_t kCapacity = 2;
    alignas(eng::SlotMap<int>::requiredAlignment())
        std::array<std::byte, eng::SlotMap<int>::requiredBytes(kCapacity)>
            buffer{};

    eng::SlotMap<int> map(buffer, kCapacity);

    // Emplace item A in slot 0
    const eng::GenerationalHandle hA = map.emplace(111);
    TEST_CHECK(hA.isValid());
    TEST_CHECK(hA.index == 0);
    TEST_CHECK(hA.generation == 1);

    // Erase item A
    TEST_CHECK(map.erase(hA));
    TEST_CHECK(!map.contains(hA));
    TEST_CHECK(map.get(hA) == nullptr);

    // Re-emplace should reuse slot 0 with incremented generation 2
    const eng::GenerationalHandle hB = map.emplace(222);
    TEST_CHECK(hB.isValid());
    TEST_CHECK(hB.index == 0);
    TEST_CHECK(hB.generation == 2);

    // Stale handle check: hA and hB have identical index, different generation
    TEST_CHECK(!map.contains(hA));
    TEST_CHECK(map.get(hA) == nullptr);
    TEST_CHECK(map.contains(hB));
    TEST_CHECK(*map.get(hB) == 222);

    // Cycle once more
    TEST_CHECK(map.erase(hB));
    const eng::GenerationalHandle hC = map.emplace(333);
    TEST_CHECK(hC.index == 0);
    TEST_CHECK(hC.generation == 3);
    TEST_CHECK(!map.contains(hA));
    TEST_CHECK(!map.contains(hB));
    TEST_CHECK(map.contains(hC));
    TEST_CHECK(*map.get(hC) == 333);

    return 0;
}

int testStaleAndForeignHandles() {
    constexpr size_t kCapacity = 4;
    alignas(eng::SlotMap<int>::requiredAlignment())
        std::array<std::byte, eng::SlotMap<int>::requiredBytes(kCapacity)>
            buf1{};
    alignas(eng::SlotMap<int>::requiredAlignment())
        std::array<std::byte, eng::SlotMap<int>::requiredBytes(kCapacity)>
            buf2{};

    eng::SlotMap<int> map1(buf1, kCapacity);
    eng::SlotMap<int> map2(buf2, kCapacity);

    const eng::GenerationalHandle h1 = map1.emplace(42);
    const eng::GenerationalHandle h2 = map2.emplace(84);

    // Default & Invalid sentinels
    const eng::GenerationalHandle hInvalid;
    TEST_CHECK(!map1.contains(hInvalid));
    TEST_CHECK(map1.get(hInvalid) == nullptr);
    TEST_CHECK(!map1.erase(hInvalid));

    const eng::GenerationalHandle hExplicitInvalid = eng::GenerationalHandle::invalid();
    TEST_CHECK(!map1.contains(hExplicitInvalid));
    TEST_CHECK(map1.get(hExplicitInvalid) == nullptr);
    TEST_CHECK(!map1.erase(hExplicitInvalid));

    // Out-of-bounds / foreign index
    const eng::GenerationalHandle hOutOfBounds(9999, 1);
    TEST_CHECK(!map1.contains(hOutOfBounds));
    TEST_CHECK(map1.get(hOutOfBounds) == nullptr);
    TEST_CHECK(!map1.erase(hOutOfBounds));

    // Foreign handle from another map pointing to empty or mismatched slot
    // map2 index 0 was minted with generation 1; map1 index 0 also has generation 1.
    // However, if we erase h1 in map1, map2's handle applied to map1 is stale:
    TEST_CHECK(map1.erase(h1));
    TEST_CHECK(!map1.contains(h2));
    TEST_CHECK(map1.get(h2) == nullptr);
    TEST_CHECK(!map1.erase(h2));

    return 0;
}

int testCapacityFull() {
    constexpr size_t kCapacity = 3;
    alignas(eng::SlotMap<int>::requiredAlignment())
        std::array<std::byte, eng::SlotMap<int>::requiredBytes(kCapacity)>
            buffer{};

    eng::SlotMap<int> map(buffer, kCapacity);

    const eng::GenerationalHandle h0 = map.emplace(1);
    const eng::GenerationalHandle h1 = map.emplace(2);
    const eng::GenerationalHandle h2 = map.emplace(3);

    TEST_CHECK(h0.isValid() && h1.isValid() && h2.isValid());
    TEST_CHECK(map.isFull());
    TEST_CHECK(map.size() == 3);
    TEST_CHECK(map.available() == 0);

    // Overflow attempt
    const eng::GenerationalHandle hOverflow = map.emplace(999);
    TEST_CHECK(!hOverflow.isValid());
    TEST_CHECK(hOverflow == eng::GenerationalHandle::invalid());
    TEST_CHECK(map.size() == 3);
    TEST_CHECK(map.isFull());

    // Existing entries untouched
    TEST_CHECK(*map.get(h0) == 1);
    TEST_CHECK(*map.get(h1) == 2);
    TEST_CHECK(*map.get(h2) == 3);

    // Erase one and verify insertion is restored
    TEST_CHECK(map.erase(h1));
    TEST_CHECK(!map.isFull());
    TEST_CHECK(map.size() == 2);
    TEST_CHECK(map.available() == 1);

    const eng::GenerationalHandle hRestored = map.emplace(42);
    TEST_CHECK(hRestored.isValid());
    TEST_CHECK(map.isFull());
    TEST_CHECK(map.size() == 3);
    TEST_CHECK(*map.get(hRestored) == 42);

    return 0;
}

int testClearAndReconstruction() {
    constexpr size_t kCapacity = 5;
    alignas(eng::SlotMap<int>::requiredAlignment())
        std::array<std::byte, eng::SlotMap<int>::requiredBytes(kCapacity)>
            buffer{};

    eng::SlotMap<int> map(buffer, kCapacity);

    const eng::GenerationalHandle h0 = map.emplace(10);
    const eng::GenerationalHandle h1 = map.emplace(20);
    const eng::GenerationalHandle h2 = map.emplace(30);

    TEST_CHECK(map.size() == 3);
    map.clear();

    TEST_CHECK(map.isEmpty());
    TEST_CHECK(map.size() == 0);
    TEST_CHECK(map.available() == kCapacity);

    // All prior handles invalidated
    TEST_CHECK(!map.contains(h0));
    TEST_CHECK(!map.contains(h1));
    TEST_CHECK(!map.contains(h2));
    TEST_CHECK(map.get(h0) == nullptr);
    TEST_CHECK(map.get(h1) == nullptr);
    TEST_CHECK(map.get(h2) == nullptr);

    // Subsequent clear() on empty map is a no-op
    map.clear();
    TEST_CHECK(map.isEmpty());

    // Re-allocation succeeds with updated generations
    const eng::GenerationalHandle hNew0 = map.emplace(100);
    TEST_CHECK(hNew0.isValid());
    TEST_CHECK(hNew0.index == 0);
    TEST_CHECK(hNew0.generation == 2);
    TEST_CHECK(*map.get(hNew0) == 100);

    return 0;
}

int testNonTrivialObjectLifecycle() {
    TrackedObject::reset();

    constexpr size_t kCapacity = 4;
    alignas(eng::SlotMap<TrackedObject>::requiredAlignment())
        std::array<std::byte, eng::SlotMap<TrackedObject>::requiredBytes(kCapacity)>
            buffer{};

    {
        eng::SlotMap<TrackedObject> map(buffer, kCapacity);
        TEST_CHECK(TrackedObject::aliveCount() == 0);

        const eng::GenerationalHandle h0 = map.emplace(10);
        const eng::GenerationalHandle h1 = map.emplace(20);
        const eng::GenerationalHandle h2 = map.emplace(30);

        TEST_CHECK(h0.isValid() && h1.isValid() && h2.isValid());
        TEST_CHECK(TrackedObject::sConstructed == 3);
        TEST_CHECK(TrackedObject::sDestructed == 0);
        TEST_CHECK(TrackedObject::aliveCount() == 3);

        // Erase h1
        TEST_CHECK(map.erase(h1));
        TEST_CHECK(TrackedObject::sDestructed == 1);
        TEST_CHECK(TrackedObject::aliveCount() == 2);

        // Clear remaining h0, h2
        map.clear();
        TEST_CHECK(TrackedObject::sDestructed == 3);
        TEST_CHECK(TrackedObject::aliveCount() == 0);

        // Re-emplace
        const auto h3 = map.emplace(40);
        const auto h4 = map.emplace(50);
        TEST_CHECK(h3.isValid() && h4.isValid());
        TEST_CHECK(TrackedObject::sConstructed == 5);
        TEST_CHECK(TrackedObject::aliveCount() == 2);

        // When map goes out of scope here, destructor must destroy remaining 2 objects
    }

    TEST_CHECK(TrackedObject::sConstructed == 5);
    TEST_CHECK(TrackedObject::sDestructed == 5);
    TEST_CHECK(TrackedObject::aliveCount() == 0);

    return 0;
}

int testOverAlignedType() {
    constexpr size_t kCapacity = 4;
    alignas(eng::SlotMap<OverAligned64>::requiredAlignment())
        std::array<std::byte, eng::SlotMap<OverAligned64>::requiredBytes(kCapacity)>
            buffer{};

    TEST_CHECK(eng::SlotMap<OverAligned64>::requiredAlignment() >= 64);

    eng::SlotMap<OverAligned64> map(buffer, kCapacity);

    for (size_t i = 0; i < kCapacity; ++i) {
        const eng::GenerationalHandle h = map.emplace();
        TEST_CHECK(h.isValid());
        OverAligned64* ptr = map.get(h);
        TEST_CHECK(ptr != nullptr);
        // Verify 64-byte alignment
        TEST_CHECK(reinterpret_cast<uintptr_t>(ptr) % 64 == 0);
        ptr->tag = static_cast<uint64_t>(i + 1);
    }

    TEST_CHECK(map.isFull());
    map.clear();
    TEST_CHECK(map.isEmpty());

    return 0;
}

int testGenerationOverflowSafety() {
    constexpr size_t kCapacity = 2;
    alignas(eng::SlotMap<int>::requiredAlignment())
        std::array<std::byte, eng::SlotMap<int>::requiredBytes(kCapacity)>
            buffer{};

    eng::SlotMap<int> map(buffer, kCapacity);

    // Force slot 0 generation to near overflow (kRetiredGeneration - 1)
    constexpr uint32_t kNearMax = eng::SlotMap<int>::kRetiredGeneration - 1;
    map.setSlotGenerationForTesting(0, kNearMax);
    TEST_CHECK(map.slotGeneration(0) == kNearMax);

    // Emplace item in slot 0
    const eng::GenerationalHandle h = map.emplace(999);
    TEST_CHECK(h.isValid());
    TEST_CHECK(h.index == 0);
    TEST_CHECK(h.generation == kNearMax);
    TEST_CHECK(*map.get(h) == 999);

    // Erase should advance generation to kRetiredGeneration and permanently retire slot 0
    TEST_CHECK(map.erase(h));
    TEST_CHECK(map.slotGeneration(0) == eng::SlotMap<int>::kRetiredGeneration);
    TEST_CHECK(map.isSlotRetired(0));

    // Old handle is dead
    TEST_CHECK(!map.contains(h));
    TEST_CHECK(map.get(h) == nullptr);

    // A fabricated handle using kRetiredGeneration cannot access the slot
    const eng::GenerationalHandle hRetired(0, eng::SlotMap<int>::kRetiredGeneration);
    TEST_CHECK(!map.contains(hRetired));
    TEST_CHECK(map.get(hRetired) == nullptr);
    TEST_CHECK(!map.erase(hRetired));

    // Slot 0 is NOT recycled on next emplace; only slot 1 is available
    const eng::GenerationalHandle hNext = map.emplace(555);
    TEST_CHECK(hNext.isValid());
    TEST_CHECK(hNext.index == 1); // Emplaced into slot 1, NOT retired slot 0!

    // Map is now completely full because slot 0 is retired and slot 1 is active
    TEST_CHECK(map.isFull());
    TEST_CHECK(map.available() == 0);

    const eng::GenerationalHandle hFail = map.emplace(777);
    TEST_CHECK(!hFail.isValid());

    return 0;
}

int testInvalidHandlesAndRepeatedOperations() {
    constexpr size_t kCapacity = 2;
    alignas(eng::SlotMap<int>::requiredAlignment())
        std::array<std::byte, eng::SlotMap<int>::requiredBytes(kCapacity)>
            buffer{};

    eng::SlotMap<int> map(buffer, kCapacity);

    const eng::GenerationalHandle h = map.emplace(7);
    TEST_CHECK(h.isValid());

    // First erase succeeds
    TEST_CHECK(map.erase(h));

    // Repeated erase on already erased handle fails safely
    TEST_CHECK(!map.erase(h));
    TEST_CHECK(!map.erase(h));
    TEST_CHECK(map.get(h) == nullptr);
    TEST_CHECK(!map.contains(h));

    // Erase invalid sentinels
    TEST_CHECK(!map.erase(eng::GenerationalHandle{}));
    TEST_CHECK(!map.erase(eng::GenerationalHandle::invalid()));
    TEST_CHECK(!map.erase(eng::GenerationalHandle(100, 1)));

    return 0;
}

int testMoveSemanticsAndSelfMove() {
    constexpr size_t kCapacity = 4;
    alignas(eng::SlotMap<int>::requiredAlignment())
        std::array<std::byte, eng::SlotMap<int>::requiredBytes(kCapacity)>
            buffer{};

    eng::SlotMap<int> map1(buffer, kCapacity);
    const eng::GenerationalHandle h0 = map1.emplace(10);
    const eng::GenerationalHandle h1 = map1.emplace(20);

    // Move construct
    eng::SlotMap<int> map2(std::move(map1));
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    TEST_CHECK(map1.isEmpty());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    TEST_CHECK(map1.capacity() == 0);

    TEST_CHECK(map2.size() == 2);
    TEST_CHECK(map2.capacity() == kCapacity);
    TEST_CHECK(*map2.get(h0) == 10);
    TEST_CHECK(*map2.get(h1) == 20);

    // Move assign
    eng::SlotMap<int> map3;
    map3 = std::move(map2);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    TEST_CHECK(map2.isEmpty());
    TEST_CHECK(map3.size() == 2);
    TEST_CHECK(*map3.get(h0) == 10);
    TEST_CHECK(*map3.get(h1) == 20);

    // Self-move safety
    auto* selfPtr = &map3;
    map3 = std::move(*selfPtr);
    TEST_CHECK(map3.size() == 2);
    TEST_CHECK(*map3.get(h0) == 10);
    TEST_CHECK(*map3.get(h1) == 20);

    return 0;
}

int testBufferBoundaryAndUnalignedBuffer() {
    // Insufficient buffer size
    std::array<std::byte, 8> tinyBuffer{};
    eng::SlotMap<int> mapTiny(tinyBuffer, 100);
    TEST_CHECK(mapTiny.capacity() == 0);
    TEST_CHECK(mapTiny.isEmpty());
    TEST_CHECK(mapTiny.isFull());
    TEST_CHECK(!mapTiny.emplace(1).isValid());

    // Buffer span constructor computing capacity automatically
    alignas(eng::SlotMap<int>::requiredAlignment())
        std::array<std::byte, eng::SlotMap<int>::requiredBytes(5)>
            autoBuf{};
    eng::SlotMap<int> mapAuto(autoBuf);
    TEST_CHECK(mapAuto.capacity() == 5);
    TEST_CHECK(mapAuto.available() == 5);

    return 0;
}

} // namespace

int main() {
    if (testInsertLookupErase() != 0) {
        return 1;
    }
    if (testSlotReuseWithDifferentGeneration() != 0) {
        return 2;
    }
    if (testStaleAndForeignHandles() != 0) {
        return 3;
    }
    if (testCapacityFull() != 0) {
        return 4;
    }
    if (testClearAndReconstruction() != 0) {
        return 5;
    }
    if (testNonTrivialObjectLifecycle() != 0) {
        return 6;
    }
    if (testOverAlignedType() != 0) {
        return 7;
    }
    if (testGenerationOverflowSafety() != 0) {
        return 8;
    }
    if (testInvalidHandlesAndRepeatedOperations() != 0) {
        return 9;
    }
    if (testMoveSemanticsAndSelfMove() != 0) {
        return 10;
    }
    if (testBufferBoundaryAndUnalignedBuffer() != 0) {
        return 11;
    }

    return 0;
}
