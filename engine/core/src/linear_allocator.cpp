/// @file linear_allocator.cpp
/// @brief LinearAllocator implementation.

#include <core/linear_allocator.h>

namespace eng {

LinearAllocator::LinearAllocator(std::span<std::byte> backingMemory) noexcept
    : buffer_(backingMemory.data()), capacity_(backingMemory.size()) {}

LinearAllocator::LinearAllocator(std::byte* buffer, size_t capacity) noexcept
    : buffer_(buffer), capacity_(capacity) {
    const bool isValidBuffer = (capacity == 0) ? true : (buffer != nullptr);
    ENG_ASSERT(isValidBuffer, "Non-null buffer required when capacity is non-zero");
}

LinearAllocator::LinearAllocator(LinearAllocator&& other) noexcept
    : buffer_(other.buffer_), capacity_(other.capacity_), offset_(other.offset_) {
    other.buffer_ = nullptr;
    other.capacity_ = 0;
    other.offset_ = 0;
}

LinearAllocator& LinearAllocator::operator=(LinearAllocator&& other) noexcept {
    if (this != &other) {
        buffer_ = other.buffer_;
        capacity_ = other.capacity_;
        offset_ = other.offset_;
        other.buffer_ = nullptr;
        other.capacity_ = 0;
        other.offset_ = 0;
    }
    return *this;
}

void* LinearAllocator::allocate(size_t size, size_t alignment) noexcept {
    if (size == 0 || alignment == 0 || !isPowerOfTwo(alignment) || buffer_ == nullptr ||
        offset_ >= capacity_) {
        return nullptr;
    }

    const auto currentAddress = reinterpret_cast<uintptr_t>(buffer_ + offset_);
    const auto misalignment =
        static_cast<size_t>(currentAddress & static_cast<uintptr_t>(alignment - 1));
    const size_t padding = (misalignment == 0) ? 0 : (alignment - misalignment);

    if (capacity_ - offset_ < padding) {
        return nullptr;
    }
    const size_t remainingAfterPadding = capacity_ - offset_ - padding;
    if (remainingAfterPadding < size) {
        return nullptr; // Capacity exhausted
    }

    std::byte* const alignedPtr = buffer_ + offset_ + padding;
    offset_ += padding + size;
    return alignedPtr;
}

void LinearAllocator::reset() noexcept {
    offset_ = 0;
}

} // namespace eng
