/// @file linear_allocator.h
/// @brief Alignment-aware linear bump allocator over caller-provided memory.
///
/// Purpose:
/// Provides fast, sequential memory allocation over a fixed-capacity backing store.
/// Primary mechanism for per-frame temporary allocations (frame allocators) and
/// scratch calculation buffers in the hot path.
///
/// Ownership and Lifetime:
/// The LinearAllocator does NOT own the backing memory buffer. The caller is responsible
/// for providing and maintaining the lifetime of the backing store. The backing memory
/// must strictly outlive the LinearAllocator instance and all pointers handed out by it.
///
/// Object Lifetime and Destruction Contract:
/// - Allocators do not invoke destructors on reset: Memory is treated as raw untyped bytes.
///   Neither reset() nor allocator destruction calls destructors of allocated objects.
/// - Caller responsibility for non-trivial destruction: If objects with non-trivial destructors
///   (!std::is_trivially_destructible_v<T>) are constructed in memory allocated here, the caller
///   is strictly responsible for manually invoking their destructors before calling reset().
/// - Pointer invalidation: Calling reset() reclaims all allocated capacity and invalidates all
///   previously allocated pointers from this allocator.
///
/// Thread-safety:
/// Not thread-safe. Multiple threads must not access the same LinearAllocator concurrently
/// without external synchronization. Each thread or worker job should own an independent
/// LinearAllocator instance.
///
/// Performance:
/// O(1) allocation time via bump pointer addition. Zero heap allocations. Zero locks.
/// Zero per-allocation metadata overhead.
///
/// Failure Mode:
/// Returns nullptr on capacity exhaustion, zero-size request, invalid alignment, or arithmetic
/// overflow.

#pragma once

#include <core/align.h>
#include <core/assert.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace eng {

/// @class LinearAllocator
/// @brief Sequentially allocates memory by advancing an offset within a caller-provided buffer.
class LinearAllocator {
public:
    /// @brief Constructs an empty allocator with zero capacity.
    constexpr LinearAllocator() noexcept = default;

    /// @brief Constructs an allocator over caller-provided backing memory span.
    ///
    /// Preconditions : backingMemory must reference valid memory or be empty.
    /// Ownership     : Backing memory lifetime must exceed that of the allocator.
    ///
    /// @param backingMemory Non-owning view of contiguous memory.
    explicit LinearAllocator(std::span<std::byte> backingMemory) noexcept;

    /// @brief Constructs an allocator over raw pointer and byte capacity.
    ///
    /// Preconditions : If capacity > 0, buffer must not be nullptr.
    ///
    /// @param buffer Pointer to contiguous backing memory.
    /// @param capacity Total available memory in bytes.
    LinearAllocator(std::byte* buffer, size_t capacity) noexcept;

    // LinearAllocator is non-copyable to prevent accidental aliasing of allocation offsets.
    LinearAllocator(const LinearAllocator&) = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;

    /// @brief Move constructor transfers buffer and offset, leaving source empty.
    LinearAllocator(LinearAllocator&& other) noexcept;

    /// @brief Move assignment transfers buffer and offset, leaving source empty.
    LinearAllocator& operator=(LinearAllocator&& other) noexcept;

    ~LinearAllocator() = default;

    /// @brief Allocates raw bytes with specified alignment.
    ///
    /// Preconditions : alignment must be a non-zero power of two.
    /// Postconditions: Returned pointer is aligned to alignment bytes, or nullptr on failure.
    /// Thread-safety : Main/calling thread only. Not re-entrant.
    /// Performance   : O(1) constant time, hot-path safe. Zero heap allocations.
    ///
    /// @param size Bytes to allocate. If size == 0, returns nullptr without altering offset.
    /// @param alignment Required alignment in bytes (default alignof(std::max_align_t)).
    /// @return Pointer to allocated block, or nullptr if capacity is exhausted or parameters are
    /// invalid.
    [[nodiscard]] void* allocate(size_t size,
                                 size_t alignment = alignof(std::max_align_t)) noexcept;

    /// @brief Type-safe allocation helper for count instances of type T.
    ///
    /// Preconditions : T must not be void or a reference type.
    /// Postconditions: Raw memory suitable for count instances of T is allocated. Objects are NOT
    ///                 constructed or initialized. If T has a non-trivial destructor, the caller
    ///                 is strictly responsible for invoking destructors prior to reset().
    ///
    /// @tparam T Value type to allocate memory for.
    /// @param count Number of consecutive elements to allocate.
    /// @return Typed pointer to contiguous elements, or nullptr on failure.
    template <typename T> [[nodiscard]] T* allocate(size_t count = 1) noexcept {
        static_assert(!std::is_void_v<T>, "Cannot allocate void elements");
        static_assert(!std::is_reference_v<T>, "Cannot allocate reference types");

        if (count == 0) {
            return nullptr;
        }
        if (count > SIZE_MAX / sizeof(T)) {
            return nullptr; // Multiplication overflow protection
        }
        const size_t totalBytes = count * sizeof(T);
        return static_cast<T*>(allocate(totalBytes, alignof(T)));
    }

    /// @brief Resets the allocation offset to zero, reclaiming all allocated capacity.
    ///
    /// Postconditions: All previously allocated pointers from this allocator become INVALID.
    /// Invariants    : Does not invoke destructors on allocated objects. Caller is responsible
    ///                 for destroying non-trivially destructible objects prior to reset().
    /// Performance   : O(1) constant time.
    void reset() noexcept;

    /// @brief Returns the total byte capacity of the backing buffer.
    [[nodiscard]] constexpr size_t capacity() const noexcept { return capacity_; }

    /// @brief Returns the total number of bytes allocated so far, including alignment padding.
    [[nodiscard]] constexpr size_t allocatedBytes() const noexcept { return offset_; }

    /// @brief Returns the remaining unallocated capacity in bytes.
    [[nodiscard]] constexpr size_t remainingBytes() const noexcept { return capacity_ - offset_; }

    /// @brief Returns true if no bytes have been allocated since initialization or last reset.
    [[nodiscard]] constexpr bool isEmpty() const noexcept { return offset_ == 0; }

    /// @brief Returns non-owning pointer to the beginning of the backing buffer.
    [[nodiscard]] constexpr const std::byte* data() const noexcept { return buffer_; }

private:
    std::byte* buffer_{nullptr};
    size_t capacity_{0};
    size_t offset_{0};
};

} // namespace eng
