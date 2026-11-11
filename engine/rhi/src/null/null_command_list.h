/// @file null_command_list.h
/// @brief Headless mock command list implementation.
///
/// Purpose:
/// Records RHI commands in host memory and aggregates execution statistics for headless testing.
///
/// Ownership and Lifetime:
/// Created and owned via std::unique_ptr<RhiCommandList> by caller.
///
/// Thread-safety:
/// Single-threaded recording per instance.
///
/// Performance:
/// Host memory command buffering with minimal overhead.

#pragma once

#include "rhi/command_list.h"

#include <vector>

namespace eng::rhi {

class NullDevice;

/// @class NullCommandList
/// @brief In-memory mock implementation of RhiCommandList.
class NullCommandList final : public RhiCommandList {
public:
    enum class State : uint8_t { Initial = 0, Recording = 1, Ended = 2, Submitted = 3 };

    struct Viewport {
        float x{0.0F};
        float y{0.0F};
        float width{0.0F};
        float height{0.0F};
        float minDepth{0.0F};
        float maxDepth{1.0F};
    };

    struct Scissor {
        int32_t x{0};
        int32_t y{0};
        uint32_t width{0};
        uint32_t height{0};
    };

    struct VertexBufferBinding {
        uint32_t binding{0};
        // NOLINTNEXTLINE(readability-redundant-member-init): Required for GCC
        // -Wmissing-field-initializers
        BufferHandle buffer{};
        size_t offset{0};
    };

    struct IndexBufferBinding {
        // NOLINTNEXTLINE(readability-redundant-member-init): Required for GCC
        // -Wmissing-field-initializers
        BufferHandle buffer{};
        size_t offset{0};
        bool is32Bit{false};
    };

    struct DrawCmd {
        uint32_t vertexCount{0};
        uint32_t instanceCount{0};
        uint32_t firstVertex{0};
        uint32_t firstInstance{0};
    };

    struct DrawIndexedCmd {
        uint32_t indexCount{0};
        uint32_t instanceCount{0};
        uint32_t firstIndex{0};
        int32_t vertexOffset{0};
        uint32_t firstInstance{0};
    };

    struct CopyBufferCmd {
        BufferHandle src;
        BufferHandle dst;
        size_t srcOffset{0};
        size_t dstOffset{0};
        size_t size{0};
    };

    struct UpdateBufferCmd {
        BufferHandle dst;
        size_t dstOffset{0};
        std::vector<uint8_t> data;
    };

    struct DescriptorSetBinding {
        PipelineLayoutHandle layout{};
        uint32_t firstSet{0};
        std::vector<DescriptorSetHandle> sets;
        std::vector<uint32_t> dynamicOffsets;
    };

    struct TransitionTextureLayoutCmd {
        TextureHandle texture{};
        TextureLayout oldLayout{TextureLayout::Undefined};
        TextureLayout newLayout{TextureLayout::Undefined};
        uint32_t baseMipLevel{0};
        uint32_t mipLevelCount{1};
        uint32_t baseArrayLayer{0};
        uint32_t arrayLayerCount{1};
    };

    struct CopyBufferToTextureCmd {
        BufferHandle srcBuffer{};
        TextureHandle dstTexture{};
        size_t bufferOffset{0};
        uint32_t mipLevel{0};
        uint32_t arrayLayer{0};
    };

    struct UpdateTextureCmd {
        TextureHandle dst{};
        std::vector<uint8_t> data{};
        uint32_t mipLevel{0};
        uint32_t arrayLayer{0};
    };

    explicit NullCommandList(NullDevice& device) noexcept;
    ~NullCommandList() noexcept override = default;

    NullCommandList(const NullCommandList&) = delete;
    NullCommandList& operator=(const NullCommandList&) = delete;
    NullCommandList(NullCommandList&&) noexcept = delete;
    NullCommandList& operator=(NullCommandList&&) noexcept = delete;

    void begin() override;
    void end() override;

    void beginRenderPass(const RenderPassBeginInfo& info) override;
    void endRenderPass() override;
    void clearRect(int32_t x, int32_t y, uint32_t width, uint32_t height, float r, float g, float b,
                   float a) override;

    void setViewport(float x, float y, float width, float height, float minDepth = 0.0F,
                     float maxDepth = 1.0F) override;
    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) override;

    void bindPipeline(PipelineHandle pipeline) override;
    void bindDescriptorSets(PipelineLayoutHandle layout, uint32_t firstSet,
                            std::span<const DescriptorSetHandle> sets,
                            std::span<const uint32_t> dynamicOffsets = {}) override;
    void pushConstants(PipelineLayoutHandle layout, ShaderStageFlags stages, uint32_t offset,
                       uint32_t size, const void* data) override;

    void bindVertexBuffer(uint32_t binding, BufferHandle buffer, size_t offset = 0) override;
    void bindIndexBuffer(BufferHandle buffer, size_t offset = 0, bool is32Bit = false) override;

    void draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0,
              uint32_t firstInstance = 0) override;
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0,
                     int32_t vertexOffset = 0, uint32_t firstInstance = 0) override;

    void copyBuffer(BufferHandle src, BufferHandle dst, size_t srcOffset, size_t dstOffset,
                    size_t size) override;
    void copyTextureToBuffer(TextureHandle srcTexture, BufferHandle dstBuffer) override;
    void copyBufferToTexture(BufferHandle srcBuffer, TextureHandle dstTexture,
                             size_t bufferOffset = 0, uint32_t mipLevel = 0,
                             uint32_t arrayLayer = 0) override;
    void transitionTextureLayout(TextureHandle texture, TextureLayout oldLayout,
                                 TextureLayout newLayout, uint32_t baseMipLevel = 0,
                                 uint32_t mipLevelCount = 1, uint32_t baseArrayLayer = 0,
                                 uint32_t arrayLayerCount = 1) override;
    void updateTexture(TextureHandle dst, const void* data, size_t size, uint32_t mipLevel = 0,
                       uint32_t arrayLayer = 0) override;
    void generateMipmaps(TextureHandle texture) override;
    void updateBuffer(BufferHandle dst, size_t dstOffset, const void* data, size_t size) override;

    [[nodiscard]] State getState() const noexcept { return state_; }
    void setState(State state) noexcept { state_ = state; }

    [[nodiscard]] PipelineHandle getBoundPipeline() const noexcept { return boundPipeline_; }
    [[nodiscard]] bool isPipelineBound() const noexcept;
    [[nodiscard]] bool hasUnboundDraw() const noexcept { return hasUnboundDraw_; }
    [[nodiscard]] bool isInsideRenderPass() const noexcept { return isInsideRenderPass_; }
    [[nodiscard]] TextureHandle getActiveColorTarget() const noexcept { return activeColorTarget_; }
    [[nodiscard]] TextureHandle getActiveDepthTarget() const noexcept { return activeDepthTarget_; }
    [[nodiscard]] TextureFormat getActiveColorFormat() const noexcept { return activeColorFormat_; }
    [[nodiscard]] TextureFormat getActiveDepthFormat() const noexcept { return activeDepthFormat_; }
    [[nodiscard]] uint32_t getActiveWidth() const noexcept { return activeWidth_; }
    [[nodiscard]] uint32_t getActiveHeight() const noexcept { return activeHeight_; }
    [[nodiscard]] bool hasInvalidAttachment() const noexcept { return hasInvalidAttachment_; }
    [[nodiscard]] bool hasDimensionMismatch() const noexcept { return hasDimensionMismatch_; }
    [[nodiscard]] bool hasFormatMismatch() const noexcept { return hasFormatMismatch_; }
    [[nodiscard]] bool hasNestedRenderPass() const noexcept { return hasNestedRenderPass_; }
    [[nodiscard]] bool hasOrphanEndPass() const noexcept { return hasOrphanEndPass_; }
    [[nodiscard]] uint32_t getPipelineBindCount() const noexcept { return pipelineBindCount_; }
    [[nodiscard]] uint32_t getBindDescriptorSetsCount() const noexcept {
        return bindDescriptorSetsCount_;
    }
    [[nodiscard]] uint32_t getPushConstantCount() const noexcept { return pushConstantCount_; }
    [[nodiscard]] uint32_t getDrawCount() const noexcept { return drawCount_; }
    [[nodiscard]] uint32_t getDrawIndexedCount() const noexcept { return drawIndexedCount_; }
    [[nodiscard]] uint32_t getCopyCount() const noexcept { return copyCount_; }
    [[nodiscard]] uint32_t getUpdateCount() const noexcept { return updateCount_; }

    [[nodiscard]] const std::vector<DescriptorSetBinding>&
    getDescriptorSetBindings() const noexcept {
        return descriptorSetBindings_;
    }
    [[nodiscard]] const std::vector<CopyBufferCmd>& getCopyCommands() const noexcept {
        return copyCommands_;
    }
    [[nodiscard]] const std::vector<UpdateBufferCmd>& getUpdateCommands() const noexcept {
        return updateCommands_;
    }
    [[nodiscard]] const std::vector<TransitionTextureLayoutCmd>&
    getTransitionTextureLayoutCommands() const noexcept {
        return transitionTextureLayoutCommands_;
    }
    [[nodiscard]] const std::vector<CopyBufferToTextureCmd>&
    getCopyBufferToTextureCommands() const noexcept {
        return copyBufferToTextureCommands_;
    }
    [[nodiscard]] const std::vector<UpdateTextureCmd>& getUpdateTextureCommands() const noexcept {
        return updateTextureCommands_;
    }
    [[nodiscard]] const std::vector<TextureHandle>& getGenerateMipmapsCommands() const noexcept {
        return generateMipmapsCommands_;
    }

private:
    NullDevice& device_;
    State state_{State::Initial};

    Viewport currentViewport_{};
    Scissor currentScissor_{};
    PipelineHandle boundPipeline_{};
    PipelineLayoutHandle boundLayout_{};
    bool hasUnboundDraw_{false};
    bool isInsideRenderPass_{false};
    TextureHandle activeColorTarget_{};
    TextureHandle activeDepthTarget_{};
    TextureFormat activeColorFormat_{TextureFormat::Undefined};
    TextureFormat activeDepthFormat_{TextureFormat::Undefined};
    uint32_t activeWidth_{0};
    uint32_t activeHeight_{0};

    bool hasInvalidAttachment_{false};
    bool hasDimensionMismatch_{false};
    bool hasFormatMismatch_{false};
    bool hasNestedRenderPass_{false};
    bool hasOrphanEndPass_{false};

    std::vector<VertexBufferBinding> vertexBindings_;
    IndexBufferBinding indexBinding_{};

    std::vector<DrawCmd> drawCommands_;
    std::vector<DrawIndexedCmd> drawIndexedCommands_;
    std::vector<CopyBufferCmd> copyCommands_;
    std::vector<UpdateBufferCmd> updateCommands_;
    std::vector<DescriptorSetBinding> descriptorSetBindings_;
    std::vector<TransitionTextureLayoutCmd> transitionTextureLayoutCommands_;
    std::vector<CopyBufferToTextureCmd> copyBufferToTextureCommands_;
    std::vector<UpdateTextureCmd> updateTextureCommands_;
    std::vector<TextureHandle> generateMipmapsCommands_;

    uint32_t pipelineBindCount_{0};
    uint32_t bindDescriptorSetsCount_{0};
    uint32_t pushConstantCount_{0};
    uint32_t drawCount_{0};
    uint32_t drawIndexedCount_{0};
    uint32_t copyCount_{0};
    uint32_t updateCount_{0};
};

} // namespace eng::rhi
