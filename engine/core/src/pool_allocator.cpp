/// @file pool_allocator.cpp
/// @brief PoolAllocator implementation.

#include <algorithm>
#include <core/pool_allocator.h>

namespace eng {

PoolAllocator::PoolAllocator(std::span<std::byte> backingMemory, size_t blockSize,
                             size_t alignment) noexcept
    : PoolAllocator(backingMemory.data(), backingMemory.size(), blockSize, alignment) {}

PoolAllocator::PoolAllocator(std::byte* buffer, size_t capacity, size_t blockSize,
                             size_t alignment) noexcept
    : buffer_(buffer), capacityBytes_(capacity), blockSize_(blockSize) {
    if (buffer_ == nullptr || capacityBytes_ == 0 || blockSize_ == 0 || alignment == 0 ||
        !isPowerOfTwo(alignment)) {
        return;
    }

    alignment_ = std::max(alignment, kMinAlignment);
    const size_t rawBlockSize = std::max(blockSize_, kMinBlockSize);
    effectiveBlockSize_ = alignUp(rawBlockSize, alignment_);

    if (effectiveBlockSize_ == 0) {
        return; // Overflow protection
    }

    firstBlock_ = static_cast<std::byte*>(alignUp(static_cast<void*>(buffer_), alignment_));
    if (firstBlock_ == nullptr) {
        return;
    }

    const auto padding = static_cast<size_t>(firstBlock_ - buffer_);
    if (padding > capacityBytes_) {
        firstBlock_ = nullptr;
        return;
    }

    const size_t remainingCapacity = capacityBytes_ - padding;
    totalBlocks_ = remainingCapacity / effectiveBlockSize_;

    if (totalBlocks_ == 0) {
        firstBlock_ = nullptr;
        return;
    }

    initializeFreeList();
}

PoolAllocator::PoolAllocator(PoolAllocator&& other) noexcept
    : buffer_(other.buffer_), firstBlock_(other.firstBlock_), capacityBytes_(other.capacityBytes_),
      blockSize_(other.blockSize_), effectiveBlockSize_(other.effectiveBlockSize_),
      alignment_(other.alignment_), totalBlocks_(other.totalBlocks_),
      allocatedCount_(other.allocatedCount_), head_(other.head_) {
    other.buffer_ = nullptr;
    other.firstBlock_ = nullptr;
    other.capacityBytes_ = 0;
    other.blockSize_ = 0;
    other.effectiveBlockSize_ = 0;
    other.alignment_ = 0;
    other.totalBlocks_ = 0;
    other.allocatedCount_ = 0;
    other.head_ = nullptr;
}

PoolAllocator& PoolAllocator::operator=(PoolAllocator&& other) noexcept {
    if (this != &other) {
        buffer_ = other.buffer_;
        firstBlock_ = other.firstBlock_;
        capacityBytes_ = other.capacityBytes_;
        blockSize_ = other.blockSize_;
        effectiveBlockSize_ = other.effectiveBlockSize_;
        alignment_ = other.alignment_;
        totalBlocks_ = other.totalBlocks_;
        allocatedCount_ = other.allocatedCount_;
        head_ = other.head_;

        other.buffer_ = nullptr;
        other.firstBlock_ = nullptr;
        other.capacityBytes_ = 0;
        other.blockSize_ = 0;
        other.effectiveBlockSize_ = 0;
        other.alignment_ = 0;
        other.totalBlocks_ = 0;
        other.allocatedCount_ = 0;
        other.head_ = nullptr;
    }
    return *this;
}

void* PoolAllocator::allocate() noexcept {
    if (head_ == nullptr) {
        return nullptr; // Pool exhausted or zero capacity
    }

    FreeNode* const node = head_;
    head_ = node->next;
    node->next = nullptr;
    node->canary = 0; // Clear canary for active allocation
    ++allocatedCount_;

    return static_cast<void*>(node);
}

bool PoolAllocator::deallocate(void* ptr) noexcept {
    if (ptr == nullptr || firstBlock_ == nullptr || totalBlocks_ == 0) {
        return false;
    }

    auto* const bytePtr = static_cast<std::byte*>(ptr);
    const size_t totalPoolBytes = totalBlocks_ * effectiveBlockSize_;

    // 1. Range check: pointer must fall within the aligned block array
    if (bytePtr < firstBlock_ || bytePtr >= firstBlock_ + totalPoolBytes) {
        return false;
    }

    // 2. Alignment check: pointer must be at the exact beginning of a block
    const auto offset = static_cast<size_t>(bytePtr - firstBlock_);
    if (offset % effectiveBlockSize_ != 0) {
        return false;
    }

    // 3. Double-free check: block must not already be marked free
    auto* const node = reinterpret_cast<FreeNode*>(bytePtr);
    if (node->canary == kFreeCanary || node == head_) {
        return false;
    }

    // 4. Return block to head of free list
    node->next = head_;
    node->canary = kFreeCanary;
    head_ = node;
    --allocatedCount_;

    return true;
}

void PoolAllocator::reset() noexcept {
    if (totalBlocks_ == 0 || firstBlock_ == nullptr) {
        return;
    }

    head_ = nullptr;
    allocatedCount_ = 0;
    initializeFreeList();
}

void PoolAllocator::initializeFreeList() noexcept {
    head_ = reinterpret_cast<FreeNode*>(firstBlock_);
    for (size_t i = 0; i < totalBlocks_; ++i) {
        auto* const curr = reinterpret_cast<FreeNode*>(firstBlock_ + (i * effectiveBlockSize_));
        auto* const next =
            (i + 1 < totalBlocks_)
                ? reinterpret_cast<FreeNode*>(firstBlock_ + ((i + 1) * effectiveBlockSize_))
                : nullptr;
        curr->next = next;
        curr->canary = kFreeCanary;
    }
}

} // namespace eng
