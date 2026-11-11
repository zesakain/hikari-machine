/// @file gpu_texture.cpp
/// @brief GPU texture allocation, staging upload, and sampler creation implementation.

#include "render/gpu_texture.h"

#include "core/log.h"
#include "rhi/buffer.h"
#include "rhi/command_list.h"

#include <algorithm>
#include <cmath>

namespace eng::render {

GpuTexture::GpuTexture(rhi::RhiDevice& device, rhi::TextureHandle texture,
                       rhi::SamplerHandle sampler, uint32_t width, uint32_t height,
                       uint32_t mipLevels, rhi::TextureFormat format) noexcept
    : device_(&device), texture_(texture), sampler_(sampler), width_(width), height_(height),
      mipLevels_(mipLevels), format_(format) {}

GpuTexture::~GpuTexture() noexcept {
    if (device_ != nullptr) {
        if (sampler_.isValid()) {
            device_->destroySampler(sampler_);
            sampler_ = {};
        }
        if (texture_.isValid()) {
            device_->destroyTexture(texture_);
            texture_ = {};
        }
        device_ = nullptr;
    }
}

GpuTexture::GpuTexture(GpuTexture&& other) noexcept
    : device_(other.device_), texture_(other.texture_), sampler_(other.sampler_),
      width_(other.width_), height_(other.height_), mipLevels_(other.mipLevels_),
      format_(other.format_) {
    other.device_ = nullptr;
    other.texture_ = {};
    other.sampler_ = {};
    other.width_ = 0;
    other.height_ = 0;
    other.mipLevels_ = 1;
}

GpuTexture& GpuTexture::operator=(GpuTexture&& other) noexcept {
    if (this != &other) {
        if (device_ != nullptr) {
            if (sampler_.isValid()) {
                device_->destroySampler(sampler_);
            }
            if (texture_.isValid()) {
                device_->destroyTexture(texture_);
            }
        }
        device_ = other.device_;
        texture_ = other.texture_;
        sampler_ = other.sampler_;
        width_ = other.width_;
        height_ = other.height_;
        mipLevels_ = other.mipLevels_;
        format_ = other.format_;

        other.device_ = nullptr;
        other.texture_ = {};
        other.sampler_ = {};
        other.width_ = 0;
        other.height_ = 0;
        other.mipLevels_ = 1;
    }
    return *this;
}

Result<std::unique_ptr<GpuTexture>> GpuTexture::create(rhi::RhiDevice& device,
                                                       const ImageData& imageData,
                                                       bool generateMips,
                                                       const rhi::SamplerDesc* customSamplerDesc) {
    if (!imageData.isValid()) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Cannot create GpuTexture from invalid ImageData"};
    }

    uint32_t mipLevels = 1;
    if (generateMips) {
        const uint32_t maxDim = std::max(imageData.width, imageData.height);
        mipLevels = static_cast<uint32_t>(std::floor(std::log2(static_cast<double>(maxDim)))) + 1U;
    }

    rhi::TextureUsageFlags usage =
        rhi::TextureUsageFlags::Sampled | rhi::TextureUsageFlags::TransferDst;
    if (generateMips && mipLevels > 1) {
        usage |= rhi::TextureUsageFlags::TransferSrc;
    }

    rhi::TextureDesc texDesc{
        .type = rhi::TextureType::Texture2D,
        .format = imageData.format,
        .width = imageData.width,
        .height = imageData.height,
        .depth = 1,
        .mipLevels = mipLevels,
        .arrayLayers = 1,
        .usage = usage,
        .debugName = "GpuTexture",
    };

    auto texRes = device.createTexture(texDesc);
    if (!texRes.hasValue()) {
        return texRes.error();
    }
    rhi::TextureHandle texture = texRes.value();

    // Configure and create sampler
    rhi::SamplerDesc defaultSamplerDesc{
        .minFilter = rhi::Filter::Linear,
        .magFilter = rhi::Filter::Linear,
        .mipmapMode = rhi::Filter::Linear,
        .addressU = rhi::SamplerAddressMode::Repeat,
        .addressV = rhi::SamplerAddressMode::Repeat,
        .addressW = rhi::SamplerAddressMode::Repeat,
        .anisotropyEnable = true,
        .maxAnisotropy = 16.0F,
        .minLod = 0.0F,
        .maxLod = static_cast<float>(mipLevels),
        .mipLodBias = 0.0F,
        .compareEnable = false,
        .debugName = "GpuTextureSampler",
    };

    const rhi::SamplerDesc& samplerToCreate =
        (customSamplerDesc != nullptr) ? *customSamplerDesc : defaultSamplerDesc;
    auto samplerRes = device.createSampler(samplerToCreate);
    if (!samplerRes.hasValue()) {
        device.destroyTexture(texture);
        return samplerRes.error();
    }
    rhi::SamplerHandle sampler = samplerRes.value();

    // Create host-visible staging buffer
    rhi::BufferDesc stagingDesc{
        .sizeInBytes = imageData.sizeInBytes(),
        .usage = rhi::BufferUsageFlags::TransferSrc,
        .memoryUsage = rhi::MemoryUsage::CpuToGpu,
        .debugName = "TextureStagingBuffer",
    };
    auto stagingRes = device.createBuffer(stagingDesc);
    if (!stagingRes.hasValue()) {
        device.destroySampler(sampler);
        device.destroyTexture(texture);
        return stagingRes.error();
    }
    rhi::BufferHandle stagingBuf = stagingRes.value();

    auto writeRes =
        device.writeBuffer(stagingBuf, 0, imageData.pixels.data(), imageData.sizeInBytes());
    if (!writeRes.hasValue()) {
        device.destroyBuffer(stagingBuf);
        device.destroySampler(sampler);
        device.destroyTexture(texture);
        return writeRes.error();
    }

    auto cmdRes = device.createCommandList();
    if (!cmdRes.hasValue()) {
        device.destroyBuffer(stagingBuf);
        device.destroySampler(sampler);
        device.destroyTexture(texture);
        return cmdRes.error();
    }
    auto cmd = std::move(cmdRes.value());

    cmd->begin();
    cmd->transitionTextureLayout(texture, rhi::TextureLayout::Undefined,
                                 rhi::TextureLayout::TransferDst, 0, 1, 0, 1);
    cmd->copyBufferToTexture(stagingBuf, texture, 0, 0, 0);

    if (generateMips && mipLevels > 1) {
        cmd->generateMipmaps(texture);
    } else {
        cmd->transitionTextureLayout(texture, rhi::TextureLayout::TransferDst,
                                     rhi::TextureLayout::ShaderReadOnly, 0, 1, 0, 1);
    }
    cmd->end();

    auto submitRes = device.submit(cmd.get());
    device.destroyBuffer(stagingBuf);

    if (!submitRes.hasValue()) {
        device.destroySampler(sampler);
        device.destroyTexture(texture);
        return submitRes.error();
    }

    return std::unique_ptr<GpuTexture>(new GpuTexture(
        device, texture, sampler, imageData.width, imageData.height, mipLevels, imageData.format));
}

} // namespace eng::render
