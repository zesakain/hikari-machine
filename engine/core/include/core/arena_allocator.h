/// @file arena_allocator.h
/// @brief Grouped subsystem and mode-level arena allocator with scoped marker rewind.
///
/// Purpose:
/// Manages grouped memory allocations over a caller-provided backing buffer.
/// Designed for subsystem lifecycles, level/scene lifetimes, and standby-mode speculative
/// preparation where allocations are bulk-freed together or rolled back to a marker.
///
/// Ownership and Lifetime:
/// The ArenaAllocator does NOT own the backing buffer. The caller is responsible for providing
/// and maintaining the lifetime of the backing store.
/// Calling reset() or restore(marker) invalidates all pointers allocated past that point.
///
/// Thread-safety:
/// Not thread-safe. Multiple threads must not access the same ArenaAllocator concurrently.
/// Each subsystem or mode execution phase must maintain its own ArenaAllocator instance.
///
/// Performance:
/// O(1) allocation and O(1) bulk reset / marker restoration. Zero heap allocations.
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

/// @class ArenaAllocator
/// @brief Grouped bump allocator supporting scoped checkpoints (markers) and bulk reset.
class ArenaAllocator {
public:
    /// @brief Opaque marker representing an allocation checkpoint.
    using Marker = size_t;

    /// @brief Constructs an empty arena with zero capacity.
    constexpr ArenaAllocator() noexcept = default;

    /// @brief Constructs an arena over caller-provided backing memory span.
    ///
    /// Preconditions : backingMemory must reference valid memory or be empty.
    /// Ownership     : Backing memory lifetime must exceed that of the arena.
    ///
    /// @param backingMemory Non-owning view of contiguous memory.
    explicit ArenaAllocator(std::span<std::byte> backingMemory) noexcept;

    /// @brief Constructs an arena over raw pointer and byte capacity.
    ///
    /// Preconditions : If capacity > 0, buffer must not be nullptr.
    ///
    /// @param buffer Pointer to contiguous backing memory.
    /// @param capacity Total available memory in bytes.
    ArenaAllocator(std::byte* buffer, size_t capacity) noexcept;

    // ArenaAllocator is non-copyable to prevent aliasing of allocation offsets and markers.
    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    /// @brief Move constructor transfers buffer and offset, leaving source empty.
    ArenaAllocator(ArenaAllocator&& other) noexcept;

    /// @brief Move assignment transfers buffer and offset, leaving source empty.
    ArenaAllocator& operator=(ArenaAllocator&& other) noexcept;

    ~ArenaAllocator() = default;

    /// @brief Allocates raw bytes with specified alignment.
    ///
    /// Preconditions : alignment must be a non-zero power of two.
    /// Postconditions: Returned pointer is aligned to alignment bytes, or nullptr on failure.
    /// Thread-safety : Calling thread only. Not re-entrant.
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
    /// Postconditions: Memory is allocated but objects are NOT constructed (POD / trivial
    /// initialization).
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

    /// @brief Captures the current allocation checkpoint.
    ///
    /// Used for scoped sub-phase allocations that can be rolled back via restore().
    ///
    /// @return Marker token representing the current offset.
    [[nodiscard]] constexpr Marker save() const noexcept { return offset_; }

    /// @brief Rolls back all allocations made after the specified marker.
    ///
    /// Preconditions : marker must be <= allocatedBytes().
    /// Postconditions: All memory allocated after the marker was captured becomes INVALID.
    /// Invariants    : Does not invoke destructors on rolled-back objects.
    /// Performance   : O(1) constant time.
    ///
    /// @param marker Checkpoint previously returned by save().
    void restore(Marker marker) noexcept;

    /// @brief Bulk-resets the entire arena, reclaiming all allocated capacity.
    ///
    /// Postconditions: All previously allocated pointers from this arena become INVALID.
    /// Invariants    : Does not invoke destructors on allocated objects.
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
