/// @file image.h
/// @brief CPU-side decoded image data and decoder interface.
///
/// Purpose:
/// Represents host-memory decoded pixel bitmaps (PNG, JPEG, BMP) with validation,
/// channel normalization, and deterministic deallocation.
///
/// Ownership and Lifetime:
/// ImageData owns its raw pixel storage buffer (std::vector<uint8_t>).
///
/// Thread-safety:
/// Read-only access is thread-safe. Decoder functions are pure and thread-safe.
///
/// Performance:
/// Single allocation for decoded pixels. Zero unnecessary intermediate buffer copies.

#pragma once

#include "core/error.h"
#include "core/result.h"
#include "rhi/rhi_types.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace eng::render {

/// @struct ImageData
/// @brief Decoded CPU bitmap containing pixel data, format, and dimensions.
struct ImageData {
    uint32_t width{0};
    uint32_t height{0};
    uint32_t channels{0};
    rhi::TextureFormat format{rhi::TextureFormat::RGBA8_UNORM};
    std::vector<uint8_t> pixels{};

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return width > 0 && height > 0 && channels > 0 && !pixels.empty();
    }

    [[nodiscard]] size_t sizeInBytes() const noexcept { return pixels.size(); }
};

/// @class ImageDecoder
/// @brief Hardware-independent image decoding interface for PNG, JPEG, and BMP files.
class ImageDecoder {
public:
    /// @brief Decodes an image file from disk.
    ///
    /// @param filePath Path to the image file on disk.
    /// @param desiredChannels Number of channels to output (default: 4 for RGBA).
    /// @param srgb Whether the format should be tagged as sRGB or linear UNORM.
    /// @return The decoded ImageData or an error code.
    [[nodiscard]] static Result<ImageData>
    decodeFromFile(std::string_view filePath, int desiredChannels = 4, bool srgb = false);

    /// @brief Decodes an encoded image buffer from memory (e.g. embedded glTF buffers).
    ///
    /// @param buffer Encoded image bytes in memory.
    /// @param desiredChannels Number of channels to output (default: 4 for RGBA).
    /// @param srgb Whether the format should be tagged as sRGB or linear UNORM.
    /// @return The decoded ImageData or an error code.
    [[nodiscard]] static Result<ImageData>
    decodeFromMemory(std::span<const uint8_t> buffer, int desiredChannels = 4, bool srgb = false);
};

} // namespace eng::render
