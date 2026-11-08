/// @file id.h
/// @brief Type-safe strong identifier template and monotonic ID generation primitives.
///
/// Purpose:
/// Prevents accidental cross-subsystem ID substitution at compile time without incurring
/// runtime overhead. Distinct entities, assets, resources, and modes use distinct Tag types
/// to enforce compile-time type boundaries.
///
/// Ownership and Lifetime:
/// StrongId is a lightweight, trivially copyable value type (same size and alignment as Rep).
/// It owns no dynamic memory, performs no heap allocations, and contains no pointers.
///
/// Thread-safety:
/// - StrongId: Pure constexpr value type. Safe to copy, compare, and access concurrently.
/// - IdGenerator: Not thread-safe. Distinct instances on distinct threads are safe. Shared
///   generator instances require external synchronization.
///
/// Representation and Invalid Sentinel:
/// - Rep must be an unsigned integral type (std::unsigned_integral).
/// - Default constructed StrongId represents an invalid sentinel with value `kInvalidValue` (0).
/// - Valid IDs minted by IdGenerator begin at 1 and advance monotonically.
/// - Explicit construction from Rep and explicit conversion to Rep prevent accidental implicit
///   coercion to raw integers or foreign ID types.
///
/// Overflow and Exhaustion:
/// - IdGenerator tracks saturation at std::numeric_limits<Rep>::max().
/// - Upon reaching maximum range, subsequent calls return a failure Result with
/// ErrorCode::OutOfBounds.
/// - Integer wraparound is strictly prevented; IDs are never recycled or duplicated.
///
/// Performance:
/// Zero runtime overhead compared to fundamental unsigned integers. Passable in CPU registers.
/// Fully compatible with standard layout and trivial copyability requirements.
///
/// Example:
/// @code
/// struct EntityTag {};
/// using EntityId = eng::StrongId<EntityTag>;
///
/// eng::IdGenerator<EntityId> generator;
/// auto result = generator.next();
/// if (result) {
///     EntityId id = result.value();
///     assert(id.isValid());
/// }
/// @endcode

#pragma once

#include <compare>
#include <concepts>
#include <core/assert.h>
#include <core/error.h>
#include <core/result.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <source_location>
#include <type_traits>

namespace eng {

/// @class StrongId
/// @brief Strongly-typed wrapper around an unsigned integer representation.
///
/// @tparam Tag Tag type used to distinguish ID types at compile time.
/// @tparam Rep Underlying unsigned integer representation (defaults to uint32_t).
template <typename Tag, std::unsigned_integral Rep = uint32_t> class StrongId {
public:
    /// @brief Tag type distinguishing this ID variety.
    using TagType = Tag;

    /// @brief Underlying unsigned integer representation type.
    using Representation = Rep;

    /// @brief Sentinel value indicating an unassigned or invalid identifier.
    static constexpr Rep kInvalidValue = 0;

    /// @brief Constructs an invalid identifier sentinel (value == 0).
    constexpr StrongId() noexcept : value_(kInvalidValue) {}

    /// @brief Explicitly constructs a StrongId from an integer value.
    ///
    /// @param val Raw integer identifier.
    explicit constexpr StrongId(Rep val) noexcept : value_(val) {}

    /// @brief Returns the underlying integer identifier.
    [[nodiscard]] constexpr Rep value() const noexcept { return value_; }

    /// @brief Checks whether this identifier holds a valid (non-zero) value.
    [[nodiscard]] constexpr bool isValid() const noexcept { return value_ != kInvalidValue; }

    /// @brief Contextual conversion to bool; returns true if valid.
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return isValid(); }

    /// @brief Explicit conversion operator to underlying representation.
    [[nodiscard]] explicit constexpr operator Rep() const noexcept { return value_; }

    /// @brief Returns a canonical invalid identifier sentinel.
    [[nodiscard]] static constexpr StrongId invalid() noexcept { return StrongId{kInvalidValue}; }

    /// @brief Default equality and three-way comparison operators.
    [[nodiscard]] constexpr bool operator==(const StrongId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const StrongId&) const noexcept = default;

private:
    Rep value_{kInvalidValue};
};

// --- Type Utilities and Concepts ---

/// @struct IsStrongId
/// @brief Compile-time trait detecting whether T is a StrongId specialization.
template <typename T> struct IsStrongId : std::false_type {};

template <typename Tag, typename Rep> struct IsStrongId<StrongId<Tag, Rep>> : std::true_type {};

/// @brief Helper variable template for IsStrongId.
template <typename T> inline constexpr bool kIsStrongId = IsStrongId<T>::value;

/// @concept StrongIdType
/// @brief Concept satisfied by any StrongId<Tag, Rep> instantiation.
template <typename T>
concept StrongIdType = kIsStrongId<T>;

/// @struct UnderlyingId
/// @brief Trait extracting the integer representation from an ID type.
template <typename T> struct UnderlyingId {
    using Type = T;
};

template <typename Tag, typename Rep> struct UnderlyingId<StrongId<Tag, Rep>> {
    using Type = Rep;
};

/// @brief Helper type alias for UnderlyingId.
template <typename T> using UnderlyingIdType = UnderlyingId<T>::Type;

/// @concept IdLike
/// @brief Concept satisfied by either an unsigned fundamental integer or a StrongId.
template <typename T>
concept IdLike = std::unsigned_integral<T> || StrongIdType<T>;

// --- ID Generator ---

/// @class IdGenerator
/// @brief Monotonic identifier generator with saturation handling and zero heap allocation.
///
/// Guarantees:
/// - Monotonically increasing identifier sequence.
/// - Zero duplicate IDs.
/// - Exhaustion detection without integer wraparound.
/// - Explicit Result failure when capacity is exhausted.
/// - Non-copyable to prevent accidental divergent duplicate streams.
///
/// @tparam Id Identifier type to generate (StrongId or raw unsigned integral).
template <IdLike Id> class IdGenerator {
public:
    /// @brief Underlying representation type.
    using Rep = UnderlyingIdType<Id>;

    static_assert(std::unsigned_integral<Rep>, "Id representation must be an unsigned integer");

    /// @brief Constructs an ID generator starting from startValue (defaults to 1).
    ///
    /// Preconditions: If startValue == 0, it is automatically adjusted to 1.
    ///
    /// @param startValue First ID value to mint.
    constexpr explicit IdGenerator(Rep startValue = 1) noexcept
        : nextValue_(startValue > 0 ? startValue : 1) {}

    // Non-copyable to prevent accidental duplicate identifier generation
    IdGenerator(const IdGenerator&) = delete;
    IdGenerator& operator=(const IdGenerator&) = delete;

    /// @brief Move constructor.
    constexpr IdGenerator(IdGenerator&& other) noexcept = default;

    /// @brief Move assignment operator.
    constexpr IdGenerator& operator=(IdGenerator&& other) noexcept = default;

    ~IdGenerator() = default;

    /// @brief Generates the next monotonic identifier.
    ///
    /// Postconditions:
    /// - If not exhausted: returns successful Result with minted ID, advances state.
    /// - If exhausted: returns failure Result with ErrorCode::OutOfBounds without wraparound.
    ///
    /// Thread-safety: Calling thread only. Not re-entrant.
    /// Performance  : O(1) integer increment. Zero allocations. Hot-path safe.
    ///
    /// @param location Source location of the call site.
    /// @return Result containing valid Id on success, or Error on exhaustion.
    [[nodiscard]] Result<Id, Error>
    next(std::source_location location = std::source_location::current()) noexcept {
        if (isExhausted_) {
            return Result<Id, Error>::failure(
                Error{.code = ErrorCode::OutOfBounds,
                      .message = "ID generator exhausted: maximum range reached",
                      .location = location});
        }

        const Rep valueToMint = nextValue_;
        if (nextValue_ == std::numeric_limits<Rep>::max()) {
            isExhausted_ = true;
        } else {
            ++nextValue_;
        }

        if constexpr (kIsStrongId<Id>) {
            return Result<Id, Error>::success(Id{valueToMint});
        } else {
            return Result<Id, Error>::success(valueToMint);
        }
    }

    /// @brief Checks whether the generator has more IDs available to mint.
    [[nodiscard]] constexpr bool hasNext() const noexcept { return !isExhausted_; }

    /// @brief Checks whether the generator range has been fully exhausted.
    [[nodiscard]] constexpr bool isExhausted() const noexcept { return isExhausted_; }

    /// @brief Returns the number of IDs remaining before exhaustion.
    [[nodiscard]] constexpr Rep remaining() const noexcept {
        if (isExhausted_) {
            return 0;
        }
        return static_cast<Rep>((std::numeric_limits<Rep>::max() - nextValue_) + 1);
    }

    /// @brief Returns the next raw integer value that will be minted.
    [[nodiscard]] constexpr Rep nextValue() const noexcept { return nextValue_; }

    /// @brief Resets the generator state to a new starting value.
    ///
    /// @param startValue First ID value to mint after reset.
    constexpr void reset(Rep startValue = 1) noexcept {
        nextValue_ = (startValue > 0 ? startValue : 1);
        isExhausted_ = false;
    }

private:
    Rep nextValue_{1};
    bool isExhausted_{false};
};

} // namespace eng

template <typename Tag, std::unsigned_integral Rep> struct std::hash<eng::StrongId<Tag, Rep>> {
    constexpr size_t operator()(const eng::StrongId<Tag, Rep>& id) const noexcept {
        return std::hash<Rep>{}(id.value());
    }
};
