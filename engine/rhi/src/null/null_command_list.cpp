/// @file null_command_list.cpp
/// @brief Headless mock command list implementation.

#include "null_command_list.h"

#include "core/assert.h"
#include "null_device.h"

namespace eng::rhi {

NullCommandList::NullCommandList(NullDevice& device) noexcept : device_(device) {}

bool NullCommandList::isPipelineBound() const noexcept {
    return boundPipeline_.isValid() && device_.isPipelineValid(boundPipeline_);
}

void NullCommandList::begin() {
    state_ = State::Recording;
    boundPipeline_ = {};
    boundLayout_ = {};
    hasUnboundDraw_ = false;
    isInsideRenderPass_ = false;
    activeColorTarget_ = {};
    activeDepthTarget_ = {};
    activeColorFormat_ = TextureFormat::Undefined;
    activeDepthFormat_ = TextureFormat::Undefined;
    activeWidth_ = 0;
    activeHeight_ = 0;
    hasInvalidAttachment_ = false;
    hasDimensionMismatch_ = false;
    hasFormatMismatch_ = false;
    hasNestedRenderPass_ = false;
    hasOrphanEndPass_ = false;
    vertexBindings_.clear();
    drawCommands_.clear();
    drawIndexedCommands_.clear();
    copyCommands_.clear();
    updateCommands_.clear();

    pipelineBindCount_ = 0;
    pushConstantCount_ = 0;
    drawCount_ = 0;
    drawIndexedCount_ = 0;
    copyCount_ = 0;
    updateCount_ = 0;
}

void NullCommandList::end() {
    ENG_ASSERT(state_ == State::Recording, "end() called on command list not in Recording state");
    state_ = State::Ended;
}

void NullCommandList::beginRenderPass(const RenderPassBeginInfo& info) {
    ENG_ASSERT(state_ == State::Recording,
               "beginRenderPass called on command list not in Recording state");
    if (isInsideRenderPass_) {
        hasNestedRenderPass_ = true;
        return;
    }

    if (info.colorTarget.isValid()) {
        if (!device_.isTextureValid(info.colorTarget)) {
            hasInvalidAttachment_ = true;
            return;
        }
        const auto* colorDesc = device_.getTextureDesc(info.colorTarget);
        if (colorDesc == nullptr ||
            !hasFlag(colorDesc->usage, TextureUsageFlags::ColorAttachment) ||
            !isColorFormat(colorDesc->format)) {
            hasInvalidAttachment_ = true;
            return;
        }

        activeColorTarget_ = info.colorTarget;
        activeColorFormat_ = colorDesc->format;
        activeWidth_ = info.renderWidth > 0 ? info.renderWidth : colorDesc->width;
        activeHeight_ = info.renderHeight > 0 ? info.renderHeight : colorDesc->height;

        if (info.depthTarget.isValid()) {
            if (!device_.isTextureValid(info.depthTarget)) {
                hasInvalidAttachment_ = true;
                return;
            }
            const auto* depthDesc = device_.getTextureDesc(info.depthTarget);
            if (depthDesc == nullptr ||
                !hasFlag(depthDesc->usage, TextureUsageFlags::DepthStencilAttachment) ||
                !isDepthFormat(depthDesc->format)) {
                hasInvalidAttachment_ = true;
                return;
            }
            if (depthDesc->width != colorDesc->width || depthDesc->height != colorDesc->height) {
                hasDimensionMismatch_ = true;
                return;
            }
            activeDepthTarget_ = info.depthTarget;
            activeDepthFormat_ = depthDesc->format;
        } else {
            activeDepthTarget_ = {};
            activeDepthFormat_ = TextureFormat::Undefined;
        }
    } else {
        // Swapchain presentation target baseline
        activeColorTarget_ = {};
        activeDepthTarget_ = {};
        activeColorFormat_ = TextureFormat::BGRA8_UNORM;
        activeDepthFormat_ = TextureFormat::Undefined;
        activeWidth_ = info.renderWidth > 0 ? info.renderWidth : 1280;
        activeHeight_ = info.renderHeight > 0 ? info.renderHeight : 720;
    }

    isInsideRenderPass_ = true;
    setViewport(0.0F, 0.0F, static_cast<float>(activeWidth_), static_cast<float>(activeHeight_));
    setScissor(0, 0, activeWidth_, activeHeight_);
}

void NullCommandList::endRenderPass() {
    ENG_ASSERT(state_ == State::Recording,
               "endRenderPass called on command list not in Recording state");
    if (!isInsideRenderPass_) {
        hasOrphanEndPass_ = true;
        return;
    }
    isInsideRenderPass_ = false;
    activeColorTarget_ = {};
    activeDepthTarget_ = {};
}

void NullCommandList::clearRect(int32_t /*x*/, int32_t /*y*/, uint32_t /*width*/,
                                uint32_t /*height*/, float /*r*/, float /*g*/, float /*b*/,
                                float /*a*/) {
    ENG_ASSERT(state_ == State::Recording, "clearRect called outside begin()/end()");
    drawCount_++;
}

void NullCommandList::setViewport(float x, float y, float width, float height, float minDepth,
                                  float maxDepth) {
    ENG_ASSERT(state_ == State::Recording, "setViewport called outside begin()/end()");
    currentViewport_ = Viewport{.x = x,
                                .y = y,
                                .width = width,
                                .height = height,
                                .minDepth = minDepth,
                                .maxDepth = maxDepth};
}

void NullCommandList::setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    ENG_ASSERT(state_ == State::Recording, "setScissor called outside begin()/end()");
    currentScissor_ = Scissor{.x = x, .y = y, .width = width, .height = height};
}

void NullCommandList::bindPipeline(PipelineHandle pipeline) {
    ENG_ASSERT(state_ == State::Recording, "bindPipeline called outside begin()/end()");
    if (device_.isPipelineValid(pipeline)) {
        boundPipeline_ = pipeline;
        const auto* pDesc = device_.getPipelineDesc(pipeline);
        boundLayout_ = (pDesc != nullptr) ? pDesc->layout : PipelineLayoutHandle{};
        ++pipelineBindCount_;
    } else {
        boundPipeline_ = {};
        boundLayout_ = {};
    }
}

void NullCommandList::pushConstants(PipelineLayoutHandle layout, ShaderStageFlags stages,
                                    uint32_t offset, uint32_t size, const void* data) {
    ENG_ASSERT(state_ == State::Recording, "pushConstants called outside begin()/end()");
    (void)stages;
    if (device_.isPipelineLayoutValid(layout) && size > 0 && data != nullptr && (offset % 4 == 0) &&
        (size % 4 == 0)) {
        ++pushConstantCount_;
    }
}

void NullCommandList::bindVertexBuffer(uint32_t binding, BufferHandle buffer, size_t offset) {
    ENG_ASSERT(state_ == State::Recording, "bindVertexBuffer called outside begin()/end()");
    vertexBindings_.push_back(
        VertexBufferBinding{.binding = binding, .buffer = buffer, .offset = offset});
}

void NullCommandList::bindIndexBuffer(BufferHandle buffer, size_t offset, bool is32Bit) {
    ENG_ASSERT(state_ == State::Recording, "bindIndexBuffer called outside begin()/end()");
    indexBinding_ = IndexBufferBinding{.buffer = buffer, .offset = offset, .is32Bit = is32Bit};
}

void NullCommandList::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                           uint32_t firstInstance) {
    ENG_ASSERT(state_ == State::Recording, "draw called outside begin()/end()");
    if (!isPipelineBound()) {
        hasUnboundDraw_ = true;
    } else if (isInsideRenderPass_) {
        const auto* pDesc = device_.getPipelineDesc(boundPipeline_);
        if (pDesc != nullptr) {
            if (!pDesc->colorAttachmentFormats.empty() &&
                pDesc->colorAttachmentFormats[0] != activeColorFormat_) {
                hasFormatMismatch_ = true;
            }
            if (pDesc->depthAttachmentFormat != TextureFormat::Undefined &&
                pDesc->depthAttachmentFormat != activeDepthFormat_) {
                hasFormatMismatch_ = true;
            }
        }
    }
    drawCommands_.push_back(DrawCmd{.vertexCount = vertexCount,
                                    .instanceCount = instanceCount,
                                    .firstVertex = firstVertex,
                                    .firstInstance = firstInstance});
    ++drawCount_;
}

void NullCommandList::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
                                  int32_t vertexOffset, uint32_t firstInstance) {
    ENG_ASSERT(state_ == State::Recording, "drawIndexed called outside begin()/end()");
    if (!isPipelineBound()) {
        hasUnboundDraw_ = true;
    } else if (isInsideRenderPass_) {
        const auto* pDesc = device_.getPipelineDesc(boundPipeline_);
        if (pDesc != nullptr) {
            if (!pDesc->colorAttachmentFormats.empty() &&
                pDesc->colorAttachmentFormats[0] != activeColorFormat_) {
                hasFormatMismatch_ = true;
            }
            if (pDesc->depthAttachmentFormat != TextureFormat::Undefined &&
                pDesc->depthAttachmentFormat != activeDepthFormat_) {
                hasFormatMismatch_ = true;
            }
        }
    }
    drawIndexedCommands_.push_back(DrawIndexedCmd{.indexCount = indexCount,
                                                  .instanceCount = instanceCount,
                                                  .firstIndex = firstIndex,
                                                  .vertexOffset = vertexOffset,
                                                  .firstInstance = firstInstance});
    ++drawIndexedCount_;
}

void NullCommandList::copyBuffer(BufferHandle src, BufferHandle dst, size_t srcOffset,
                                 size_t dstOffset, size_t size) {
    ENG_ASSERT(state_ == State::Recording, "copyBuffer called outside begin()/end()");
    copyCommands_.push_back(CopyBufferCmd{
        .src = src, .dst = dst, .srcOffset = srcOffset, .dstOffset = dstOffset, .size = size});
    ++copyCount_;
}

void NullCommandList::updateBuffer(BufferHandle dst, size_t dstOffset, const void* data,
                                   size_t size) {
    ENG_ASSERT(state_ == State::Recording, "updateBuffer called outside begin()/end()");
    UpdateBufferCmd cmd{};
    cmd.dst = dst;
    cmd.dstOffset = dstOffset;
    if (data != nullptr && size > 0) {
        cmd.data.resize(size);
        const auto* bytePtr = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            cmd.data[i] = bytePtr[i];
        }
    }
    updateCommands_.push_back(std::move(cmd));
    ++updateCount_;
}

} // namespace eng::rhi
