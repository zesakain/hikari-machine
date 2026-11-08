/// @file shader.h
/// @brief Shader module descriptors and SPIR-V binary validation for RHI.
///
/// Purpose:
/// Defines hardware-neutral descriptors for compiling SPIR-V bytecode into GPU
/// shader modules. Encapsulates shader stage, entry point, and validation rules.
///
/// Contract:
/// - spirvCode must be valid 32-bit word-aligned SPIR-V binary starting with 0x07230203.
/// - Minimum SPIR-V header is 5 words (20 bytes).
/// - stage must specify exactly one valid shader stage flag (Vertex, Fragment, or Compute).
///
/// Ownership and Lifetime:
/// Descriptors are pure value types passed by const reference during shader creation.
/// The caller retains ownership of the underlying SPIR-V memory buffer during createShader().
/// Returned ShaderHandle is generational and owned by the caller until destroyShader().
///
/// Thread-safety:
/// Value types; thread-safe.
///
/// Performance:
/// Fits in registers/stack; zero heap allocation.

#pragma once

#include "core/error.h"
#include "core/result.h"
#include "rhi/rhi_types.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace eng::rhi {

/// Standard SPIR-V binary magic number (little-endian: 0x07230203).
inline constexpr uint32_t kSpirvMagicNumber = 0x07230203U;

/// Minimum valid SPIR-V binary length in 32-bit words (magic, version, generator, bound, schema).
inline constexpr size_t kSpirvMinWordCount = 5;

/// @struct ShaderDesc
/// @brief Specification for compiling a GPU shader module.
struct ShaderDesc {
    /// Target execution stage (Vertex, Fragment, Compute).
    ShaderStageFlags stage{ShaderStageFlags::None};

    /// SPIR-V bytecode as a contiguous span of 32-bit words.
    std::span<const uint32_t> spirvCode{};

    /// Function name of the entry point (default "main").
    std::string_view entryPoint{"main"};

    /// Optional debug identification label.
    std::string_view debugName{};
};

/// Validates a ShaderDesc according to SPIR-V specification constraints.
///
/// @param desc Shader descriptor to validate.
/// @return Success or error indicating validation failure reason.
[[nodiscard]] constexpr Result<void> validateShaderDesc(const ShaderDesc& desc) noexcept {
    if (desc.stage == ShaderStageFlags::None) {
        return Error{.code = ErrorCode::InvalidArgument, .message = "Shader stage cannot be None"};
    }

    if (desc.spirvCode.empty()) {
        return Error{.code = ErrorCode::InvalidArgument, .message = "SPIR-V code span is empty"};
    }

    if (desc.spirvCode.size() < kSpirvMinWordCount) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "SPIR-V binary is smaller than 5-word header"};
    }

    if (desc.spirvCode[0] != kSpirvMagicNumber) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Invalid SPIR-V magic number; expected 0x07230203"};
    }

    if (desc.entryPoint.empty()) {
        return Error{.code = ErrorCode::InvalidArgument,
                     .message = "Shader entry point name cannot be empty"};
    }

    return Result<void>::success();
}

} // namespace eng::rhi
