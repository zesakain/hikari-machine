/// @file null_device.cpp
/// @brief Headless mock RHI device implementation.

#include "null_device.h"

#include "core/assert.h"
#include "null_command_list.h"

#include <cstring>

namespace eng::rhi {

NullDevice::NullDevice() noexcept {
    caps_.tier = HardwareTier::Tier0;
    caps_.deviceName = "Null Mock Headless Device";
    caps_.limits.maxTextureDimension1D = 4096;
    caps_.limits.maxTextureDimension2D = 4096;
    caps_.limits.maxTextureDimension3D = 2048;
    caps_.limits.maxTextureArrayLayers = 256;
    caps_.limits.maxUniformBufferBindingSize = 65536;
    caps_.limits.maxStorageBufferBindingSize = 128 * 1024 * 1024;
    caps_.limits.maxBoundDescriptorSets = 4;
    caps_.limits.maxColorAttachments = 8;
    caps_.features.dynamicRendering = true;
    caps_.features.timelineSemaphores = true;
    caps_.features.descriptorIndexing = false;
    caps_.features.rayTracing = false;
    caps_.features.meshShaders = false;

    buffers_.reserve(128);
    textures_.reserve(128);
    samplers_.reserve(64);
}

const GpuCaps& NullDevice::getCaps() const noexcept {
    return caps_;
}

Result<BufferHandle> NullDevice::createBuffer(const BufferDesc& desc) {
    if (desc.sizeInBytes == 0) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Buffer sizeInBytes must be > 0"};
    }
    if (desc.sizeInBytes > caps_.limits.maxStorageBufferBindingSize) {
        return Error{.code = ErrorCode::ResourceCreationFailed,
                     .message = "Buffer size exceeds maxStorageBufferBindingSize"};
    }

    auto slotIndex = static_cast<uint32_t>(buffers_.size());
    for (size_t i = 0; i < buffers_.size(); ++i) {
        if (!buffers_[i].alive) {
            slotIndex = static_cast<uint32_t>(i);
            break;
        }
    }

    if (slotIndex == buffers_.size()) {
        BufferSlot newSlot{};
        newSlot.desc = desc;
        newSlot.storage.resize(desc.sizeInBytes, 0);
        newSlot.generation = 1;
        newSlot.alive = true;
        buffers_.push_back(std::move(newSlot));
    } else {
        auto& slot = buffers_[slotIndex];
        slot.desc = desc;
        slot.storage.assign(desc.sizeInBytes, 0);
        slot.alive = true;
    }

    const auto handle = BufferHandle(GenerationalHandle{slotIndex, buffers_[slotIndex].generation});
    return Result<BufferHandle>::success(handle);
}

void NullDevice::destroyBuffer(BufferHandle handle) {
    auto* slot = resolveBuffer(handle);
    if (slot != nullptr) {
        slot->alive = false;
        slot->generation++;
        slot->storage.clear();
        slot->storage.shrink_to_fit();
    }
}

Result<TextureHandle> NullDevice::createTexture(const TextureDesc& desc) {
    if (desc.width == 0 || desc.height == 0 || desc.depth == 0 || desc.mipLevels == 0 ||
        desc.arrayLayers == 0) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Texture dimensions and mips must be non-zero"};
    }
    if (desc.width > caps_.limits.maxTextureDimension2D ||
        desc.height > caps_.limits.maxTextureDimension2D) {
        return Error{.code = ErrorCode::ResourceCreationFailed,
                     .message = "Texture dimensions exceed maxTextureDimension2D"};
    }
    if (desc.arrayLayers > caps_.limits.maxTextureArrayLayers) {
        return Error{.code = ErrorCode::ResourceCreationFailed,
                     .message = "Texture arrayLayers exceed limit"};
    }

    auto slotIndex = static_cast<uint32_t>(textures_.size());
    for (size_t i = 0; i < textures_.size(); ++i) {
        if (!textures_[i].alive) {
            slotIndex = static_cast<uint32_t>(i);
            break;
        }
    }

    if (slotIndex == textures_.size()) {
        TextureSlot newSlot{};
        newSlot.desc = desc;
        newSlot.generation = 1;
        newSlot.alive = true;
        textures_.push_back(newSlot);
    } else {
        auto& slot = textures_[slotIndex];
        slot.desc = desc;
        slot.alive = true;
    }

    const auto handle =
        TextureHandle(GenerationalHandle{slotIndex, textures_[slotIndex].generation});
    return Result<TextureHandle>::success(handle);
}

void NullDevice::destroyTexture(TextureHandle handle) {
    if (handle.index() < textures_.size()) {
        auto& slot = textures_[handle.index()];
        if (slot.alive && slot.generation == handle.generation()) {
            slot.alive = false;
            slot.generation++;
        }
    }
}

Result<SamplerHandle> NullDevice::createSampler(const SamplerDesc& desc) {
    auto slotIndex = static_cast<uint32_t>(samplers_.size());
    for (size_t i = 0; i < samplers_.size(); ++i) {
        if (!samplers_[i].alive) {
            slotIndex = static_cast<uint32_t>(i);
            break;
        }
    }

    if (slotIndex == samplers_.size()) {
        SamplerSlot newSlot{};
        newSlot.desc = desc;
        newSlot.generation = 1;
        newSlot.alive = true;
        samplers_.push_back(newSlot);
    } else {
        auto& slot = samplers_[slotIndex];
        slot.desc = desc;
        slot.alive = true;
    }

    const auto handle =
        SamplerHandle(GenerationalHandle{slotIndex, samplers_[slotIndex].generation});
    return Result<SamplerHandle>::success(handle);
}

void NullDevice::destroySampler(SamplerHandle handle) {
    if (handle.index() < samplers_.size()) {
        auto& slot = samplers_[handle.index()];
        if (slot.alive && slot.generation == handle.generation()) {
            slot.alive = false;
            slot.generation++;
        }
    }
}

Result<std::unique_ptr<RhiCommandList>> NullDevice::createCommandList() {
    return std::unique_ptr<RhiCommandList>(std::make_unique<NullCommandList>(*this));
}

Result<void> NullDevice::submit(RhiCommandList* commandList) {
    if (commandList == nullptr) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Cannot submit null command list"};
    }

    auto* nullCmd = static_cast<NullCommandList*>(commandList);
    if (nullCmd->getState() != NullCommandList::State::Ended) {
        return Error{.code = ErrorCode::CommandSubmissionFailed,
                     .message = "Command list must be in Ended state prior to submission"};
    }

    // Apply inline buffer updates
    for (const auto& update : nullCmd->getUpdateCommands()) {
        auto* slot = resolveBuffer(update.dst);
        if (slot == nullptr) {
            return Error{.code = ErrorCode::CommandSubmissionFailed,
                         .message = "Destination buffer in updateBuffer is invalid or stale"};
        }
        if (update.dstOffset + update.data.size() > slot->desc.sizeInBytes) {
            return Error{.code = ErrorCode::OutOfBounds,
                         .message = "updateBuffer out of buffer bounds"};
        }
        if (!update.data.empty()) {
            std::memcpy(slot->storage.data() + update.dstOffset, update.data.data(),
                        update.data.size());
        }
    }

    // Apply buffer copies
    for (const auto& copy : nullCmd->getCopyCommands()) {
        auto* srcSlot = resolveBuffer(copy.src);
        auto* dstSlot = resolveBuffer(copy.dst);
        if (srcSlot == nullptr || dstSlot == nullptr) {
            return Error{.code = ErrorCode::CommandSubmissionFailed,
                         .message =
                             "Source or destination buffer in copyBuffer is invalid or stale"};
        }
        if (copy.srcOffset + copy.size > srcSlot->desc.sizeInBytes ||
            copy.dstOffset + copy.size > dstSlot->desc.sizeInBytes) {
            return Error{.code = ErrorCode::OutOfBounds,
                         .message = "copyBuffer out of buffer bounds"};
        }
        if (copy.size > 0) {
            std::memcpy(dstSlot->storage.data() + copy.dstOffset,
                        srcSlot->storage.data() + copy.srcOffset, copy.size);
        }
    }

    nullCmd->setState(NullCommandList::State::Submitted);
    ++submittedCount_;
    return Result<void>::success();
}

void NullDevice::waitIdle() {
    // Null mock execution is synchronous on the calling CPU thread
}

bool NullDevice::isBufferValid(BufferHandle handle) const noexcept {
    return resolveBuffer(handle) != nullptr;
}

bool NullDevice::isTextureValid(TextureHandle handle) const noexcept {
    if (handle.index() < textures_.size()) {
        const auto& slot = textures_[handle.index()];
        return slot.alive && slot.generation == handle.generation();
    }
    return false;
}

bool NullDevice::isSamplerValid(SamplerHandle handle) const noexcept {
    if (handle.index() < samplers_.size()) {
        const auto& slot = samplers_[handle.index()];
        return slot.alive && slot.generation == handle.generation();
    }
    return false;
}

const uint8_t* NullDevice::getBufferData(BufferHandle handle) const noexcept {
    const auto* slot = resolveBuffer(handle);
    return slot != nullptr ? slot->storage.data() : nullptr;
}

size_t NullDevice::getBufferSize(BufferHandle handle) const noexcept {
    const auto* slot = resolveBuffer(handle);
    return slot != nullptr ? slot->desc.sizeInBytes : 0;
}

NullDevice::BufferSlot* NullDevice::resolveBuffer(BufferHandle handle) noexcept {
    if (handle.index() < buffers_.size()) {
        auto& slot = buffers_[handle.index()];
        if (slot.alive && slot.generation == handle.generation()) {
            return &slot;
        }
    }
    return nullptr;
}

const NullDevice::BufferSlot* NullDevice::resolveBuffer(BufferHandle handle) const noexcept {
    if (handle.index() < buffers_.size()) {
        const auto& slot = buffers_[handle.index()];
        if (slot.alive && slot.generation == handle.generation()) {
            return &slot;
        }
    }
    return nullptr;
}

Result<std::unique_ptr<RhiDevice>> createNullDevice() {
    return std::unique_ptr<RhiDevice>(std::make_unique<NullDevice>());
}

} // namespace eng::rhi
