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
    if (padding >= capacityBytes_) {
        firstBlock_ = nullptr;
        return;
    }

    const size_t usableCapacity = capacityBytes_ - padding;
    if (usableCapacity < effectiveBlockSize_) {
        firstBlock_ = nullptr;
        return;
    }

    // Calculate maximum blocks fitting alongside the trailing bitset storage
    size_t count = usableCapacity / effectiveBlockSize_;
    while (count > 0) {
        const size_t words = (count + 63) / 64;
        const size_t bitsetBytes = words * sizeof(uint64_t);
        if ((count * effectiveBlockSize_) + bitsetBytes <= usableCapacity) {
            break;
        }
        --count;
    }

    if (count == 0) {
        firstBlock_ = nullptr;
        return;
    }

    totalBlocks_ = count;
    bitsetWordCount_ = (totalBlocks_ + 63) / 64;
    bitset_ = reinterpret_cast<uint64_t*>(firstBlock_ + (totalBlocks_ * effectiveBlockSize_));

    initializeFreeList();
}

PoolAllocator::PoolAllocator(PoolAllocator&& other) noexcept
    : buffer_(other.buffer_), firstBlock_(other.firstBlock_), bitset_(other.bitset_),
      bitsetWordCount_(other.bitsetWordCount_), capacityBytes_(other.capacityBytes_),
      blockSize_(other.blockSize_), effectiveBlockSize_(other.effectiveBlockSize_),
      alignment_(other.alignment_), totalBlocks_(other.totalBlocks_),
      allocatedCount_(other.allocatedCount_), head_(other.head_) {
    other.buffer_ = nullptr;
    other.firstBlock_ = nullptr;
    other.bitset_ = nullptr;
    other.bitsetWordCount_ = 0;
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
        bitset_ = other.bitset_;
        bitsetWordCount_ = other.bitsetWordCount_;
        capacityBytes_ = other.capacityBytes_;
        blockSize_ = other.blockSize_;
        effectiveBlockSize_ = other.effectiveBlockSize_;
        alignment_ = other.alignment_;
        totalBlocks_ = other.totalBlocks_;
        allocatedCount_ = other.allocatedCount_;
        head_ = other.head_;

        other.buffer_ = nullptr;
        other.firstBlock_ = nullptr;
        other.bitset_ = nullptr;
        other.bitsetWordCount_ = 0;
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

    const auto offset = static_cast<size_t>(reinterpret_cast<uintptr_t>(node) -
                                            reinterpret_cast<uintptr_t>(firstBlock_));
    const size_t blockIndex = offset / effectiveBlockSize_;
    bitset_[blockIndex / 64] |= (1ULL << (blockIndex % 64));
    ++allocatedCount_;

    return static_cast<void*>(node);
}

bool PoolAllocator::deallocate(void* ptr) noexcept {
    if (ptr == nullptr || firstBlock_ == nullptr || totalBlocks_ == 0) {
        return false;
    }

    const auto addr = reinterpret_cast<uintptr_t>(ptr);
    const auto startAddr = reinterpret_cast<uintptr_t>(firstBlock_);

    // 1. Foreign pointer check (address before first aligned block)
    if (addr < startAddr) {
        return false;
    }

    const uintptr_t offset = addr - startAddr;
    const size_t totalPoolBytes = totalBlocks_ * effectiveBlockSize_;

    // 2. Foreign pointer check (address beyond block array)
    if (offset >= totalPoolBytes) {
        return false;
    }

    // 3. Block boundary check: pointer must be at the exact beginning of a block
    if (offset % effectiveBlockSize_ != 0) {
        return false;
    }

    // 4. Double-deallocation check via bitset
    const auto blockIndex = static_cast<size_t>(offset / effectiveBlockSize_);
    const size_t wordIndex = blockIndex / 64;
    const uint64_t mask = 1ULL << (blockIndex % 64);

    if ((bitset_[wordIndex] & mask) == 0) {
        return false; // Block is already free; double-deallocation rejected!
    }

    // 5. Mark block free and return to head of intrusive free list
    bitset_[wordIndex] &= ~mask;
    auto* const node = static_cast<FreeNode*>(ptr);
    node->next = head_;
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
    }
    std::fill_n(bitset_, bitsetWordCount_, 0ULL);
}

} // namespace eng
