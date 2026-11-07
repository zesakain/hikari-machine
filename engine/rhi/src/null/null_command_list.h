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
        // NOLINTNEXTLINE(readability-redundant-member-init): Required for GCC -Wmissing-field-initializers
        BufferHandle buffer{};
        size_t offset{0};
    };

    struct IndexBufferBinding {
        // NOLINTNEXTLINE(readability-redundant-member-init): Required for GCC -Wmissing-field-initializers
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

    explicit NullCommandList(NullDevice& device) noexcept;
    ~NullCommandList() noexcept override = default;

    NullCommandList(const NullCommandList&) = delete;
    NullCommandList& operator=(const NullCommandList&) = delete;
    NullCommandList(NullCommandList&&) noexcept = delete;
    NullCommandList& operator=(NullCommandList&&) noexcept = delete;

    void begin() override;
    void end() override;

    void setViewport(float x, float y, float width, float height, float minDepth = 0.0F,
                     float maxDepth = 1.0F) override;
    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) override;

    void bindVertexBuffer(uint32_t binding, BufferHandle buffer, size_t offset = 0) override;
    void bindIndexBuffer(BufferHandle buffer, size_t offset = 0, bool is32Bit = false) override;

    void draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0,
              uint32_t firstInstance = 0) override;
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0,
                     int32_t vertexOffset = 0, uint32_t firstInstance = 0) override;

    void copyBuffer(BufferHandle src, BufferHandle dst, size_t srcOffset, size_t dstOffset,
                    size_t size) override;
    void updateBuffer(BufferHandle dst, size_t dstOffset, const void* data, size_t size) override;

    [[nodiscard]] State getState() const noexcept { return state_; }
    void setState(State state) noexcept { state_ = state; }

    [[nodiscard]] uint32_t getDrawCount() const noexcept { return drawCount_; }
    [[nodiscard]] uint32_t getDrawIndexedCount() const noexcept { return drawIndexedCount_; }
    [[nodiscard]] uint32_t getCopyCount() const noexcept { return copyCount_; }
    [[nodiscard]] uint32_t getUpdateCount() const noexcept { return updateCount_; }

    [[nodiscard]] const std::vector<CopyBufferCmd>& getCopyCommands() const noexcept {
        return copyCommands_;
    }
    [[nodiscard]] const std::vector<UpdateBufferCmd>& getUpdateCommands() const noexcept {
        return updateCommands_;
    }

private:
    NullDevice& device_;
    State state_{State::Initial};

    Viewport currentViewport_{};
    Scissor currentScissor_{};
    std::vector<VertexBufferBinding> vertexBindings_;
    IndexBufferBinding indexBinding_{};

    std::vector<DrawCmd> drawCommands_;
    std::vector<DrawIndexedCmd> drawIndexedCommands_;
    std::vector<CopyBufferCmd> copyCommands_;
    std::vector<UpdateBufferCmd> updateCommands_;

    uint32_t drawCount_{0};
    uint32_t drawIndexedCount_{0};
    uint32_t copyCount_{0};
    uint32_t updateCount_{0};
};

} // namespace eng::rhi
