/// @file align.h
/// @brief Compile-time and runtime alignment and power-of-two arithmetic utilities.
///
/// Purpose:
/// Provides robust, overflow-safe alignment calculations for memory allocators,
/// GPU buffer offsets, and data-structure padding.
///
/// Thread-safety:
/// Pure constexpr functions with zero side-effects. Safe to call concurrently from any thread.
///
/// Performance:
/// Bitwise arithmetic compiling to single-instruction operations without branches or allocations.

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace eng {

/// @brief Checks whether a given unsigned integer is a non-zero power of two.
///
/// Preconditions : None.
/// Performance   : O(1) bitwise test; single CPU instruction.
///
/// @tparam T Unsigned integral type.
/// @param value Number to test.
/// @return True if value > 0 and value is an exact power of two.
template <std::unsigned_integral T> [[nodiscard]] constexpr bool isPowerOfTwo(T value) noexcept {
    return value > 0 && (value & (value - 1)) == 0;
}

/// @brief Aligns an unsigned integer upwards to the nearest multiple of alignment.
///
/// Preconditions : alignment must be a non-zero power of two.
/// Invariants    : If value + alignment - 1 would overflow T, returns 0.
/// Thread-safety : Pure function.
/// Performance   : O(1) bitwise arithmetic.
///
/// @tparam T Unsigned integral type (e.g. size_t, uintptr_t).
/// @param value Raw value or offset to align.
/// @param alignment Target alignment in bytes (must be power of two).
/// @return Aligned value >= value, or 0 on arithmetic overflow.
template <std::unsigned_integral T>
[[nodiscard]] constexpr T alignUp(T value, size_t alignment) noexcept {
    if (alignment == 0 || !isPowerOfTwo(alignment)) {
        return 0;
    }
    const auto mask = static_cast<T>(alignment - 1);
    if (value > std::numeric_limits<T>::max() - mask) {
        return 0; // Overflow protection
    }
    return (value + mask) & ~mask;
}

/// @brief Aligns a raw pointer upwards to the nearest multiple of alignment via pointer offset.
///
/// Preconditions : alignment must be a non-zero power of two.
/// Thread-safety : Pure function.
/// Performance   : O(1) pointer arithmetic; avoids integer-to-pointer conversion.
///
/// @param ptr Raw pointer to align.
/// @param alignment Target alignment in bytes (must be power of two).
/// @return Aligned pointer >= ptr, or nullptr on invalid alignment.
[[nodiscard]] inline void* alignUp(void* ptr, size_t alignment) noexcept {
    if (ptr == nullptr || alignment == 0 || !isPowerOfTwo(alignment)) {
        return nullptr;
    }
    auto* bytePtr = static_cast<std::byte*>(ptr);
    const auto address = reinterpret_cast<uintptr_t>(bytePtr);
    const auto misalignment = static_cast<size_t>(address & static_cast<uintptr_t>(alignment - 1));
    const size_t padding = (misalignment == 0) ? 0 : (alignment - misalignment);
    if (address > std::numeric_limits<uintptr_t>::max() - padding) {
        return nullptr; // Address overflow protection
    }
    return bytePtr + padding;
}

/// @brief Checks whether a memory address or offset satisfies a given byte alignment.
///
/// Preconditions : alignment must be a non-zero power of two.
/// Thread-safety : Pure function.
///
/// @tparam T Unsigned integral type.
/// @param value Value to check.
/// @return True if value is evenly divisible by alignment.
template <std::unsigned_integral T>
[[nodiscard]] constexpr bool isAligned(T value, size_t alignment) noexcept {
    if (alignment == 0 || !isPowerOfTwo(alignment)) {
        return false;
    }
    return (value & static_cast<T>(alignment - 1)) == 0;
}

/// @brief Checks whether a pointer satisfies a given byte alignment.
///
/// Preconditions : alignment must be a non-zero power of two.
/// Thread-safety : Pure function.
///
/// @param ptr Pointer to inspect.
/// @param alignment Target alignment requirement in bytes.
/// @return True if ptr is evenly divisible by alignment.
[[nodiscard]] inline bool isAligned(const void* ptr, size_t alignment) noexcept {
    return isAligned(reinterpret_cast<uintptr_t>(ptr), alignment);
}

} // namespace eng
