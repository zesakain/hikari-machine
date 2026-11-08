/// @file null_device.h
/// @brief Headless mock RHI device implementation for CI and unit tests.
///
/// Purpose:
/// Provides a fully in-memory, zero-GPU RHI backend for headless simulation tests,
/// validation, and continuous integration.
///
/// Ownership and Lifetime:
/// Created by createNullDevice() factory and owned via std::unique_ptr<RhiDevice>.
///
/// Thread-safety:
/// Resource allocation and destruction are not thread-safe and must be called from the main thread
/// or serialized.
///
/// Performance:
/// In-memory allocations only; zero graphics driver overhead.

#pragma once

#include "rhi/rhi_device.h"

#include <cstdint>
#include <vector>

namespace eng::rhi {

/// @class NullDevice
/// @brief In-memory mock implementation of RhiDevice.
class NullDevice final : public RhiDevice {
public:
    struct BufferSlot {
        BufferDesc desc{};
        std::vector<uint8_t> storage;
        uint32_t generation{1};
        bool alive{false};
    };

    struct TextureSlot {
        TextureDesc desc{};
        uint32_t generation{1};
        bool alive{false};
    };

    struct SamplerSlot {
        SamplerDesc desc{};
        uint32_t generation{1};
        bool alive{false};
    };

    struct ShaderSlot {
        ShaderDesc desc{};
        std::vector<uint32_t> code;
        uint32_t generation{1};
        bool alive{false};
    };

    struct PipelineLayoutSlot {
        PipelineLayoutDesc desc{};
        uint32_t generation{1};
        bool alive{false};
    };

    struct PipelineSlot {
        GraphicsPipelineDesc desc{};
        uint32_t generation{1};
        bool alive{false};
    };

    NullDevice() noexcept;
    ~NullDevice() noexcept override = default;

    NullDevice(const NullDevice&) = delete;
    NullDevice& operator=(const NullDevice&) = delete;
    NullDevice(NullDevice&&) noexcept = delete;
    NullDevice& operator=(NullDevice&&) noexcept = delete;

    [[nodiscard]] const GpuCaps& getCaps() const noexcept override;

    [[nodiscard]] Result<BufferHandle> createBuffer(const BufferDesc& desc) override;
    void destroyBuffer(BufferHandle handle) override;
    Result<void> writeBuffer(BufferHandle handle, size_t offset, const void* data,
                             size_t size) override;

    [[nodiscard]] Result<TextureHandle> createTexture(const TextureDesc& desc) override;
    void destroyTexture(TextureHandle handle) override;

    [[nodiscard]] Result<SamplerHandle> createSampler(const SamplerDesc& desc) override;
    void destroySampler(SamplerHandle handle) override;

    [[nodiscard]] Result<ShaderHandle> createShader(const ShaderDesc& desc) override;
    void destroyShader(ShaderHandle handle) override;

    [[nodiscard]] Result<PipelineLayoutHandle>
    createPipelineLayout(const PipelineLayoutDesc& desc) override;
    void destroyPipelineLayout(PipelineLayoutHandle handle) override;

    [[nodiscard]] Result<PipelineHandle>
    createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    void destroyGraphicsPipeline(PipelineHandle handle) override;

    [[nodiscard]] Result<std::unique_ptr<RhiCommandList>> createCommandList() override;
    Result<void> submit(RhiCommandList* commandList) override;
    void waitIdle() override;

    // Inspection helpers for testing
    [[nodiscard]] bool isBufferValid(BufferHandle handle) const noexcept;
    [[nodiscard]] bool isTextureValid(TextureHandle handle) const noexcept;
    [[nodiscard]] bool isSamplerValid(SamplerHandle handle) const noexcept;
    [[nodiscard]] bool isShaderValid(ShaderHandle handle) const noexcept;
    [[nodiscard]] bool isPipelineLayoutValid(PipelineLayoutHandle handle) const noexcept;
    [[nodiscard]] bool isPipelineValid(PipelineHandle handle) const noexcept;
    [[nodiscard]] const uint8_t* getBufferData(BufferHandle handle) const noexcept;
    [[nodiscard]] size_t getBufferSize(BufferHandle handle) const noexcept;
    [[nodiscard]] const ShaderDesc* getShaderDesc(ShaderHandle handle) const noexcept;
    [[nodiscard]] const PipelineLayoutDesc*
    getPipelineLayoutDesc(PipelineLayoutHandle handle) const noexcept;
    [[nodiscard]] const GraphicsPipelineDesc* getPipelineDesc(PipelineHandle handle) const noexcept;
    [[nodiscard]] uint32_t getSubmittedCount() const noexcept { return submittedCount_; }

private:
    [[nodiscard]] BufferSlot* resolveBuffer(BufferHandle handle) noexcept;
    [[nodiscard]] const BufferSlot* resolveBuffer(BufferHandle handle) const noexcept;

    GpuCaps caps_{};
    std::vector<BufferSlot> buffers_;
    std::vector<TextureSlot> textures_;
    std::vector<SamplerSlot> samplers_;
    std::vector<ShaderSlot> shaders_;
    std::vector<PipelineLayoutSlot> pipelineLayouts_;
    std::vector<PipelineSlot> pipelines_;
    uint32_t submittedCount_{0};
};

} // namespace eng::rhi
