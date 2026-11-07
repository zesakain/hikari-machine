/// @file slot_map.h
/// @brief Fixed-capacity generational slot map container backed by caller-provided memory.
///
/// Purpose:
/// Provides an associative, stable-reference object container indexed exclusively by
/// GenerationalHandle. When slots are recycled, generation counters advance to guarantee
/// that stale or recycled handles cannot access newly created objects (preventing ABA bugs).
///
/// Ownership and Lifetime Contract:
/// - Backing Memory: The SlotMap does NOT own its backing buffer. The caller is responsible
///   for providing and maintaining the lifetime of the backing memory. The backing memory
///   must strictly outlive the SlotMap instance and all pointers handed out by it.
/// - Object Lifecycle: The SlotMap owns the construction and destruction of objects `T` stored
///   within its active slots. When an element is erased, or when the SlotMap is cleared or
///   destroyed, the destructor of `T` is invoked deterministically in-place.
/// - Pointer Stability: Objects are constructed directly in-place within their assigned slot.
///   Objects are never relocated or moved in memory upon insertion or removal of other elements.
///   Pointers returned by get() remain stable until that specific slot is erased, cleared, or the
///   SlotMap is destroyed.
/// - Storage Pointer Prohibition: Per AGENTS.md rules, callers must not store raw pointers across
///   frame boundaries; references across frames must be retained as GenerationalHandle values.
///
/// Alignment & Memory Layout:
/// Backing memory must satisfy `requiredAlignment()`, which is `max(alignof(T), alignof(uint32_t),
/// alignof(void*))`. Each slot holds its metadata alongside raw storage aligned to `alignof(T)`.
/// If an unaligned buffer is provided, SlotMap aligns within the buffer if sufficient padding
/// exists, or safely refuses initialization if space is inadequate.
///
/// Stale Handles & Generation Overflow Safety:
/// - Generations increment monotonically upon slot reclamation.
/// - If a slot's generation counter reaches `kRetiredGeneration` (0xFFFF'FFFF), the slot is
///   permanently retired and excluded from the free list. This ensures generations never wrap
///   around to zero (`kInvalidGeneration`) or to previously minted generations, completely
///   eliminating the possibility of stale handle resurrection.
///
/// Thread-safety:
/// Not thread-safe. A SlotMap instance must not be modified or read concurrently across threads
/// without external synchronization. Safe to call from a single thread or job.
///
/// Performance:
/// - emplace(): O(1) constant time. Pops free list, placement-constructs T. Zero heap allocation.
/// - get(), contains(): O(1) constant time. Direct array index validation and generation check.
/// - erase(): O(1) constant time. In-place destruction, increments generation, pushes to free list.
/// - clear(): O(N) where N is capacity. Destructs active objects, advances generations, rebuilds
/// list.
/// - Zero heap allocations across all operations.
///
/// Failure Modes:
/// - emplace() returns GenerationalHandle::invalid() when capacity is exhausted or configuration is
///   invalid, leaving all existing state intact without throwing exceptions or corrupting storage.
/// - get() returns nullptr on invalid, stale, foreign, or out-of-range handles.
/// - erase() returns false when passed invalid, stale, foreign, or already-erased handles.
///
/// Example:
/// @code
/// alignas(SlotMap<Entity>::requiredAlignment())
/// std::byte buffer[SlotMap<Entity>::requiredBytes(128)];
/// SlotMap<Entity> map(buffer, 128);
///
/// GenerationalHandle h = map.emplace(42, "Player");
/// if (Entity* e = map.get(h)) {
///     e->update();
/// }
/// map.erase(h);
/// assert(map.get(h) == nullptr); // Stale handle safely detected
/// @endcode

#pragma once

#include <algorithm>
#include <concepts>
#include <core/align.h>
#include <core/assert.h>
#include <core/generational_handle.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

namespace eng {

/// @class SlotMap
/// @brief Generational container providing O(1) handle-based access over caller-provided memory.
///
/// @tparam T Object value type to store. Must not be void or a reference.
template <typename T> class SlotMap {
    static_assert(!std::is_void_v<T>, "SlotMap cannot store void");
    static_assert(!std::is_reference_v<T>, "SlotMap cannot store references");

public:
    /// @brief Generation value assigned to permanently retired slots to prevent ABA resurrection.
    static constexpr uint32_t kRetiredGeneration = std::numeric_limits<uint32_t>::max();

    /// @brief Internal slot alignment requirement matching the strictest alignment of T,
    /// uint32_t, and void*.
    static constexpr size_t kSlotAlignment =
        std::max({alignof(T), alignof(uint32_t), alignof(void*)});

    /// @struct Slot
    /// @brief Internal representation of a generational slot holding raw storage and metadata.
    struct alignas(kSlotAlignment) Slot {
        alignas(alignof(T)) std::byte storage[sizeof(T)];
        uint32_t generation{1};
        uint32_t nextFree{GenerationalHandle::kInvalidIndex};
        bool isAllocated{false};
        bool isRetired{false};
    };

    static_assert(alignof(Slot) >= alignof(T), "Slot alignment must satisfy T");
    static_assert(offsetof(Slot, storage) == 0,
                  "Slot storage must reside at offset 0 for alignment guarantee");

    /// @brief Size in bytes of a single slot (including metadata and alignment padding).
    static constexpr size_t kSlotSize = sizeof(Slot);

    /// @brief Returns the minimum byte alignment required for the caller's backing memory buffer.
    [[nodiscard]] static constexpr size_t requiredAlignment() noexcept { return kSlotAlignment; }

    /// @brief Returns the minimum buffer size in bytes required to store the requested capacity.
    ///
    /// @param capacity Number of slots required.
    /// @return Total bytes required for capacity slots (assuming buffer is already properly
    /// aligned).
    [[nodiscard]] static constexpr size_t requiredBytes(size_t capacity) noexcept {
        return capacity * kSlotSize;
    }

    /// @brief Constructs an empty, uninitialized SlotMap with zero capacity.
    constexpr SlotMap() noexcept = default;

    /// @brief Constructs a SlotMap over a caller-provided memory span with explicit slot capacity.
    ///
    /// Preconditions : If capacity > 0, backingMemory must contain at least
    ///                 requiredBytes(capacity) plus any alignment padding.
    /// Postconditions: If valid, ready to emplace up to capacity elements.
    ///                 If buffer is too small, initializes to empty state (capacity == 0).
    ///
    /// @param backingMemory Caller-provided memory buffer.
    /// @param capacity Requested maximum number of live objects.
    SlotMap(std::span<std::byte> backingMemory, size_t capacity) noexcept {
        initialize(backingMemory.data(), backingMemory.size(), capacity);
    }

    /// @brief Constructs a SlotMap over a caller-provided memory span, automatically computing
    /// capacity.
    ///
    /// @param backingMemory Caller-provided memory buffer. Capacity is computed from available
    /// bytes.
    explicit SlotMap(std::span<std::byte> backingMemory) noexcept {
        if (backingMemory.empty()) {
            return;
        }
        void* aligned = alignUp(backingMemory.data(), kSlotAlignment);
        if (aligned == nullptr) {
            return;
        }
        const auto* alignedBytes = static_cast<const std::byte*>(aligned);
        const auto adjustment = static_cast<size_t>(alignedBytes - backingMemory.data());
        if (backingMemory.size() <= adjustment) {
            return;
        }
        const size_t usableBytes = backingMemory.size() - adjustment;
        const size_t computedCapacity = usableBytes / kSlotSize;
        initialize(backingMemory.data(), backingMemory.size(), computedCapacity);
    }

    /// @brief Constructs a SlotMap over raw pointer and byte buffer size with explicit capacity.
    ///
    /// @param buffer Pointer to contiguous backing memory.
    /// @param bufferSize Total size of backing memory in bytes.
    /// @param capacity Requested maximum number of live objects.
    SlotMap(std::byte* buffer, size_t bufferSize, size_t capacity) noexcept {
        initialize(buffer, bufferSize, capacity);
    }

    /// @brief Constructs a SlotMap over raw pointer and byte buffer size, computing capacity.
    ///
    /// @param buffer Pointer to contiguous backing memory.
    /// @param bufferSize Total size of backing memory in bytes.
    SlotMap(std::byte* buffer, size_t bufferSize) noexcept {
        if (buffer != nullptr && bufferSize > 0) {
            void* aligned = alignUp(buffer, kSlotAlignment);
            if (aligned != nullptr) {
                const auto* alignedBytes = static_cast<const std::byte*>(aligned);
                const auto adjustment = static_cast<size_t>(alignedBytes - buffer);
                if (bufferSize > adjustment) {
                    const size_t usableBytes = bufferSize - adjustment;
                    initialize(buffer, bufferSize, usableBytes / kSlotSize);
                }
            }
        }
    }

    /// @brief Destructor. Destroys all currently allocated objects in-place.
    ~SlotMap() noexcept { clear(); }

    // Non-copyable to prevent aliasing and duplicate object ownership.
    SlotMap(const SlotMap&) = delete;
    SlotMap& operator=(const SlotMap&) = delete;

    /// @brief Move constructor transfers ownership of backing view and active objects.
    SlotMap(SlotMap&& other) noexcept
        : slots_(other.slots_), capacity_(other.capacity_), size_(other.size_),
          availableCount_(other.availableCount_), freeHead_(other.freeHead_) {
        other.slots_ = nullptr;
        other.capacity_ = 0;
        other.size_ = 0;
        other.availableCount_ = 0;
        other.freeHead_ = GenerationalHandle::kInvalidIndex;
    }

    /// @brief Move assignment destroys existing elements and assumes other's storage view.
    SlotMap& operator=(SlotMap&& other) noexcept {
        if (this != &other) {
            clear();
            slots_ = other.slots_;
            capacity_ = other.capacity_;
            size_ = other.size_;
            availableCount_ = other.availableCount_;
            freeHead_ = other.freeHead_;

            other.slots_ = nullptr;
            other.capacity_ = 0;
            other.size_ = 0;
            other.availableCount_ = 0;
            other.freeHead_ = GenerationalHandle::kInvalidIndex;
        }
        return *this;
    }

    /// @brief Constructs a new object of type T in-place and mints an associated
    /// GenerationalHandle.
    ///
    /// Preconditions : None.
    /// Postconditions: If capacity is available, object is constructed in-place, size is
    /// incremented,
    ///                 and a valid GenerationalHandle is returned.
    ///                 If capacity is full, returns GenerationalHandle::invalid() without
    ///                 modifying state.
    /// Thread-safety : Single-thread only.
    /// Performance   : O(1) constant time. Placement new. Zero heap allocations.
    ///
    /// @tparam Args Constructor argument types forwarded to T.
    /// @param args Arguments forwarded to T's constructor.
    /// @return Valid GenerationalHandle on success, or GenerationalHandle::invalid() on failure.
    template <typename... Args> [[nodiscard]] GenerationalHandle emplace(Args&&... args) noexcept {
        if (freeHead_ == GenerationalHandle::kInvalidIndex || availableCount_ == 0) {
            return GenerationalHandle::invalid();
        }

        const uint32_t slotIndex = freeHead_;
        Slot& slot = slots_[slotIndex];

        freeHead_ = slot.nextFree;
        slot.nextFree = GenerationalHandle::kInvalidIndex;

        // Construct object in-place directly in the slot storage
        ::new (static_cast<void*>(slot.storage)) T(std::forward<Args>(args)...);
        slot.isAllocated = true;

        ++size_;
        --availableCount_;

        return GenerationalHandle{slotIndex, slot.generation};
    }

    /// @brief Accesses an object by handle, returning a mutable pointer.
    ///
    /// Preconditions : None.
    /// Thread-safety : Single-thread only.
    /// Performance   : O(1) constant time index and generation check.
    ///
    /// @param handle Handle referencing the target slot.
    /// @return Pointer to object if handle is currently valid; nullptr if handle is stale,
    ///         invalid, unallocated, or out-of-range.
    [[nodiscard]] T* get(GenerationalHandle handle) noexcept {
        if (!handle.isValid() || handle.index >= capacity_) {
            return nullptr;
        }
        Slot& slot = slots_[handle.index];
        if (!slot.isAllocated || slot.generation != handle.generation) {
            return nullptr;
        }
        return std::launder(reinterpret_cast<T*>(slot.storage));
    }

    /// @brief Accesses an object by handle, returning a const pointer.
    ///
    /// @param handle Handle referencing the target slot.
    /// @return Const pointer to object if handle is valid; nullptr otherwise.
    [[nodiscard]] const T* get(GenerationalHandle handle) const noexcept {
        if (!handle.isValid() || handle.index >= capacity_) {
            return nullptr;
        }
        const Slot& slot = slots_[handle.index];
        if (!slot.isAllocated || slot.generation != handle.generation) {
            return nullptr;
        }
        return std::launder(reinterpret_cast<const T*>(slot.storage));
    }

    /// @brief Checks whether a handle references a currently active object.
    ///
    /// @param handle Handle to query.
    /// @return True if handle is currently valid and live; false if stale, unallocated, or invalid.
    [[nodiscard]] bool contains(GenerationalHandle handle) const noexcept {
        return get(handle) != nullptr;
    }

    /// @brief Erases an object referenced by handle, destroying it in-place and reclaiming the
    /// slot.
    ///
    /// Postconditions: If handle was valid, object destructor is invoked, generation counter
    ///                 advances (invalidating this handle and prior handles), and slot is returned
    ///                 to the free list.
    ///                 If slot generation reaches kRetiredGeneration, the slot is permanently
    ///                 retired and excluded from the free list to guarantee overflow safety.
    ///
    /// @param handle Handle of object to erase.
    /// @return True if object was found and erased; false if handle was invalid, stale, or foreign.
    bool erase(GenerationalHandle handle) noexcept {
        if (!handle.isValid() || handle.index >= capacity_) {
            return false;
        }
        Slot& slot = slots_[handle.index];
        if (!slot.isAllocated || slot.generation != handle.generation) {
            return false;
        }

        std::destroy_at(std::launder(reinterpret_cast<T*>(slot.storage)));
        slot.isAllocated = false;
        --size_;

        if (slot.generation < kRetiredGeneration - 1) {
            ++slot.generation;
            slot.nextFree = freeHead_;
            freeHead_ = handle.index;
            ++availableCount_;
        } else {
            // Generation overflow handling:
            // When generation reaches kRetiredGeneration (0xFFFF'FFFF), mark as permanently
            // retired. Do not return to free list, preventing ancient handles from resurrecting.
            slot.generation = kRetiredGeneration;
            slot.isRetired = true;
            slot.nextFree = GenerationalHandle::kInvalidIndex;
        }

        return true;
    }

    /// @brief Destroys all active objects in-place and invalidates all existing handles.
    ///
    /// Postconditions: size() == 0, all active elements destructed, generations of active slots
    ///                 incremented, and free list rebuilt for all non-retired slots.
    void clear() noexcept {
        if (slots_ == nullptr || capacity_ == 0) {
            return;
        }

        for (size_t i = 0; i < capacity_; ++i) {
            Slot& slot = slots_[i];
            if (slot.isAllocated) {
                std::destroy_at(std::launder(reinterpret_cast<T*>(slot.storage)));
                slot.isAllocated = false;
                if (slot.generation < kRetiredGeneration - 1) {
                    ++slot.generation;
                } else {
                    slot.generation = kRetiredGeneration;
                    slot.isRetired = true;
                }
            }
        }

        size_ = 0;
        freeHead_ = GenerationalHandle::kInvalidIndex;
        availableCount_ = 0;

        // Rebuild free list in reverse order so allocations proceed from lowest slot index
        for (size_t i = capacity_; i > 0; --i) {
            const size_t idx = i - 1;
            Slot& slot = slots_[idx];
            if (!slot.isRetired) {
                slot.nextFree = freeHead_;
                freeHead_ = static_cast<uint32_t>(idx);
                ++availableCount_;
            } else {
                slot.nextFree = GenerationalHandle::kInvalidIndex;
            }
        }
    }

    /// @brief Returns the number of currently active objects stored in the SlotMap.
    [[nodiscard]] constexpr size_t size() const noexcept { return size_; }

    /// @brief Returns the total slot capacity configured in the backing storage.
    [[nodiscard]] constexpr size_t capacity() const noexcept { return capacity_; }

    /// @brief Returns the number of slots available for future emplace operations.
    [[nodiscard]] constexpr size_t available() const noexcept { return availableCount_; }

    /// @brief Returns true if no objects are currently stored.
    [[nodiscard]] constexpr bool isEmpty() const noexcept { return size_ == 0; }

    /// @brief Returns true if no more slots are available for allocation.
    [[nodiscard]] constexpr bool isFull() const noexcept { return availableCount_ == 0; }

    /// @brief Testing utility: forces a slot's generation counter (e.g. for deterministic overflow
    /// tests).
    ///
    /// Invariants: Must only be called with index < capacity().
    void setSlotGenerationForTesting(uint32_t index, uint32_t generation) noexcept {
        if (index < capacity_) {
            slots_[index].generation = generation;
        }
    }

    /// @brief Testing utility: inspects a slot's current generation counter.
    [[nodiscard]] uint32_t slotGeneration(uint32_t index) const noexcept {
        if (index < capacity_) {
            return slots_[index].generation;
        }
        return GenerationalHandle::kInvalidGeneration;
    }

    /// @brief Testing utility: inspects whether a slot is permanently retired.
    [[nodiscard]] bool isSlotRetired(uint32_t index) const noexcept {
        if (index < capacity_) {
            return slots_[index].isRetired;
        }
        return false;
    }

private:
    void initialize(std::byte* buffer, size_t bufferSize, size_t requestedCapacity) noexcept {
        if (buffer == nullptr || bufferSize == 0 || requestedCapacity == 0) {
            return;
        }
        if (requestedCapacity >= GenerationalHandle::kInvalidIndex) {
            return;
        }

        void* aligned = alignUp(buffer, kSlotAlignment);
        if (aligned == nullptr) {
            return;
        }

        const auto* alignedBytes = static_cast<const std::byte*>(aligned);
        const auto adjustment = static_cast<size_t>(alignedBytes - buffer);
        if (bufferSize <= adjustment) {
            return;
        }

        const size_t usableBytes = bufferSize - adjustment;
        if (usableBytes < requiredBytes(requestedCapacity)) {
            return; // Backing buffer has insufficient space for requested capacity
        }

        slots_ = static_cast<Slot*>(aligned);
        capacity_ = requestedCapacity;
        size_ = 0;
        availableCount_ = requestedCapacity;
        freeHead_ = 0;

        for (size_t i = 0; i < capacity_; ++i) {
            Slot& slot = slots_[i];
            slot.generation = 1;
            slot.isAllocated = false;
            slot.isRetired = false;
            slot.nextFree = (i + 1 < capacity_) ? static_cast<uint32_t>(i + 1)
                                                : GenerationalHandle::kInvalidIndex;
        }
    }

    Slot* slots_{nullptr};
    size_t capacity_{0};
    size_t size_{0};
    size_t availableCount_{0};
    uint32_t freeHead_{GenerationalHandle::kInvalidIndex};
};

} // namespace eng
