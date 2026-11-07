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
/// and maintaining the lifetime of the backing store. The backing memory must strictly outlive
/// the ArenaAllocator instance and all pointers handed out by it.
///
/// Object Lifetime and Destruction Contract:
/// - Allocators do not invoke destructors on reset or restore: Memory is treated as raw bytes.
///   Neither reset(), restore(), nor allocator destruction calls destructors of allocated objects.
/// - Caller responsibility for non-trivial destruction: If objects with non-trivial destructors
///   (!std::is_trivially_destructible_v<T>) are constructed in memory allocated here, the caller is
///   strictly responsible for manually invoking their destructors before calling reset() or
///   restore().
/// - Pointer invalidation: Calling reset() invalidates all previously allocated pointers from this
///   arena. Calling restore(marker) invalidates all pointers allocated at or after that marker.
///
/// Thread-safety:
/// Not thread-safe. Multiple threads must not access the same ArenaAllocator concurrently without
/// external synchronization. Each subsystem or mode execution phase must maintain its own
/// ArenaAllocator instance.
///
/// Performance:
/// O(1) allocation and O(1) bulk reset / marker restoration. Zero heap allocations.
/// Zero per-allocation metadata overhead.
///
/// Failure Mode:
/// Returns nullptr on capacity exhaustion, zero-size request, invalid alignment, or arithmetic
/// overflow. restore() returns false if the marker is foreign, stale, or out-of-range.

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
    /// @brief Opaque checkpoint token representing an allocation state within an ArenaAllocator.
    ///
    /// Stores the owning arena instance identity, allocation offset, and generation counter.
    /// Designed as a lightweight, standard-layout value type (24 bytes) with zero heap allocation.
    struct Marker {
        const void* arenaId{nullptr};
        size_t offset{0};
        size_t resetId{0};

        [[nodiscard]] constexpr bool operator==(const Marker&) const noexcept = default;
    };

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
    /// Postconditions: Raw memory suitable for count instances of T is allocated. Objects are NOT
    ///                 constructed or initialized. If T has a non-trivial destructor, the caller
    ///                 is strictly responsible for invoking destructors prior to reset() or
    ///                 restore().
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
    /// The returned marker is uniquely bound to this ArenaAllocator instance and its
    /// current reset generation.
    ///
    /// @return Marker token representing the current offset.
    [[nodiscard]] constexpr Marker save() const noexcept {
        return Marker{.arenaId = this, .offset = offset_, .resetId = resetId_};
    }

    /// @brief Rolls back all allocations made after the specified marker.
    ///
    /// Preconditions : marker must have been created by this ArenaAllocator instance, must belong
    ///                 to the current reset generation, and must not exceed current offset.
    /// Postconditions: On success, offset is rewound to marker.offset, and all memory allocated
    ///                 after the marker becomes INVALID. On failure, allocator state is UNCHANGED.
    /// Invariants    : Does not invoke destructors on rolled-back objects. Caller is responsible
    ///                 for destroying non-trivially destructible objects prior to restore().
    /// Thread-safety : Calling thread only. Not re-entrant.
    /// Performance   : O(1) constant time. Zero heap allocations.
    ///
    /// @param marker Checkpoint previously returned by save().
    /// @return True if successfully restored; false if marker is foreign, stale, or out-of-range.
    [[nodiscard]] bool restore(Marker marker) noexcept;

    /// @brief Bulk-resets the entire arena, reclaiming all allocated capacity.
    ///
    /// Postconditions: All previously allocated pointers and saved markers become INVALID.
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
    size_t resetId_{0};
};

} // namespace eng
