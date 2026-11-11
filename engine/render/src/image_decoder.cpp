/// @file image_decoder.cpp
/// @brief Concrete implementation of ImageDecoder using stb_image.

#include "core/log.h"
#include "render/image.h"

#define STB_IMAGE_STATIC
#define STBI_NO_STDIO_NOT_REALLY
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-function"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
#endif
#include "stb/stb_image.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <cstring>
#include <filesystem>

namespace eng::render {

namespace {

rhi::TextureFormat resolveFormat(int channels, bool srgb) noexcept {
    if (channels == 1) {
        return rhi::TextureFormat::R8_UNORM;
    }
    if (channels == 4) {
        return srgb ? rhi::TextureFormat::RGBA8_SRGB : rhi::TextureFormat::RGBA8_UNORM;
    }
    return srgb ? rhi::TextureFormat::RGBA8_SRGB : rhi::TextureFormat::RGBA8_UNORM;
}

} // namespace

Result<ImageData> ImageDecoder::decodeFromFile(std::string_view filePath, int desiredChannels,
                                               bool srgb) {
    if (filePath.empty()) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Image file path cannot be empty"};
    }

    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec)) {
        return Error{.code = ErrorCode::EntityNotFound, .message = "Image file does not exist"};
    }

    int width = 0;
    int height = 0;
    int channels = 0;

    std::string pathStr(filePath);
    stbi_uc* data = stbi_load(pathStr.c_str(), &width, &height, &channels, desiredChannels);
    if (data == nullptr) {
        const char* failureReason = stbi_failure_reason();
        return Error{.code = ErrorCode::ResourceCreationFailed,
                     .message = failureReason ? failureReason : "Failed to decode image file"};
    }

    if (width <= 0 || height <= 0) {
        stbi_image_free(data);
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Decoded image has invalid dimensions (width <= 0 or height <= 0)"};
    }

    const int finalChannels = (desiredChannels > 0) ? desiredChannels : channels;
    const size_t totalBytes = static_cast<size_t>(width) * static_cast<size_t>(height) *
                              static_cast<size_t>(finalChannels);

    ImageData image{};
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.channels = static_cast<uint32_t>(finalChannels);
    image.format = resolveFormat(finalChannels, srgb);

    image.pixels.resize(totalBytes);
    std::memcpy(image.pixels.data(), data, totalBytes);

    stbi_image_free(data);
    return Result<ImageData>::success(std::move(image));
}

Result<ImageData> ImageDecoder::decodeFromMemory(std::span<const uint8_t> buffer,
                                                 int desiredChannels, bool srgb) {
    if (buffer.empty()) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Encoded image buffer cannot be empty"};
    }

    int width = 0;
    int height = 0;
    int channels = 0;

    stbi_uc* data = stbi_load_from_memory(buffer.data(), static_cast<int>(buffer.size()), &width,
                                          &height, &channels, desiredChannels);
    if (data == nullptr) {
        const char* failureReason = stbi_failure_reason();
        return Error{.code = ErrorCode::ResourceCreationFailed,
                     .message =
                         failureReason ? failureReason : "Failed to decode image from memory"};
    }

    if (width <= 0 || height <= 0) {
        stbi_image_free(data);
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Decoded image has invalid dimensions"};
    }

    const int finalChannels = (desiredChannels > 0) ? desiredChannels : channels;
    const size_t totalBytes = static_cast<size_t>(width) * static_cast<size_t>(height) *
                              static_cast<size_t>(finalChannels);

    ImageData image{};
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.channels = static_cast<uint32_t>(finalChannels);
    image.format = resolveFormat(finalChannels, srgb);

    image.pixels.resize(totalBytes);
    std::memcpy(image.pixels.data(), data, totalBytes);

    stbi_image_free(data);
    return Result<ImageData>::success(std::move(image));
}

} // namespace eng::render
