/// @file pool_allocator.h
/// @brief Fixed-size, alignment-aware pool allocator with deterministic bitset tracking.
///
/// Purpose:
/// Manages rapid O(1) allocation and O(1) deallocation of uniform, fixed-size memory blocks
/// over caller-provided backing memory. Primarily utilized for high-frequency objects, particle
/// chunks, and component pools.
///
/// Ownership and Lifetime:
/// The PoolAllocator does NOT own the backing buffer. The caller is responsible for providing
/// and maintaining the lifetime of the backing store. The backing memory must strictly outlive
/// the PoolAllocator instance and all pointers handed out by it.
///
/// Object Lifetime and Destruction Contract:
/// - Allocators do not invoke constructors on allocate, nor destructors on deallocate or reset:
///   Memory is treated as raw untyped bytes.
/// - Caller responsibility for non-trivial destruction: If objects with non-trivial destructors
///   (!std::is_trivially_destructible_v<T>) are placed in allocated blocks, the caller is strictly
///   responsible for manually invoking their destructors before calling deallocate() or reset().
/// - Pointer invalidation: Calling reset() reclaims all blocks and invalidates all outstanding
///   pointers previously returned by allocate().
///
/// Allocation State & Double-Deallocation Safety:
/// Block allocation state is deterministically tracked using a compact in-pool bitset (1 bit per
/// block) stored in trailing backing memory. This eliminates false-positive double-free detections
/// regardless of user block contents, while preserving O(1) alloc/dealloc with zero heap usage.
/// Intrusive free-list storage requires each block to be at least sizeof(void*) (8 bytes) and
/// aligned to at least alignof(void*) (8 bytes).
///
/// Thread-safety:
/// Not thread-safe. Multiple threads must not access the same PoolAllocator instance concurrently
/// without external synchronization.
///
/// Performance:
/// - allocate(): O(1) constant time, pops head of intrusive free list and sets bit. Hot-path safe.
/// - deallocate(): O(1) constant time, validates pointer, checks bitset, and pushes to head.
/// - reset(): O(N) where N is block capacity; re-links intrusive free list and clears bitset.
/// - Zero heap allocations. Memory overhead is strictly 1 bit per block in trailing buffer.
///
/// Failure Mode:
/// - allocate() returns nullptr when the pool is exhausted, capacity is zero, or configuration is
/// invalid.
/// - deallocate() returns false and safely ignores pointers that are nullptr, foreign, interior, or
/// double-freed.

#pragma once

#include <core/align.h>
#include <core/assert.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace eng {

/// @class PoolAllocator
/// @brief Fixed-size block allocator operating over a contiguous caller-provided memory buffer.
class PoolAllocator {
public:
    /// @brief Minimum block size required to store intrusive free list pointers.
    static constexpr size_t kMinBlockSize = sizeof(void*);

    /// @brief Minimum block alignment required for intrusive free list storage.
    static constexpr size_t kMinAlignment = alignof(void*);

    /// @brief Constructs an uninitialized pool with zero capacity.
    constexpr PoolAllocator() noexcept = default;

    /// @brief Constructs a pool allocator over a caller-provided memory span.
    ///
    /// Preconditions : backingMemory must reference valid memory or be empty.
    ///                 alignment must be a power of two.
    /// Ownership     : Backing memory lifetime must exceed that of the pool allocator.
    ///
    /// @param backingMemory Non-owning view of contiguous memory.
    /// @param blockSize Requested size in bytes for each allocation block.
    /// @param alignment Target byte alignment for each block (minimum 8 bytes).
    PoolAllocator(std::span<std::byte> backingMemory, size_t blockSize,
                  size_t alignment = alignof(std::max_align_t)) noexcept;

    /// @brief Constructs a pool allocator over raw pointer and byte capacity.
    ///
    /// Preconditions : If capacity > 0, buffer must not be nullptr.
    ///                 alignment must be a power of two.
    ///
    /// @param buffer Pointer to contiguous backing memory.
    /// @param capacity Total available memory in bytes.
    /// @param blockSize Requested size in bytes for each allocation block.
    /// @param alignment Target byte alignment for each block (minimum 8 bytes).
    PoolAllocator(std::byte* buffer, size_t capacity, size_t blockSize,
                  size_t alignment = alignof(std::max_align_t)) noexcept;

    // PoolAllocator is non-copyable to prevent aliasing of free-list pointers.
    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    /// @brief Move constructor transfers buffer, bitset, and free list, leaving source empty.
    PoolAllocator(PoolAllocator&& other) noexcept;

    /// @brief Move assignment transfers buffer, bitset, and free list, leaving source empty.
    PoolAllocator& operator=(PoolAllocator&& other) noexcept;

    ~PoolAllocator() = default;

    /// @brief Allocates a single fixed-size block from the pool.
    ///
    /// Postconditions: Returned pointer is aligned to effective alignment and sized to at least
    ///                 blockSize() bytes, or nullptr if pool is exhausted or uninitialized.
    /// Thread-safety : Calling thread only. Not re-entrant.
    /// Performance   : O(1) constant time, hot-path safe. Zero heap allocations.
    ///
    /// @return Pointer to allocated block, or nullptr if capacity is exhausted.
    [[nodiscard]] void* allocate() noexcept;

    /// @brief Type-safe allocation helper for a single instance of type T.
    ///
    /// Preconditions : sizeof(T) must be <= effectiveBlockSize() and alignof(T) <= alignment().
    /// Postconditions: Allocates raw memory suitable for T. Does NOT invoke constructors.
    ///
    /// @tparam T Type to allocate storage for.
    /// @return Typed pointer to allocated block, or nullptr on failure or size mismatch.
    template <typename T> [[nodiscard]] T* allocate() noexcept {
        static_assert(!std::is_void_v<T>, "Cannot allocate void elements");
        static_assert(!std::is_reference_v<T>, "Cannot allocate reference types");

        if (sizeof(T) > effectiveBlockSize_ || alignof(T) > alignment_) {
            return nullptr;
        }
        return static_cast<T*>(allocate());
    }

    /// @brief Returns an allocated block back to the pool's free list.
    ///
    /// Validates that ptr is non-null, belongs to the pool buffer, points to the beginning of a
    /// valid block, and is currently marked allocated in the bitset (double-free detection).
    ///
    /// Postconditions: Block is recycled for subsequent allocate() calls. Does NOT invoke
    /// destructors. Thread-safety : Calling thread only. Not re-entrant. Performance   : O(1)
    /// constant time.
    ///
    /// @param ptr Pointer previously returned by allocate().
    /// @return True if successfully deallocated; false if ptr was invalid, foreign, interior, or
    /// already freed.
    bool deallocate(void* ptr) noexcept;

    /// @brief Type-safe deallocation helper.
    ///
    /// Note: Does NOT invoke T's destructor. Caller is responsible for destroying non-trivial
    /// objects.
    ///
    /// @tparam T Object type.
    /// @param ptr Pointer previously returned by allocate<T>().
    /// @return True if successfully returned to the free list; false on rejection.
    template <typename T> bool deallocate(T* ptr) noexcept {
        return deallocate(static_cast<void*>(ptr));
    }

    /// @brief Reclaims all allocated blocks and restores the free list and bitset to initial state.
    ///
    /// Postconditions: All outstanding pointers previously returned by allocate() become INVALID.
    /// Invariants    : Does not invoke destructors on active objects.
    /// Performance   : O(N) where N is capacity(); re-links free list and clears bitset.
    void reset() noexcept;

    /// @brief Total number of fixed-size blocks supported by the backing memory.
    [[nodiscard]] constexpr size_t capacity() const noexcept { return totalBlocks_; }

    /// @brief Number of blocks currently allocated and in active use.
    [[nodiscard]] constexpr size_t allocatedCount() const noexcept { return allocatedCount_; }

    /// @brief Number of blocks currently available for allocation.
    [[nodiscard]] constexpr size_t availableCount() const noexcept {
        return totalBlocks_ - allocatedCount_;
    }

    /// @brief Caller-requested block size in bytes.
    [[nodiscard]] constexpr size_t blockSize() const noexcept { return blockSize_; }

    /// @brief Actual aligned byte stride between blocks (>= max(blockSize, kMinBlockSize)).
    [[nodiscard]] constexpr size_t effectiveBlockSize() const noexcept {
        return effectiveBlockSize_;
    }

    /// @brief Effective alignment applied to each block (>= max(alignment, kMinAlignment)).
    [[nodiscard]] constexpr size_t alignment() const noexcept { return alignment_; }

    /// @brief Returns true if zero blocks are currently allocated.
    [[nodiscard]] constexpr bool isEmpty() const noexcept { return allocatedCount_ == 0; }

    /// @brief Returns true if all blocks are currently allocated (availableCount() == 0).
    [[nodiscard]] constexpr bool isFull() const noexcept {
        return totalBlocks_ > 0 && allocatedCount_ == totalBlocks_;
    }

    /// @brief Non-owning pointer to the beginning of the caller-provided backing memory.
    [[nodiscard]] constexpr const std::byte* data() const noexcept { return buffer_; }

    /// @brief Non-owning pointer to the first aligned block within the backing memory.
    [[nodiscard]] constexpr const std::byte* firstBlock() const noexcept { return firstBlock_; }

private:
    struct FreeNode {
        FreeNode* next{nullptr};
    };

    void initializeFreeList() noexcept;

    std::byte* buffer_{nullptr};
    std::byte* firstBlock_{nullptr};
    uint64_t* bitset_{nullptr};
    size_t bitsetWordCount_{0};
    size_t capacityBytes_{0};
    size_t blockSize_{0};
    size_t effectiveBlockSize_{0};
    size_t alignment_{0};
    size_t totalBlocks_{0};
    size_t allocatedCount_{0};
    FreeNode* head_{nullptr};
};

} // namespace eng
