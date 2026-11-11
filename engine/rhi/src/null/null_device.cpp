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

Result<void> NullDevice::writeBuffer(BufferHandle handle, size_t offset, const void* data,
                                     size_t size) {
    if (data == nullptr && size > 0) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Null data pointer in writeBuffer"};
    }
    auto* slot = resolveBuffer(handle);
    if (slot == nullptr) {
        return Error{.code = ErrorCode::HandleResolutionFailed,
                     .message = "Invalid buffer handle in writeBuffer"};
    }
    if (offset + size > slot->storage.size()) {
        return Error{.code = ErrorCode::OutOfBounds,
                     .message = "Buffer write exceeds allocated storage bounds"};
    }
    if (size > 0) {
        std::memcpy(slot->storage.data() + offset, data, size);
    }
    return Result<void>::success();
}

Result<void> NullDevice::readBuffer(BufferHandle handle, size_t offset, void* outData,
                                    size_t size) {
    if (outData == nullptr && size > 0) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Null destination pointer in readBuffer"};
    }
    auto* slot = resolveBuffer(handle);
    if (slot == nullptr) {
        return Error{.code = ErrorCode::HandleResolutionFailed,
                     .message = "Invalid buffer handle in readBuffer"};
    }
    if (offset + size > slot->storage.size()) {
        return Error{.code = ErrorCode::OutOfBounds,
                     .message = "Buffer read exceeds allocated storage bounds"};
    }
    if (size > 0) {
        std::memcpy(outData, slot->storage.data() + offset, size);
    }
    return Result<void>::success();
}

Result<TextureHandle> NullDevice::createTexture(const TextureDesc& desc) {
    if (desc.format == TextureFormat::Undefined) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Texture format cannot be Undefined"};
    }
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
    if (desc.minLod > desc.maxLod) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Sampler minLod cannot be greater than maxLod"};
    }
    if (desc.anisotropyEnable && desc.maxAnisotropy < 1.0F) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Sampler maxAnisotropy must be at least 1.0"};
    }

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

Result<ShaderHandle> NullDevice::createShader(const ShaderDesc& desc) {
    auto valRes = validateShaderDesc(desc);
    if (!valRes.hasValue()) {
        return valRes.error();
    }

    auto slotIndex = static_cast<uint32_t>(shaders_.size());
    for (size_t i = 0; i < shaders_.size(); ++i) {
        if (!shaders_[i].alive) {
            slotIndex = static_cast<uint32_t>(i);
            break;
        }
    }

    if (slotIndex == shaders_.size()) {
        ShaderSlot newSlot{};
        newSlot.desc = desc;
        newSlot.code.assign(desc.spirvCode.begin(), desc.spirvCode.end());
        newSlot.desc.spirvCode = newSlot.code;
        newSlot.generation = 1;
        newSlot.alive = true;
        shaders_.push_back(std::move(newSlot));
    } else {
        auto& slot = shaders_[slotIndex];
        slot.desc = desc;
        slot.code.assign(desc.spirvCode.begin(), desc.spirvCode.end());
        slot.desc.spirvCode = slot.code;
        slot.alive = true;
    }

    return ShaderHandle(GenerationalHandle{slotIndex, shaders_[slotIndex].generation});
}

void NullDevice::destroyShader(ShaderHandle handle) {
    if (isShaderValid(handle)) {
        auto& slot = shaders_[handle.index()];
        slot.alive = false;
        slot.code.clear();
        ++slot.generation;
    }
}

Result<PipelineLayoutHandle> NullDevice::createPipelineLayout(const PipelineLayoutDesc& desc) {
    auto valRes = validatePipelineLayoutDesc(desc);
    if (!valRes.hasValue()) {
        return valRes.error();
    }

    for (const auto& layoutHandle : desc.descriptorSetLayoutHandles) {
        if (!isDescriptorSetLayoutValid(layoutHandle)) {
            return Error{.code = ErrorCode::HandleResolutionFailed,
                         .message =
                             "Invalid descriptor set layout handle in pipeline layout creation"};
        }
    }

    // Validate descriptor set bindings
    for (const auto& setDesc : desc.descriptorSetLayouts) {
        for (size_t i = 0; i < setDesc.bindings.size(); ++i) {
            if (setDesc.bindings[i].descriptorCount == 0) {
                return Error{.code = ErrorCode::InvalidArgument,
                             .message = "Descriptor binding count cannot be 0"};
            }
            for (size_t j = i + 1; j < setDesc.bindings.size(); ++j) {
                if (setDesc.bindings[i].binding == setDesc.bindings[j].binding) {
                    return Error{.code = ErrorCode::InvalidArgument,
                                 .message = "Duplicate binding index within descriptor set layout"};
                }
            }
        }
    }

    auto slotIndex = static_cast<uint32_t>(pipelineLayouts_.size());
    for (size_t i = 0; i < pipelineLayouts_.size(); ++i) {
        if (!pipelineLayouts_[i].alive) {
            slotIndex = static_cast<uint32_t>(i);
            break;
        }
    }

    if (slotIndex == pipelineLayouts_.size()) {
        PipelineLayoutSlot newSlot{};
        newSlot.desc = desc;
        newSlot.generation = 1;
        newSlot.alive = true;
        pipelineLayouts_.push_back(std::move(newSlot));
    } else {
        auto& slot = pipelineLayouts_[slotIndex];
        slot.desc = desc;
        slot.alive = true;
    }

    return PipelineLayoutHandle(
        GenerationalHandle{slotIndex, pipelineLayouts_[slotIndex].generation});
}

void NullDevice::destroyPipelineLayout(PipelineLayoutHandle handle) {
    if (isPipelineLayoutValid(handle)) {
        auto& slot = pipelineLayouts_[handle.index()];
        slot.alive = false;
        ++slot.generation;
    }
}

Result<PipelineHandle> NullDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    auto valRes = validateGraphicsPipelineDesc(desc);
    if (!valRes.hasValue()) {
        return valRes.error();
    }

    if (!isShaderValid(desc.vertexShader)) {
        return Error{.code = ErrorCode::HandleResolutionFailed,
                     .message = "Invalid vertex shader handle in graphics pipeline creation"};
    }
    const auto* vsDesc = getShaderDesc(desc.vertexShader);
    if (vsDesc == nullptr || !hasFlag(vsDesc->stage, ShaderStageFlags::Vertex)) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Shader bound to vertex stage does not have Vertex stage flag"};
    }

    if (desc.fragmentShader.isValid()) {
        if (!isShaderValid(desc.fragmentShader)) {
            return Error{.code = ErrorCode::HandleResolutionFailed,
                         .message = "Invalid fragment shader handle in graphics pipeline creation"};
        }
        const auto* fsDesc = getShaderDesc(desc.fragmentShader);
        if (fsDesc == nullptr || !hasFlag(fsDesc->stage, ShaderStageFlags::Fragment)) {
            return Error{.code = ErrorCode::InvalidArgument,
                         .message =
                             "Shader bound to fragment stage does not have Fragment stage flag"};
        }
    }

    if (!isPipelineLayoutValid(desc.layout)) {
        return Error{.code = ErrorCode::HandleResolutionFailed,
                     .message = "Invalid pipeline layout handle in graphics pipeline creation"};
    }

    auto slotIndex = static_cast<uint32_t>(pipelines_.size());
    for (size_t i = 0; i < pipelines_.size(); ++i) {
        if (!pipelines_[i].alive) {
            slotIndex = static_cast<uint32_t>(i);
            break;
        }
    }

    if (slotIndex == pipelines_.size()) {
        PipelineSlot newSlot{};
        newSlot.desc = desc;
        newSlot.generation = 1;
        newSlot.alive = true;
        pipelines_.push_back(std::move(newSlot));
    } else {
        auto& slot = pipelines_[slotIndex];
        slot.desc = desc;
        slot.alive = true;
    }

    return PipelineHandle(GenerationalHandle{slotIndex, pipelines_[slotIndex].generation});
}

void NullDevice::destroyGraphicsPipeline(PipelineHandle handle) {
    if (isPipelineValid(handle)) {
        auto& slot = pipelines_[handle.index()];
        slot.alive = false;
        ++slot.generation;
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

bool NullDevice::isShaderValid(ShaderHandle handle) const noexcept {
    if (handle.index() < shaders_.size()) {
        const auto& slot = shaders_[handle.index()];
        return slot.alive && slot.generation == handle.generation();
    }
    return false;
}

bool NullDevice::isPipelineLayoutValid(PipelineLayoutHandle handle) const noexcept {
    if (handle.index() < pipelineLayouts_.size()) {
        const auto& slot = pipelineLayouts_[handle.index()];
        return slot.alive && slot.generation == handle.generation();
    }
    return false;
}

bool NullDevice::isPipelineValid(PipelineHandle handle) const noexcept {
    if (handle.index() < pipelines_.size()) {
        const auto& slot = pipelines_[handle.index()];
        return slot.alive && slot.generation == handle.generation();
    }
    return false;
}

const ShaderDesc* NullDevice::getShaderDesc(ShaderHandle handle) const noexcept {
    if (isShaderValid(handle)) {
        return &shaders_[handle.index()].desc;
    }
    return nullptr;
}

const PipelineLayoutDesc*
NullDevice::getPipelineLayoutDesc(PipelineLayoutHandle handle) const noexcept {
    if (isPipelineLayoutValid(handle)) {
        return &pipelineLayouts_[handle.index()].desc;
    }
    return nullptr;
}

const GraphicsPipelineDesc* NullDevice::getPipelineDesc(PipelineHandle handle) const noexcept {
    if (isPipelineValid(handle)) {
        return &pipelines_[handle.index()].desc;
    }
    return nullptr;
}

const TextureDesc* NullDevice::getTextureDesc(TextureHandle handle) const noexcept {
    if (isTextureValid(handle)) {
        return &textures_[handle.index()].desc;
    }
    return nullptr;
}

const uint8_t* NullDevice::getBufferData(BufferHandle handle) const noexcept {
    const auto* slot = resolveBuffer(handle);
    return slot != nullptr ? slot->storage.data() : nullptr;
}

size_t NullDevice::getBufferSize(BufferHandle handle) const noexcept {
    const auto* slot = resolveBuffer(handle);
    return slot != nullptr ? slot->desc.sizeInBytes : 0;
}

Result<DescriptorSetLayoutHandle>
NullDevice::createDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) {
    auto valRes = validateDescriptorSetLayoutDesc(desc);
    if (!valRes.hasValue()) {
        return valRes.error();
    }

    auto slotIndex = static_cast<uint32_t>(descriptorSetLayouts_.size());
    for (size_t i = 0; i < descriptorSetLayouts_.size(); ++i) {
        if (!descriptorSetLayouts_[i].alive) {
            slotIndex = static_cast<uint32_t>(i);
            break;
        }
    }

    if (slotIndex == descriptorSetLayouts_.size()) {
        DescriptorSetLayoutSlot newSlot{};
        newSlot.desc = desc;
        newSlot.generation = 1;
        newSlot.alive = true;
        descriptorSetLayouts_.push_back(std::move(newSlot));
    } else {
        auto& slot = descriptorSetLayouts_[slotIndex];
        slot.desc = desc;
        slot.alive = true;
    }

    return DescriptorSetLayoutHandle(
        GenerationalHandle{slotIndex, descriptorSetLayouts_[slotIndex].generation});
}

void NullDevice::destroyDescriptorSetLayout(DescriptorSetLayoutHandle handle) {
    if (isDescriptorSetLayoutValid(handle)) {
        auto& slot = descriptorSetLayouts_[handle.index()];
        slot.alive = false;
        ++slot.generation;
    }
}

Result<DescriptorPoolHandle> NullDevice::createDescriptorPool(const DescriptorPoolDesc& desc) {
    auto valRes = validateDescriptorPoolDesc(desc);
    if (!valRes.hasValue()) {
        return valRes.error();
    }

    auto slotIndex = static_cast<uint32_t>(descriptorPools_.size());
    for (size_t i = 0; i < descriptorPools_.size(); ++i) {
        if (!descriptorPools_[i].alive) {
            slotIndex = static_cast<uint32_t>(i);
            break;
        }
    }

    if (slotIndex == descriptorPools_.size()) {
        DescriptorPoolSlot newSlot{};
        newSlot.desc = desc;
        newSlot.allocatedCount = 0;
        newSlot.generation = 1;
        newSlot.alive = true;
        descriptorPools_.push_back(std::move(newSlot));
    } else {
        auto& slot = descriptorPools_[slotIndex];
        slot.desc = desc;
        slot.allocatedCount = 0;
        slot.alive = true;
    }

    return DescriptorPoolHandle(
        GenerationalHandle{slotIndex, descriptorPools_[slotIndex].generation});
}

void NullDevice::destroyDescriptorPool(DescriptorPoolHandle handle) {
    if (isDescriptorPoolValid(handle)) {
        auto& poolSlot = descriptorPools_[handle.index()];
        poolSlot.alive = false;
        ++poolSlot.generation;

        for (auto& setSlot : descriptorSets_) {
            if (setSlot.alive && setSlot.pool == handle) {
                setSlot.alive = false;
                ++setSlot.generation;
            }
        }
    }
}

Result<void> NullDevice::resetDescriptorPool(DescriptorPoolHandle handle) {
    if (!isDescriptorPoolValid(handle)) {
        return Error{.code = ErrorCode::HandleResolutionFailed,
                     .message = "Invalid descriptor pool handle in resetDescriptorPool"};
    }

    auto& poolSlot = descriptorPools_[handle.index()];
    poolSlot.allocatedCount = 0;

    for (auto& setSlot : descriptorSets_) {
        if (setSlot.alive && setSlot.pool == handle) {
            setSlot.alive = false;
            ++setSlot.generation;
        }
    }

    return Result<void>::success();
}

Result<DescriptorSetHandle> NullDevice::allocateDescriptorSet(DescriptorPoolHandle pool,
                                                              DescriptorSetLayoutHandle layout) {
    if (!isDescriptorPoolValid(pool)) {
        return Error{.code = ErrorCode::HandleResolutionFailed,
                     .message = "Invalid descriptor pool handle in allocateDescriptorSet"};
    }
    if (!isDescriptorSetLayoutValid(layout)) {
        return Error{.code = ErrorCode::HandleResolutionFailed,
                     .message = "Invalid descriptor set layout handle in allocateDescriptorSet"};
    }

    auto& poolSlot = descriptorPools_[pool.index()];
    if (poolSlot.allocatedCount >= poolSlot.desc.maxSets) {
        return Error{.code = ErrorCode::OutOfMemory,
                     .message = "Descriptor pool capacity exhausted"};
    }

    auto slotIndex = static_cast<uint32_t>(descriptorSets_.size());
    for (size_t i = 0; i < descriptorSets_.size(); ++i) {
        if (!descriptorSets_[i].alive) {
            slotIndex = static_cast<uint32_t>(i);
            break;
        }
    }

    if (slotIndex == descriptorSets_.size()) {
        DescriptorSetSlot newSlot{};
        newSlot.pool = pool;
        newSlot.layout = layout;
        newSlot.generation = 1;
        newSlot.alive = true;
        descriptorSets_.push_back(std::move(newSlot));
    } else {
        auto& slot = descriptorSets_[slotIndex];
        slot.pool = pool;
        slot.layout = layout;
        slot.writes.clear();
        slot.alive = true;
    }

    ++poolSlot.allocatedCount;
    return DescriptorSetHandle(
        GenerationalHandle{slotIndex, descriptorSets_[slotIndex].generation});
}

Result<void> NullDevice::updateDescriptorSet(DescriptorSetHandle set,
                                             std::span<const DescriptorWrite> writes) {
    if (!isDescriptorSetValid(set)) {
        return Error{.code = ErrorCode::HandleResolutionFailed,
                     .message = "Invalid descriptor set handle in updateDescriptorSet"};
    }

    auto& setSlot = descriptorSets_[set.index()];
    const auto* layoutDesc = getDescriptorSetLayoutDesc(setSlot.layout);
    if (layoutDesc == nullptr) {
        return Error{.code = ErrorCode::HandleResolutionFailed,
                     .message = "Descriptor set has invalid layout reference"};
    }

    for (const auto& write : writes) {
        const DescriptorBindingDesc* matchedBinding = nullptr;
        for (const auto& b : layoutDesc->bindings) {
            if (b.binding == write.dstBinding) {
                matchedBinding = &b;
                break;
            }
        }
        if (matchedBinding == nullptr) {
            return Error{.code = ErrorCode::InvalidArgument,
                         .message = "Descriptor write binding index not declared in layout"};
        }
        if (matchedBinding->descriptorType != write.descriptorType) {
            return Error{.code = ErrorCode::InvalidArgument,
                         .message = "Descriptor write type does not match layout binding type"};
        }

        if (write.descriptorType == DescriptorType::UniformBuffer ||
            write.descriptorType == DescriptorType::StorageBuffer) {
            if (write.bufferInfos.empty()) {
                return Error{.code = ErrorCode::InvalidArgument,
                             .message = "Descriptor buffer write has no buffer infos"};
            }
            for (const auto& bufInfo : write.bufferInfos) {
                if (!bufInfo.buffer.isValid() || !isBufferValid(bufInfo.buffer)) {
                    return Error{.code = ErrorCode::HandleResolutionFailed,
                                 .message = "Invalid buffer handle in descriptor buffer write"};
                }
                const size_t bufSize = getBufferSize(bufInfo.buffer);
                if (bufInfo.offset > bufSize) {
                    return Error{.code = ErrorCode::OutOfBounds,
                                 .message = "Descriptor buffer offset exceeds buffer size"};
                }
                if (bufInfo.range > 0 && (bufInfo.offset + bufInfo.range > bufSize)) {
                    return Error{.code = ErrorCode::OutOfBounds,
                                 .message = "Descriptor buffer range exceeds buffer size"};
                }
            }
        } else if (write.descriptorType == DescriptorType::CombinedImageSampler) {
            if (write.imageInfos.empty()) {
                return Error{.code = ErrorCode::InvalidArgument,
                             .message = "Descriptor image write has no image infos"};
            }
            for (const auto& imgInfo : write.imageInfos) {
                if (!imgInfo.texture.isValid() || !isTextureValid(imgInfo.texture)) {
                    return Error{.code = ErrorCode::HandleResolutionFailed,
                                 .message = "Invalid texture handle in CombinedImageSampler write"};
                }
                if (!imgInfo.sampler.isValid() || !isSamplerValid(imgInfo.sampler)) {
                    return Error{.code = ErrorCode::HandleResolutionFailed,
                                 .message = "Invalid sampler handle in CombinedImageSampler write"};
                }
            }
        } else if (write.descriptorType == DescriptorType::SampledImage) {
            if (write.imageInfos.empty()) {
                return Error{.code = ErrorCode::InvalidArgument,
                             .message = "Descriptor image write has no image infos"};
            }
            for (const auto& imgInfo : write.imageInfos) {
                if (!imgInfo.texture.isValid() || !isTextureValid(imgInfo.texture)) {
                    return Error{.code = ErrorCode::HandleResolutionFailed,
                                 .message = "Invalid texture handle in SampledImage write"};
                }
            }
        } else if (write.descriptorType == DescriptorType::Sampler) {
            if (write.imageInfos.empty()) {
                return Error{.code = ErrorCode::InvalidArgument,
                             .message = "Descriptor image write has no image infos"};
            }
            for (const auto& imgInfo : write.imageInfos) {
                if (!imgInfo.sampler.isValid() || !isSamplerValid(imgInfo.sampler)) {
                    return Error{.code = ErrorCode::HandleResolutionFailed,
                                 .message = "Invalid sampler handle in Sampler write"};
                }
            }
        }

        setSlot.writes.push_back(write);
    }

    return Result<void>::success();
}

bool NullDevice::isDescriptorSetLayoutValid(DescriptorSetLayoutHandle handle) const noexcept {
    if (handle.index() < descriptorSetLayouts_.size()) {
        const auto& slot = descriptorSetLayouts_[handle.index()];
        return slot.alive && slot.generation == handle.generation();
    }
    return false;
}

bool NullDevice::isDescriptorPoolValid(DescriptorPoolHandle handle) const noexcept {
    if (handle.index() < descriptorPools_.size()) {
        const auto& slot = descriptorPools_[handle.index()];
        return slot.alive && slot.generation == handle.generation();
    }
    return false;
}

bool NullDevice::isDescriptorSetValid(DescriptorSetHandle handle) const noexcept {
    if (handle.index() < descriptorSets_.size()) {
        const auto& slot = descriptorSets_[handle.index()];
        return slot.alive && slot.generation == handle.generation();
    }
    return false;
}

const DescriptorSetLayoutDesc*
NullDevice::getDescriptorSetLayoutDesc(DescriptorSetLayoutHandle handle) const noexcept {
    if (isDescriptorSetLayoutValid(handle)) {
        return &descriptorSetLayouts_[handle.index()].desc;
    }
    return nullptr;
}

const DescriptorPoolDesc*
NullDevice::getDescriptorPoolDesc(DescriptorPoolHandle handle) const noexcept {
    if (isDescriptorPoolValid(handle)) {
        return &descriptorPools_[handle.index()].desc;
    }
    return nullptr;
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
