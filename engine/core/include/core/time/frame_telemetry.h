/// @file frame_telemetry.h
/// @brief Monotonic real-time performance telemetry tracker for frame duration, CPU workload, and
/// FPS.
///
/// Purpose:
/// Measures and tracks real application performance telemetry: instantaneous and rolling average
/// FPS, CPU frame execution duration, and bounded historical sample buffers for telemetry
/// visualization.
///
/// Contract:
/// - frameDuration: total elapsed monotonic time between frames.
/// - cpuDuration: elapsed monotonic time spent executing CPU workloads during the frame.
/// - Clamps invalid, zero, or non-positive deltas to avoid division by zero.
///
/// Ownership and lifetime:
/// Value type with static array storage. Owned by the caller (main application loop or
/// EditorEngine).
///
/// Thread-safety:
/// Mutable methods are intended for the main thread during frame updates; const query methods are
/// safe to call concurrently during UI panel rendering.
///
/// Cost:
/// Zero heap allocations. Fixed stack/member memory footprint. Permitted in the engine hot path.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <core/time/duration.h>
#include <core/time/timestamp.h>
#include <cstdint>
#include <optional>

namespace eng {

/// @class FrameTelemetryTracker
/// @brief Monotonic high-resolution frame time, CPU execution duration, and FPS telemetry tracker.
class FrameTelemetryTracker {
public:
    static constexpr size_t kHistoryCapacity = 120;
    static constexpr float kMinValidDeltaSeconds = 0.0001F; // 10,000 FPS ceiling
    static constexpr float kMaxValidDeltaSeconds = 1.0F;    // 1 FPS floor / 1 second clamp

    constexpr FrameTelemetryTracker() noexcept = default;

    /// @brief Records a completed frame measurement.
    ///
    /// @param frameDuration Elapsed monotonic duration between frames.
    /// @param cpuDuration Elapsed monotonic duration spent executing CPU tasks.
    /// @param gpuDuration Optional hardware GPU duration (if timestamp queries supported).
    void recordFrame(Duration frameDuration, Duration cpuDuration,
                     std::optional<Duration> gpuDuration = std::nullopt) noexcept;

    /// @brief Clears all sample buffers, min/max trackers, and rolling averages.
    void reset() noexcept;

    // --- FPS Telemetry ---
    [[nodiscard]] float instantaneousFps() const noexcept { return instantaneousFps_; }
    [[nodiscard]] float averageFps() const noexcept { return averageFps_; }
    [[nodiscard]] float minFps() const noexcept { return minFps_; }
    [[nodiscard]] float maxFps() const noexcept { return maxFps_; }

    // --- Frame Time Telemetry (milliseconds) ---
    [[nodiscard]] float frameTimeMs() const noexcept { return frameTimeMs_; }
    [[nodiscard]] float averageFrameTimeMs() const noexcept { return averageFrameTimeMs_; }
    [[nodiscard]] float minFrameTimeMs() const noexcept { return minFrameTimeMs_; }
    [[nodiscard]] float maxFrameTimeMs() const noexcept { return maxFrameTimeMs_; }

    // --- CPU Workload Telemetry (milliseconds) ---
    [[nodiscard]] float cpuTimeMs() const noexcept { return cpuTimeMs_; }
    [[nodiscard]] float averageCpuTimeMs() const noexcept { return averageCpuTimeMs_; }

    // --- GPU Hardware Telemetry ---
    [[nodiscard]] bool hasGpuTiming() const noexcept { return hasGpuTiming_; }
    [[nodiscard]] float gpuTimeMs() const noexcept { return gpuTimeMs_; }
    [[nodiscard]] float averageGpuTimeMs() const noexcept { return averageGpuTimeMs_; }

    // --- Bounded Ring Buffers (for ImGui PlotLines) ---
    [[nodiscard]] const std::array<float, kHistoryCapacity>& fpsHistory() const noexcept {
        return fpsHistory_;
    }
    [[nodiscard]] const std::array<float, kHistoryCapacity>& frameTimeHistory() const noexcept {
        return frameTimeHistory_;
    }
    [[nodiscard]] const std::array<float, kHistoryCapacity>& cpuTimeHistory() const noexcept {
        return cpuTimeHistory_;
    }
    [[nodiscard]] const std::array<float, kHistoryCapacity>& gpuTimeHistory() const noexcept {
        return gpuTimeHistory_;
    }
    [[nodiscard]] size_t historyIndex() const noexcept { return historyIndex_; }
    [[nodiscard]] size_t sampleCount() const noexcept { return sampleCount_; }
    [[nodiscard]] uint64_t totalFrames() const noexcept { return totalFrames_; }

private:
    float instantaneousFps_{0.0F};
    float averageFps_{0.0F};
    float minFps_{0.0F};
    float maxFps_{0.0F};

    float frameTimeMs_{0.0F};
    float averageFrameTimeMs_{0.0F};
    float minFrameTimeMs_{0.0F};
    float maxFrameTimeMs_{0.0F};

    float cpuTimeMs_{0.0F};
    float averageCpuTimeMs_{0.0F};

    bool hasGpuTiming_{false};
    float gpuTimeMs_{0.0F};
    float averageGpuTimeMs_{0.0F};

    std::array<float, kHistoryCapacity> fpsHistory_{};
    std::array<float, kHistoryCapacity> frameTimeHistory_{};
    std::array<float, kHistoryCapacity> cpuTimeHistory_{};
    std::array<float, kHistoryCapacity> gpuTimeHistory_{};

    size_t historyIndex_{0};
    size_t sampleCount_{0};
    uint64_t totalFrames_{0};
};

} // namespace eng
