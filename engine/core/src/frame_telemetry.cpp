/// @file frame_telemetry.cpp
/// @brief Implementation of FrameTelemetryTracker for live frame and CPU performance measurements.

#include "core/time/frame_telemetry.h"

namespace eng {

void FrameTelemetryTracker::recordFrame(Duration frameDuration, Duration cpuDuration,
                                        std::optional<Duration> gpuDuration) noexcept {
    float deltaSec = static_cast<float>(frameDuration.toSeconds());

    // Guard against zero, negative, NaN, or non-finite durations
    if (std::isnan(deltaSec) || std::isinf(deltaSec) || deltaSec < kMinValidDeltaSeconds) {
        deltaSec = kMinValidDeltaSeconds;
    } else if (deltaSec > kMaxValidDeltaSeconds) {
        deltaSec = kMaxValidDeltaSeconds;
    }

    frameTimeMs_ = deltaSec * 1000.0F;
    instantaneousFps_ = 1.0F / deltaSec;

    float cpuSec = static_cast<float>(cpuDuration.toSeconds());
    if (std::isnan(cpuSec) || std::isinf(cpuSec) || cpuSec < 0.0F) {
        cpuSec = 0.0F;
    } else if (cpuSec > kMaxValidDeltaSeconds) {
        cpuSec = kMaxValidDeltaSeconds;
    }
    cpuTimeMs_ = cpuSec * 1000.0F;

    if (gpuDuration.has_value()) {
        hasGpuTiming_ = true;
        float gpuSec = static_cast<float>(gpuDuration->toSeconds());
        if (std::isnan(gpuSec) || std::isinf(gpuSec) || gpuSec < 0.0F) {
            gpuSec = 0.0F;
        }
        gpuTimeMs_ = gpuSec * 1000.0F;
    } else {
        hasGpuTiming_ = false;
        gpuTimeMs_ = 0.0F;
    }

    // Insert into circular history ring buffers
    fpsHistory_[historyIndex_] = instantaneousFps_;
    frameTimeHistory_[historyIndex_] = frameTimeMs_;
    cpuTimeHistory_[historyIndex_] = cpuTimeMs_;
    gpuTimeHistory_[historyIndex_] = gpuTimeMs_;

    historyIndex_ = (historyIndex_ + 1) % kHistoryCapacity;
    if (sampleCount_ < kHistoryCapacity) {
        ++sampleCount_;
    }
    ++totalFrames_;

    // Compute rolling window min, max, average across accumulated samples
    float sumFps = 0.0F;
    float sumFrameMs = 0.0F;
    float sumCpuMs = 0.0F;
    float sumGpuMs = 0.0F;
    float curMinFps = fpsHistory_[0];
    float curMaxFps = fpsHistory_[0];
    float curMinFrameMs = frameTimeHistory_[0];
    float curMaxFrameMs = frameTimeHistory_[0];

    for (size_t i = 0; i < sampleCount_; ++i) {
        const float f = fpsHistory_[i];
        const float ft = frameTimeHistory_[i];
        const float c = cpuTimeHistory_[i];
        const float g = gpuTimeHistory_[i];

        sumFps += f;
        sumFrameMs += ft;
        sumCpuMs += c;
        sumGpuMs += g;

        if (f < curMinFps) {
            curMinFps = f;
        }
        if (f > curMaxFps) {
            curMaxFps = f;
        }
        if (ft < curMinFrameMs) {
            curMinFrameMs = ft;
        }
        if (ft > curMaxFrameMs) {
            curMaxFrameMs = ft;
        }
    }

    const float countF = static_cast<float>(sampleCount_);
    averageFps_ = sumFps / countF;
    averageFrameTimeMs_ = sumFrameMs / countF;
    averageCpuTimeMs_ = sumCpuMs / countF;
    averageGpuTimeMs_ = sumGpuMs / countF;
    minFps_ = curMinFps;
    maxFps_ = curMaxFps;
    minFrameTimeMs_ = curMinFrameMs;
    maxFrameTimeMs_ = curMaxFrameMs;
}

void FrameTelemetryTracker::reset() noexcept {
    instantaneousFps_ = 0.0F;
    averageFps_ = 0.0F;
    minFps_ = 0.0F;
    maxFps_ = 0.0F;

    frameTimeMs_ = 0.0F;
    averageFrameTimeMs_ = 0.0F;
    minFrameTimeMs_ = 0.0F;
    maxFrameTimeMs_ = 0.0F;

    cpuTimeMs_ = 0.0F;
    averageCpuTimeMs_ = 0.0F;

    hasGpuTiming_ = false;
    gpuTimeMs_ = 0.0F;
    averageGpuTimeMs_ = 0.0F;

    fpsHistory_.fill(0.0F);
    frameTimeHistory_.fill(0.0F);
    cpuTimeHistory_.fill(0.0F);
    gpuTimeHistory_.fill(0.0F);

    historyIndex_ = 0;
    sampleCount_ = 0;
    totalFrames_ = 0;
}

} // namespace eng
