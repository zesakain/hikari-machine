/// @file sampler.h
/// @brief GPU texture sampler state descriptor.
///
/// Purpose:
/// Configures texture filtering, address wrap modes, and depth compare operations.
///
/// Ownership and Lifetime:
/// Descriptors are pure value types passed by const reference during sampler creation.
///
/// Thread-safety:
/// Value type with const accessors; fully thread-safe.
///
/// Performance:
/// Standard layout struct, zero allocation overhead.

#pragma once

#include "rhi/rhi_types.h"

#include <string_view>

namespace eng::rhi {

/// @struct SamplerDesc
/// @brief State descriptor for texture sampler creation.
struct SamplerDesc {
    /// Minification filtering mode.
    Filter minFilter{Filter::Linear};

    /// Magnification filtering mode.
    Filter magFilter{Filter::Linear};

    /// Mipmap level transition filter.
    Filter mipmapMode{Filter::Linear};

    /// Wrap mode for U (horizontal) coordinate.
    SamplerAddressMode addressU{SamplerAddressMode::Repeat};

    /// Wrap mode for V (vertical) coordinate.
    SamplerAddressMode addressV{SamplerAddressMode::Repeat};

    /// Wrap mode for W (depth) coordinate.
    SamplerAddressMode addressW{SamplerAddressMode::Repeat};

    /// Enables anisotropic filtering if supported by hardware capabilities.
    bool anisotropyEnable{false};

    /// Maximum anisotropy clamping limit (typically 1.0 to 16.0).
    float maxAnisotropy{1.0F};

    /// Minimum level-of-detail clamp.
    float minLod{0.0F};

    /// Maximum level-of-detail clamp (e.g. 1000.0F for unconstrained LOD).
    float maxLod{1000.0F};

    /// Mipmap level-of-detail bias.
    float mipLodBias{0.0F};

    /// Enables hardware depth comparison (for shadow map sampling).
    bool compareEnable{false};

    /// Comparison operator applied when compareEnable is true.
    CompareOp compareOp{CompareOp::Always};

    /// Diagnostic name string for graphics debuggers.
    // NOLINTNEXTLINE(readability-redundant-member-init): Required for GCC
    // -Wmissing-field-initializers
    std::string_view debugName{};
};

} // namespace eng::rhi
