/// @file id_test.cpp
/// @brief Comprehensive unit tests for StrongId<Tag, Rep> and IdGenerator<Id>.

#include <core/id.h>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

#define TEST_CHECK(condition)                                                                      \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

struct EntityTag {};
struct AssetTag {};
struct ComponentTag {};

using EntityId = eng::StrongId<EntityTag, uint32_t>;
using AssetId = eng::StrongId<AssetTag, uint32_t>;
using ComponentId = eng::StrongId<ComponentTag, uint64_t>;
using ByteId = eng::StrongId<EntityTag, uint8_t>;
using ShortId = eng::StrongId<EntityTag, uint16_t>;

int testTypePropertiesAndZeroOverhead() {
    // Exact sizing matching primitive unsigned integer
    static_assert(sizeof(ByteId) == sizeof(uint8_t));
    static_assert(sizeof(ShortId) == sizeof(uint16_t));
    static_assert(sizeof(EntityId) == sizeof(uint32_t));
    static_assert(sizeof(ComponentId) == sizeof(uint64_t));

    // Alignment matching representation
    static_assert(alignof(ByteId) == alignof(uint8_t));
    static_assert(alignof(ShortId) == alignof(uint16_t));
    static_assert(alignof(EntityId) == alignof(uint32_t));
    static_assert(alignof(ComponentId) == alignof(uint64_t));

    // Standard layout and trivial copyability
    static_assert(std::is_trivially_copyable_v<EntityId>);
    static_assert(std::is_standard_layout_v<EntityId>);
    static_assert(std::is_trivially_destructible_v<EntityId>);

    return 0;
}

int testDefaultAndInvalidSentinels() {
    // Default constructor produces invalid sentinel
    const EntityId defId;
    TEST_CHECK(!defId.isValid());
    TEST_CHECK(!static_cast<bool>(defId));
    TEST_CHECK(defId.value() == 0);
    TEST_CHECK(defId.value() == EntityId::kInvalidValue);

    // Explicit invalid() factory
    const EntityId invId = EntityId::invalid();
    TEST_CHECK(!invId.isValid());
    TEST_CHECK(defId == invId);

    // Explicit construction with 0 is invalid
    const EntityId zeroId(0);
    TEST_CHECK(!zeroId.isValid());
    TEST_CHECK(zeroId == defId);

    // Explicit construction with non-zero is valid
    const EntityId validId(1);
    TEST_CHECK(validId.isValid());
    TEST_CHECK(static_cast<bool>(validId));
    TEST_CHECK(validId.value() == 1);

    return 0;
}

int testCompileTimeTagSafety() {
    // Distinct tags cannot be converted or assigned to each other
    static_assert(!std::is_same_v<EntityId, AssetId>);
    static_assert(!std::is_convertible_v<EntityId, AssetId>);
    static_assert(!std::is_constructible_v<EntityId, AssetId>);
    static_assert(!std::is_assignable_v<EntityId, AssetId>);
    static_assert(!std::is_assignable_v<AssetId, EntityId>);

    // Cannot convert from different integer representation
    static_assert(!std::is_convertible_v<EntityId, ComponentId>);
    static_assert(!std::is_constructible_v<EntityId, ComponentId>);

    return 0;
}

int testComparisonsAndOrdering() {
    const EntityId id1(10);
    const EntityId id2(20);
    const EntityId id3(10);

    // Equality & Inequality
    TEST_CHECK(id1 == id3);
    TEST_CHECK(id1 != id2);
    TEST_CHECK(!(id1 == id2));
    TEST_CHECK(!(id1 != id3));

    // Three-way comparison / ordering
    TEST_CHECK(id1 < id2);
    TEST_CHECK(id1 <= id2);
    TEST_CHECK(id1 <= id3);
    TEST_CHECK(id2 > id1);
    TEST_CHECK(id2 >= id1);
    TEST_CHECK(id1 >= id3);

    return 0;
}

int testExplicitConstructionAndConversion() {
    // No implicit conversions to or from raw integer
    static_assert(!std::is_convertible_v<uint32_t, EntityId>);
    static_assert(!std::is_convertible_v<EntityId, uint32_t>);

    // Explicit construction and value retrieval
    constexpr uint32_t kRaw = 12345;
    const EntityId id(kRaw);
    TEST_CHECK(id.value() == kRaw);
    TEST_CHECK(static_cast<uint32_t>(id) == kRaw);

    return 0;
}

int testCompileTimeTraitsAndConcepts() {
    // kIsStrongId
    static_assert(eng::kIsStrongId<EntityId>);
    static_assert(eng::kIsStrongId<AssetId>);
    static_assert(eng::kIsStrongId<ComponentId>);
    static_assert(!eng::kIsStrongId<uint32_t>);
    static_assert(!eng::kIsStrongId<int>);

    // StrongIdType concept
    static_assert(eng::StrongIdType<EntityId>);
    static_assert(!eng::StrongIdType<uint32_t>);

    // UnderlyingIdType
    static_assert(std::is_same_v<eng::UnderlyingIdType<EntityId>, uint32_t>);
    static_assert(std::is_same_v<eng::UnderlyingIdType<ComponentId>, uint64_t>);
    static_assert(std::is_same_v<eng::UnderlyingIdType<ByteId>, uint8_t>);
    static_assert(std::is_same_v<eng::UnderlyingIdType<uint32_t>, uint32_t>);

    // IdLike concept
    static_assert(eng::IdLike<EntityId>);
    static_assert(eng::IdLike<uint32_t>);
    static_assert(eng::IdLike<uint64_t>);
    static_assert(!eng::IdLike<int>); // Signed integers rejected
    static_assert(!eng::IdLike<double>);

    return 0;
}

int testGeneratorMonotonicAndUnique() {
    eng::IdGenerator<EntityId> gen;

    TEST_CHECK(gen.hasNext());
    TEST_CHECK(!gen.isExhausted());
    TEST_CHECK(gen.nextValue() == 1);

    constexpr size_t kCount = 100;
    uint32_t previousVal = 0;

    for (size_t i = 0; i < kCount; ++i) {
        auto result = gen.next();
        TEST_CHECK(result.hasValue());
        const EntityId id = result.value();
        TEST_CHECK(id.isValid());
        TEST_CHECK(id.value() > previousVal);
        TEST_CHECK(id.value() == i + 1);
        previousVal = id.value();
    }

    TEST_CHECK(gen.hasNext());
    TEST_CHECK(!gen.isExhausted());
    TEST_CHECK(gen.nextValue() == kCount + 1);

    return 0;
}

int testGeneratorExhaustionAndOverflow() {
    // Test with 8-bit ID starting at 250 (max is 255)
    eng::IdGenerator<ByteId> gen(250);

    TEST_CHECK(gen.remaining() == 6); // 250, 251, 252, 253, 254, 255
    TEST_CHECK(gen.hasNext());

    for (uint8_t expected = 250; expected < 255; ++expected) {
        auto res = gen.next();
        TEST_CHECK(res.hasValue());
        TEST_CHECK(res.value().value() == expected);
        TEST_CHECK(gen.hasNext());
    }

    // Last valid ID: 255
    auto lastRes = gen.next();
    TEST_CHECK(lastRes.hasValue());
    TEST_CHECK(lastRes.value().value() == 255);

    // Now exhausted: no wraparound!
    TEST_CHECK(gen.isExhausted());
    TEST_CHECK(!gen.hasNext());
    TEST_CHECK(gen.remaining() == 0);

    // Subsequent calls must fail with ErrorCode::OutOfBounds
    auto fail1 = gen.next();
    TEST_CHECK(fail1.isError());
    TEST_CHECK(fail1.error().code == eng::ErrorCode::OutOfBounds);

    auto fail2 = gen.next();
    TEST_CHECK(fail2.isError());
    TEST_CHECK(fail2.error().code == eng::ErrorCode::OutOfBounds);

    // Resetting restores operation
    gen.reset(1);
    TEST_CHECK(gen.hasNext());
    TEST_CHECK(!gen.isExhausted());
    auto resetRes = gen.next();
    TEST_CHECK(resetRes.hasValue());
    TEST_CHECK(resetRes.value().value() == 1);

    return 0;
}

int testDifferentIntegerRepresentations() {
    // 16-bit generator
    eng::IdGenerator<ShortId> gen16(65530);
    for (uint16_t expected = 65530; expected < 65535; ++expected) {
        auto res = gen16.next();
        TEST_CHECK(res.hasValue());
        TEST_CHECK(res.value().value() == expected);
    }
    auto last16 = gen16.next();
    TEST_CHECK(last16.hasValue());
    TEST_CHECK(last16.value().value() == 65535);
    TEST_CHECK(gen16.isExhausted());
    TEST_CHECK(gen16.next().isError());

    // 64-bit generator
    eng::IdGenerator<ComponentId> gen64;
    auto res64 = gen64.next();
    TEST_CHECK(res64.hasValue());
    TEST_CHECK(res64.value().value() == 1);

    // Raw unsigned int generator (non-strong ID)
    eng::IdGenerator<uint32_t> rawGen(10);
    auto rawRes = rawGen.next();
    TEST_CHECK(rawRes.hasValue());
    TEST_CHECK(rawRes.value() == 10);

    return 0;
}

int testMoveSemanticsOfGenerator() {
    eng::IdGenerator<EntityId> gen1(10);
    auto r1 = gen1.next();
    TEST_CHECK(r1.hasValue());
    TEST_CHECK(r1.value().value() == 10);

    // Move construction
    eng::IdGenerator<EntityId> gen2(std::move(gen1));
    auto r2 = gen2.next();
    TEST_CHECK(r2.hasValue());
    TEST_CHECK(r2.value().value() == 11);

    // Move assignment
    eng::IdGenerator<EntityId> gen3;
    gen3 = std::move(gen2);
    auto r3 = gen3.next();
    TEST_CHECK(r3.hasValue());
    TEST_CHECK(r3.value().value() == 12);

    return 0;
}

} // namespace

int main() {
    if (testTypePropertiesAndZeroOverhead() != 0) {
        return 1;
    }
    if (testDefaultAndInvalidSentinels() != 0) {
        return 2;
    }
    if (testCompileTimeTagSafety() != 0) {
        return 3;
    }
    if (testComparisonsAndOrdering() != 0) {
        return 4;
    }
    if (testExplicitConstructionAndConversion() != 0) {
        return 5;
    }
    if (testCompileTimeTraitsAndConcepts() != 0) {
        return 6;
    }
    if (testGeneratorMonotonicAndUnique() != 0) {
        return 7;
    }
    if (testGeneratorExhaustionAndOverflow() != 0) {
        return 8;
    }
    if (testDifferentIntegerRepresentations() != 0) {
        return 9;
    }
    if (testMoveSemanticsOfGenerator() != 0) {
        return 10;
    }

    return 0;
}
