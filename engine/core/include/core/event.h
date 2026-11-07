/// @file event.h
/// @brief Core typed event system with bounded queuing and allocation-free dispatch.
///
/// Purpose:
/// Provides decoupled, type-safe inter-component communication without direct subsystem
/// dependencies. Events are plain C++ data types with zero virtual base classes or RTTI.
///
/// Ownership and Lifetime Contract:
/// - Event Objects: Events are value types passed as const references during dispatch.
///   Event payloads can be non-trivial; the EventQueue guarantees deterministic placement
///   construction and destruction without premature moves or leaks.
/// - Backing Storage: EventDispatcher and EventQueue do NOT own dynamic heap memory.
///   The caller provides contiguous backing buffers whose lifetime must exceed that of the
///   subsystem instances.
/// - Listener Lifetime: Listeners are registered using a function pointer and an optional context
///   pointer. The context pointer must outlive the registration or be unregistered prior to
///   destruction.
///
/// Dispatch and Reentrancy Safety:
/// - Nested dispatch is fully supported.
/// - If a listener is unregistered during dispatch, it is marked pending removal and skipped
///   for the remainder of the active dispatch. Slots are reclaimed when all nested dispatches
///   unwind.
/// - If a listener is registered during dispatch, it will NOT be invoked for the in-flight
/// dispatch,
///   preventing infinite iteration loops.
///
/// Bounded Event Queue:
/// - Operates as a bounded ring buffer over caller-provided memory.
/// - Enqueueing is strictly O(1) and heap-allocation-free.
/// - When capacity is reached, enqueueing fails explicitly and increments droppedCount.
/// - Draining processes a snapshot of currently queued events; events enqueued during drain
///   are preserved for subsequent drains.
///
/// Thread-safety:
/// Not thread-safe. A single dispatcher or queue must not be accessed concurrently across
/// threads without external synchronization. Safe to call within a single job or thread.
///
/// Performance:
/// - dispatch(): O(N) where N is active listeners for that EventTypeId. Zero allocations.
/// - enqueue(): O(1) constant time. In-place placement new. Zero allocations.
/// - drain(): O(M) where M is queued events. Explicit in-place destruction. Zero allocations.

#pragma once

#include <algorithm>
#include <concepts>
#include <core/align.h>
#include <core/assert.h>
#include <core/generational_handle.h>
#include <core/id.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

namespace eng {

namespace detail {
/// @brief Returns the next sequential process-unique event type ID.
[[nodiscard]] uint32_t getNextEventTypeId() noexcept;
} // namespace detail

/// @struct EventTypeTag
/// @brief Tag type for strongly-typed event identifiers.
struct EventTypeTag {};

/// @brief Strongly-typed unique identifier for event types.
using EventTypeId = StrongId<EventTypeTag, uint32_t>;

/// @brief Retrieves the unique compile-time assigned runtime EventTypeId for an Event type.
///
/// Zero RTTI, zero virtual dispatch. Evaluates a process-unique sequential counter once per type.
template <typename Event> [[nodiscard]] inline EventTypeId getEventTypeId() noexcept {
    static const EventTypeId kId{detail::getNextEventTypeId()};
    return kId;
}

/// @struct ListenerTag
/// @brief Tag type for strongly-typed listener tokens.
struct ListenerTag {};

/// @brief Generational registration token returned when a listener is registered.
using ListenerToken = StrongId<ListenerTag, uint64_t>;

/// @class EventDispatcher
/// @brief Type-safe event dispatcher with reentrancy safety and zero-allocation dispatch.
class EventDispatcher {
public:
    /// @brief Internal dispatch function signature.
    using DispatchThunk = void (*)(const void* eventData, void* fnPtr, void* userContext);

    /// @struct ListenerSlot
    /// @brief Storage unit for a single registered listener.
    struct alignas(void*) ListenerSlot {
        uint32_t eventTypeId{0};
        DispatchThunk thunk{nullptr};
        void* fnPtr{nullptr};
        void* userContext{nullptr};
        uint64_t registeredInDispatchId{0};
        uint32_t generation{1};
        uint32_t nextFree{GenerationalHandle::kInvalidIndex};
        bool isAllocated{false};
        bool isPendingRemoval{false};
    };

    static constexpr size_t kSlotSize = sizeof(ListenerSlot);
    static constexpr size_t kSlotAlignment = alignof(ListenerSlot);

    /// @brief Returns the byte alignment required for caller-provided backing memory.
    [[nodiscard]] static constexpr size_t requiredAlignment() noexcept { return kSlotAlignment; }

    /// @brief Returns the bytes required to support maxListeners capacity.
    [[nodiscard]] static constexpr size_t requiredBytes(size_t maxListeners) noexcept {
        return maxListeners * kSlotSize;
    }

    /// @brief Constructs an uninitialized dispatcher with zero capacity.
    constexpr EventDispatcher() noexcept = default;

    /// @brief Constructs a dispatcher over caller-provided memory with explicit capacity.
    EventDispatcher(std::span<std::byte> backingMemory, size_t maxListeners) noexcept {
        initialize(backingMemory.data(), backingMemory.size(), maxListeners);
    }

    /// @brief Constructs a dispatcher over caller-provided memory, computing capacity.
    explicit EventDispatcher(std::span<std::byte> backingMemory) noexcept {
        if (!backingMemory.empty()) {
            void* aligned = alignUp(backingMemory.data(), kSlotAlignment);
            if (aligned != nullptr) {
                const auto* alignedBytes = static_cast<const std::byte*>(aligned);
                const auto adjustment = static_cast<size_t>(alignedBytes - backingMemory.data());
                if (backingMemory.size() > adjustment) {
                    const size_t usable = backingMemory.size() - adjustment;
                    initialize(backingMemory.data(), backingMemory.size(), usable / kSlotSize);
                }
            }
        }
    }

    /// @brief Constructs a dispatcher over raw pointer and buffer size with explicit capacity.
    EventDispatcher(std::byte* buffer, size_t bufferSize, size_t maxListeners) noexcept {
        initialize(buffer, bufferSize, maxListeners);
    }

    ~EventDispatcher() noexcept { clear(); }

    EventDispatcher(const EventDispatcher&) = delete;
    EventDispatcher& operator=(const EventDispatcher&) = delete;

    /// @brief Move constructor transfers listener storage and state.
    EventDispatcher(EventDispatcher&& other) noexcept
        : slots_(other.slots_), capacity_(other.capacity_), size_(other.size_),
          availableCount_(other.availableCount_), freeHead_(other.freeHead_),
          dispatchId_(other.dispatchId_), dispatchDepth_(other.dispatchDepth_),
          pendingRemovalCount_(other.pendingRemovalCount_) {
        other.slots_ = nullptr;
        other.capacity_ = 0;
        other.size_ = 0;
        other.availableCount_ = 0;
        other.freeHead_ = GenerationalHandle::kInvalidIndex;
        other.dispatchId_ = 0;
        other.dispatchDepth_ = 0;
        other.pendingRemovalCount_ = 0;
    }

    /// @brief Move assignment operator.
    EventDispatcher& operator=(EventDispatcher&& other) noexcept {
        if (this != &other) {
            clear();
            slots_ = other.slots_;
            capacity_ = other.capacity_;
            size_ = other.size_;
            availableCount_ = other.availableCount_;
            freeHead_ = other.freeHead_;
            dispatchId_ = other.dispatchId_;
            dispatchDepth_ = other.dispatchDepth_;
            pendingRemovalCount_ = other.pendingRemovalCount_;

            other.slots_ = nullptr;
            other.capacity_ = 0;
            other.size_ = 0;
            other.availableCount_ = 0;
            other.freeHead_ = GenerationalHandle::kInvalidIndex;
            other.dispatchId_ = 0;
            other.dispatchDepth_ = 0;
            other.pendingRemovalCount_ = 0;
        }
        return *this;
    }

    /// @brief Registers a free function listener without context.
    template <typename Event>
    [[nodiscard]] ListenerToken registerListener(void (*fn)(const Event&)) noexcept {
        if (fn == nullptr) {
            return ListenerToken::invalid();
        }
        auto thunk = [](const void* evt, void* fnPtr, void* /*ctx*/) {
            auto typedFn = reinterpret_cast<void (*)(const Event&)>(fnPtr);
            typedFn(*static_cast<const Event*>(evt));
        };
        return registerRaw(getEventTypeId<Event>(), thunk, reinterpret_cast<void*>(fn), nullptr);
    }

    /// @brief Registers a free function listener with a context pointer.
    template <typename Event>
    [[nodiscard]] ListenerToken registerListener(void (*fn)(const Event&, void*),
                                                 void* context) noexcept {
        if (fn == nullptr) {
            return ListenerToken::invalid();
        }
        auto thunk = [](const void* evt, void* fnPtr, void* ctx) {
            auto typedFn = reinterpret_cast<void (*)(const Event&, void*)>(fnPtr);
            typedFn(*static_cast<const Event*>(evt), ctx);
        };
        return registerRaw(getEventTypeId<Event>(), thunk, reinterpret_cast<void*>(fn), context);
    }

    /// @brief Registers a member function listener on an instance pointer.
    template <typename Event, typename Receiver, void (Receiver::*Method)(const Event&)>
    [[nodiscard]] ListenerToken registerListener(Receiver* instance) noexcept {
        if (instance == nullptr) {
            return ListenerToken::invalid();
        }
        auto thunk = [](const void* evt, void* /*fnPtr*/, void* ctx) {
            (static_cast<Receiver*>(ctx)->*Method)(*static_cast<const Event*>(evt));
        };
        return registerRaw(getEventTypeId<Event>(), thunk, nullptr, static_cast<void*>(instance));
    }

    /// @brief Unregisters a listener using the token returned during registration.
    ///
    /// If invoked during active dispatch, removal is deferred until all nested dispatches unwind.
    /// The token is immediately invalidated to prevent duplicate unregistration.
    ///
    /// @param token Registration token to unregister.
    /// @return True if listener was found and unregistered; false if token is invalid or stale.
    bool unregisterListener(ListenerToken token) noexcept {
        if (!token.isValid() || slots_ == nullptr) {
            return false;
        }
        const GenerationalHandle handle = GenerationalHandle::fromBits(token.value());
        if (handle.index >= capacity_) {
            return false;
        }
        ListenerSlot& slot = slots_[handle.index];
        if (!slot.isAllocated || slot.isPendingRemoval || slot.generation != handle.generation) {
            return false;
        }

        if (dispatchDepth_ > 0) {
            slot.isPendingRemoval = true;
            ++slot.generation; // Invalidate token immediately
            ++pendingRemovalCount_;
        } else {
            slot.isAllocated = false;
            slot.isPendingRemoval = false;
            ++slot.generation;
            slot.thunk = nullptr;
            slot.fnPtr = nullptr;
            slot.userContext = nullptr;
            slot.nextFree = freeHead_;
            freeHead_ = handle.index;
            --size_;
            ++availableCount_;
        }
        return true;
    }

    /// @brief Synchronously dispatches an event to all registered listeners.
    ///
    /// Preconditions : None.
    /// Thread-safety : Single-thread only. Safe for reentrant dispatches.
    /// Performance   : O(N) where N is listeners for this Event type. Zero heap allocations.
    ///
    /// @tparam Event Plain data event type.
    /// @param event Event payload passed by reference.
    /// @return Number of listeners that were invoked.
    template <typename Event> size_t dispatch(const Event& event) noexcept {
        if (slots_ == nullptr || size_ == 0) {
            return 0;
        }

        const EventTypeId targetTypeId = getEventTypeId<Event>();
        const uint64_t thisDispatchId = ++dispatchId_;
        ++dispatchDepth_;
        size_t invokedCount = 0;

        const size_t cap = capacity_;
        for (size_t i = 0; i < cap; ++i) {
            ListenerSlot& slot = slots_[i];
            if (slot.isAllocated && !slot.isPendingRemoval &&
                slot.eventTypeId == targetTypeId.value()) {
                if (slot.registeredInDispatchId == thisDispatchId) {
                    continue; // Skip listener registered during this exact dispatch execution
                }
                slot.thunk(&event, slot.fnPtr, slot.userContext);
                ++invokedCount;
            }
        }

        --dispatchDepth_;
        if (dispatchDepth_ == 0 && pendingRemovalCount_ > 0) {
            purgePendingRemovals();
        }

        return invokedCount;
    }

    /// @brief Returns the number of listeners currently registered for Event.
    template <typename Event> [[nodiscard]] size_t listenerCount() const noexcept {
        if (slots_ == nullptr) {
            return 0;
        }
        const EventTypeId targetId = getEventTypeId<Event>();
        size_t count = 0;
        for (size_t i = 0; i < capacity_; ++i) {
            if (slots_[i].isAllocated && !slots_[i].isPendingRemoval &&
                slots_[i].eventTypeId == targetId.value()) {
                ++count;
            }
        }
        return count;
    }

    /// @brief Clears all registered listeners and resets the dispatcher.
    void clear() noexcept {
        ENG_ASSERT(dispatchDepth_ == 0, "Cannot clear EventDispatcher during active dispatch");
        if (slots_ == nullptr || capacity_ == 0) {
            return;
        }
        for (size_t i = 0; i < capacity_; ++i) {
            ListenerSlot& slot = slots_[i];
            if (slot.isAllocated) {
                slot.isAllocated = false;
                slot.isPendingRemoval = false;
                ++slot.generation;
            }
        }
        size_ = 0;
        availableCount_ = capacity_;
        freeHead_ = 0;
        dispatchId_ = 0;
        dispatchDepth_ = 0;
        pendingRemovalCount_ = 0;

        for (size_t i = 0; i < capacity_; ++i) {
            slots_[i].nextFree = (i + 1 < capacity_) ? static_cast<uint32_t>(i + 1)
                                                     : GenerationalHandle::kInvalidIndex;
            slots_[i].thunk = nullptr;
            slots_[i].fnPtr = nullptr;
            slots_[i].userContext = nullptr;
            slots_[i].registeredInDispatchId = 0;
        }
    }

    /// @brief Total active registered listeners across all event types.
    [[nodiscard]] constexpr size_t size() const noexcept { return size_; }

    /// @brief Maximum listener capacity configured in backing storage.
    [[nodiscard]] constexpr size_t capacity() const noexcept { return capacity_; }

    /// @brief Remaining listener capacity.
    [[nodiscard]] constexpr size_t available() const noexcept { return availableCount_; }

    /// @brief Returns true if no listeners are registered.
    [[nodiscard]] constexpr bool isEmpty() const noexcept { return size_ == 0; }

    /// @brief Returns true if listener capacity is full.
    [[nodiscard]] constexpr bool isFull() const noexcept { return availableCount_ == 0; }

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

        const size_t usable = bufferSize - adjustment;
        if (usable < requiredBytes(requestedCapacity)) {
            return;
        }

        slots_ = static_cast<ListenerSlot*>(aligned);
        capacity_ = requestedCapacity;
        size_ = 0;
        availableCount_ = requestedCapacity;
        freeHead_ = 0;
        dispatchId_ = 0;
        dispatchDepth_ = 0;
        pendingRemovalCount_ = 0;

        for (size_t i = 0; i < capacity_; ++i) {
            ListenerSlot& slot = slots_[i];
            slot.generation = 1;
            slot.isAllocated = false;
            slot.isPendingRemoval = false;
            slot.registeredInDispatchId = 0;
            slot.nextFree = (i + 1 < capacity_) ? static_cast<uint32_t>(i + 1)
                                                : GenerationalHandle::kInvalidIndex;
        }
    }

    [[nodiscard]] ListenerToken registerRaw(EventTypeId eventTypeId, DispatchThunk thunk,
                                            void* fnPtr, void* userContext) noexcept {
        if (freeHead_ == GenerationalHandle::kInvalidIndex || availableCount_ == 0) {
            return ListenerToken::invalid();
        }

        const uint32_t slotIndex = freeHead_;
        ListenerSlot& slot = slots_[slotIndex];

        freeHead_ = slot.nextFree;
        slot.nextFree = GenerationalHandle::kInvalidIndex;
        slot.eventTypeId = eventTypeId.value();
        slot.thunk = thunk;
        slot.fnPtr = fnPtr;
        slot.userContext = userContext;
        slot.isAllocated = true;
        slot.isPendingRemoval = false;
        slot.registeredInDispatchId = (dispatchDepth_ > 0) ? dispatchId_ : 0;

        ++size_;
        --availableCount_;

        return ListenerToken{GenerationalHandle{slotIndex, slot.generation}.toBits()};
    }

    void purgePendingRemovals() noexcept {
        for (size_t i = 0; i < capacity_; ++i) {
            ListenerSlot& slot = slots_[i];
            if (slot.isPendingRemoval) {
                slot.isAllocated = false;
                slot.isPendingRemoval = false;
                slot.thunk = nullptr;
                slot.fnPtr = nullptr;
                slot.userContext = nullptr;
                slot.registeredInDispatchId = 0;
                slot.nextFree = freeHead_;
                freeHead_ = static_cast<uint32_t>(i);
                --size_;
                ++availableCount_;
            }
        }
        pendingRemovalCount_ = 0;
    }

    ListenerSlot* slots_{nullptr};
    size_t capacity_{0};
    size_t size_{0};
    size_t availableCount_{0};
    uint32_t freeHead_{GenerationalHandle::kInvalidIndex};
    uint64_t dispatchId_{0};
    uint32_t dispatchDepth_{0};
    size_t pendingRemovalCount_{0};
};

/// @class EventQueue
/// @brief Bounded FIFO queue for Event objects over caller-provided memory.
///
/// @tparam Event Plain data event type. Supports non-trivial constructors/destructors.
template <typename Event> class EventQueue {
public:
    static constexpr size_t kEventAlignment = std::max(alignof(Event), alignof(void*));
    static constexpr size_t kEventSize = sizeof(Event);

    /// @brief Returns the minimum alignment in bytes for backing memory.
    [[nodiscard]] static constexpr size_t requiredAlignment() noexcept { return kEventAlignment; }

    /// @brief Returns the byte size required to hold capacity events.
    [[nodiscard]] static constexpr size_t requiredBytes(size_t capacity) noexcept {
        return capacity * kEventSize;
    }

    /// @brief Constructs an uninitialized queue with zero capacity.
    constexpr EventQueue() noexcept = default;

    /// @brief Constructs an EventQueue over caller-provided buffer with explicit capacity.
    EventQueue(std::span<std::byte> backingMemory, size_t capacity) noexcept {
        initialize(backingMemory.data(), backingMemory.size(), capacity);
    }

    /// @brief Constructs an EventQueue over caller-provided buffer, calculating capacity.
    explicit EventQueue(std::span<std::byte> backingMemory) noexcept {
        if (!backingMemory.empty()) {
            void* aligned = alignUp(backingMemory.data(), kEventAlignment);
            if (aligned != nullptr) {
                const auto* alignedBytes = static_cast<const std::byte*>(aligned);
                const auto adjustment = static_cast<size_t>(alignedBytes - backingMemory.data());
                if (backingMemory.size() > adjustment) {
                    const size_t usable = backingMemory.size() - adjustment;
                    initialize(backingMemory.data(), backingMemory.size(), usable / kEventSize);
                }
            }
        }
    }

    /// @brief Constructs an EventQueue over raw pointer and buffer size with explicit capacity.
    EventQueue(std::byte* buffer, size_t bufferSize, size_t capacity) noexcept {
        initialize(buffer, bufferSize, capacity);
    }

    ~EventQueue() noexcept { clear(); }

    EventQueue(const EventQueue&) = delete;
    EventQueue& operator=(const EventQueue&) = delete;

    /// @brief Move constructor.
    EventQueue(EventQueue&& other) noexcept
        : buffer_(other.buffer_), capacity_(other.capacity_), head_(other.head_),
          tail_(other.tail_), count_(other.count_), droppedCount_(other.droppedCount_) {
        other.buffer_ = nullptr;
        other.capacity_ = 0;
        other.head_ = 0;
        other.tail_ = 0;
        other.count_ = 0;
        other.droppedCount_ = 0;
    }

    /// @brief Move assignment operator.
    EventQueue& operator=(EventQueue&& other) noexcept {
        if (this != &other) {
            clear();
            buffer_ = other.buffer_;
            capacity_ = other.capacity_;
            head_ = other.head_;
            tail_ = other.tail_;
            count_ = other.count_;
            droppedCount_ = other.droppedCount_;

            other.buffer_ = nullptr;
            other.capacity_ = 0;
            other.head_ = 0;
            other.tail_ = 0;
            other.count_ = 0;
            other.droppedCount_ = 0;
        }
        return *this;
    }

    /// @brief Constructs an Event in-place directly in the bounded queue.
    ///
    /// Preconditions : None.
    /// Postconditions: If capacity is available, event is constructed in FIFO slot.
    ///                 If queue is full, increments droppedCount and returns false.
    /// Performance   : O(1) in-place placement new. Zero allocations. Hot-path safe.
    ///
    /// @tparam Args Constructor argument types forwarded to Event.
    /// @param args Arguments forwarded to Event's constructor.
    /// @return True if enqueued; false if queue was full.
    template <typename... Args> bool emplace(Args&&... args) noexcept {
        if (count_ >= capacity_ || buffer_ == nullptr) {
            ++droppedCount_;
            return false;
        }

        void* dest = getSlotAddress(head_);
        ::new (dest) Event(std::forward<Args>(args)...);

        head_ = (head_ + 1) % capacity_;
        ++count_;
        return true;
    }

    /// @brief Pushes a copy of an event into the queue.
    bool push(const Event& event) noexcept { return emplace(event); }

    /// @brief Pushes a moved event into the queue.
    bool push(Event&& event) noexcept { return emplace(std::move(event)); }

    /// @brief Drains queued events directly to an EventDispatcher.
    ///
    /// @param dispatcher EventDispatcher to receive dispatched events.
    /// @return Number of events drained and dispatched.
    size_t drain(EventDispatcher& dispatcher) noexcept {
        return drain([&dispatcher](const Event& event) { dispatcher.dispatch(event); });
    }

    /// @brief Drains queued events in FIFO order by invoking a callable handler.
    ///
    /// Preconditions : None.
    /// Postconditions: Exactly the events present at the start of drain are processed.
    ///                 Events enqueued during drain execution are safely preserved.
    /// Performance   : O(M) where M is count(). Explicit in-place destruction.
    ///
    /// @tparam Handler Callable taking (const Event&).
    /// @param handler Invoked for each popped event.
    /// @return Number of events drained.
    template <typename Handler> size_t drain(Handler&& handler) noexcept {
        if (count_ == 0 || buffer_ == nullptr) {
            return 0;
        }

        const size_t eventsToDrain = count_;
        for (size_t i = 0; i < eventsToDrain; ++i) {
            void* src = getSlotAddress(tail_);
            Event* eventPtr = std::launder(reinterpret_cast<Event*>(src));

            handler(*eventPtr);

            std::destroy_at(eventPtr);
            tail_ = (tail_ + 1) % capacity_;
            --count_;
        }

        return eventsToDrain;
    }

    /// @brief Destroys all currently queued events and empties the queue.
    void clear() noexcept {
        if (buffer_ == nullptr || count_ == 0) {
            return;
        }
        while (count_ > 0) {
            void* src = getSlotAddress(tail_);
            Event* eventPtr = std::launder(reinterpret_cast<Event*>(src));
            std::destroy_at(eventPtr);
            tail_ = (tail_ + 1) % capacity_;
            --count_;
        }
        head_ = 0;
        tail_ = 0;
        count_ = 0;
    }

    /// @brief Peeks at the front of the queue without removing the event.
    [[nodiscard]] const Event* peek() const noexcept {
        if (count_ == 0 || buffer_ == nullptr) {
            return nullptr;
        }
        const void* src = getSlotAddress(tail_);
        return std::launder(reinterpret_cast<const Event*>(src));
    }

    /// @brief Number of events currently queued.
    [[nodiscard]] constexpr size_t size() const noexcept { return count_; }

    /// @brief Total event capacity of the queue.
    [[nodiscard]] constexpr size_t capacity() const noexcept { return capacity_; }

    /// @brief Number of events dropped due to buffer exhaustion.
    [[nodiscard]] constexpr size_t droppedCount() const noexcept { return droppedCount_; }

    /// @brief Resets the dropped count to zero.
    void resetDroppedCount() noexcept { droppedCount_ = 0; }

    /// @brief Returns true if queue has no events.
    [[nodiscard]] constexpr bool isEmpty() const noexcept { return count_ == 0; }

    /// @brief Returns true if queue is at maximum capacity.
    [[nodiscard]] constexpr bool isFull() const noexcept { return count_ == capacity_; }

private:
    void initialize(std::byte* buffer, size_t bufferSize, size_t requestedCapacity) noexcept {
        if (buffer == nullptr || bufferSize == 0 || requestedCapacity == 0) {
            return;
        }

        void* aligned = alignUp(buffer, kEventAlignment);
        if (aligned == nullptr) {
            return;
        }

        const auto* alignedBytes = static_cast<const std::byte*>(aligned);
        const auto adjustment = static_cast<size_t>(alignedBytes - buffer);
        if (bufferSize <= adjustment) {
            return;
        }

        const size_t usable = bufferSize - adjustment;
        if (usable < requiredBytes(requestedCapacity)) {
            return;
        }

        buffer_ = static_cast<std::byte*>(aligned);
        capacity_ = requestedCapacity;
        head_ = 0;
        tail_ = 0;
        count_ = 0;
        droppedCount_ = 0;
    }

    [[nodiscard]] void* getSlotAddress(size_t index) noexcept {
        return static_cast<void*>(buffer_ + (index * kEventSize));
    }

    [[nodiscard]] const void* getSlotAddress(size_t index) const noexcept {
        return static_cast<const void*>(buffer_ + (index * kEventSize));
    }

    std::byte* buffer_{nullptr};
    size_t capacity_{0};
    size_t head_{0};
    size_t tail_{0};
    size_t count_{0};
    size_t droppedCount_{0};
};

} // namespace eng
