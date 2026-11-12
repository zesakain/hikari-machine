#pragma once

#include <cstdint>

namespace eng::render {

/// @enum ToneMapOperator
/// @brief Available tone mapping operators mapping high dynamic range (HDR) radiance values to
/// display-referred dynamic range.
enum class ToneMapOperator : uint8_t {
    /// Fitted ACES curve (Narkowicz fit) providing cinematic contrast and highlight roll-off.
    AcesFitted = 0,

    /// Filmic extended Reinhard curve with white point preservation.
    FilmicReinhard = 1,

    /// Khronos PBR Neutral curve preventing color desaturation, hue shifts, and highlight burnout.
    Neutral = 2,

    /// Linear bypass: no non-linear tonemapping curve applied (clamped to [0, 1]).
    Bypass = 3,
};

/// @struct PostProcessSettings
/// @brief Runtime configuration for post-processing, tone mapping, and bloom.
///
/// Ownership and lifetime: Plain value struct; copied freely across rendering interfaces.
/// Thread-safety: Thread-independent data value.
/// Cost: Zero runtime allocation overhead.
struct PostProcessSettings {
    /// Master toggle for post-processing pipeline. When false, scene renders directly to standard
    /// low dynamic range target.
    bool enabled{false};

    /// Linear photometric exposure multiplier applied prior to tone mapping.
    /// Range: [0.001, 100.0], default: 1.0.
    float exposure{1.0F};

    /// Selected tone reproduction operator. Default: ACES fitted.
    ToneMapOperator toneMapOp{ToneMapOperator::AcesFitted};

    /// Master toggle for bloom bright extraction and multi-scale blur pyramid.
    bool bloomEnabled{true};

    /// Luminance threshold for bright pass extraction.
    /// Range: [0.0, 50.0], default: 1.0.
    float bloomThreshold{1.0F};

    /// Soft-knee transition half-width for bloom extraction.
    /// Range: [0.0, 2.0], default: 0.5.
    float bloomKnee{0.5F};

    /// Scale factor applied to the blurred bloom pyramid during final composite.
    /// Range: [0.0, 1.0], default: 0.04.
    float bloomIntensity{0.04F};

    /// 9-tap tent blur reconstruction filter radius.
    /// Range: [0.5, 4.0], default: 1.0.
    float bloomFilterRadius{1.0F};
};

} // namespace eng::render
