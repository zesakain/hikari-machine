/// @file clock.h
/// @brief Independent simulation and presentation clock with scaling and pause capabilities.
///
/// Purpose:
/// Manages frame-to-frame delta time and accumulated runtime for independent simulation domains.
/// Designed to instantiate separate clocks for World Simulation, Action/Gameplay, and UI.
///
/// Decoupling and Independence:
/// Each Clock instance operates completely independently. Pausing or time-scaling one clock
/// has zero effect on any other clock. There is NO global singleton clock.
///
/// Time Scaling and Slow-Motion:
/// - Real time (realDeltaTime, totalRealTime) strictly reflects elapsed wall/monotonic time.
/// - Scaled time (deltaTime, totalTime) reflects simulation time adjusted by timeScale.
/// - Scaling policy: Negative time scales are clamped to 0.0f to preserve forward time
/// monotonicity.
/// - When paused, deltaTime is Duration::zero(), but realDeltaTime continues tracking real elapsed
/// time.
///
/// Thread-safety:
/// Individual instances are not thread-safe. Each clock must be ticked by its owning thread/phase.
///
/// Performance:
/// Pure arithmetic updates. Zero allocations, hot-path safe.

#pragma once

#include <core/time/duration.h>
#include <core/time/timestamp.h>

namespace eng {

/// @class Clock
/// @brief Independent timekeeper supporting scaling, pause/resume, and real vs. scaled metrics.
class Clock {
public:
    /// @brief Constructs a clock with 1.0f time scale and zero accumulated time.
    constexpr Clock() noexcept = default;

    /// @brief Advances clock by an explicit real delta duration (deterministic / offline tick).
    ///
    /// @param realDelta Real-time duration elapsed since last tick.
    void tick(Duration realDelta) noexcept;

    /// @brief Advances clock by comparing provided timestamp against last tick timestamp.
    ///
    /// On the first tick, deltaTime is Duration::zero().
    ///
    /// @param now Current monotonic frame timestamp.
    void tick(Timestamp now) noexcept;

    /// @brief Advances clock by sampling Timestamp::now().
    void tick() noexcept;

    /// @brief Resets all accumulated time, deltas, and state to initial values.
    void reset() noexcept;

    /// @brief Resets clock and sets initial reference timestamp.
    ///
    /// @param startTime Initial reference timestamp.
    void reset(Timestamp startTime) noexcept;

    /// @brief Pauses the clock. Scaled deltaTime becomes zero until resumed.
    void pause() noexcept { isPaused_ = true; }

    /// @brief Resumes the clock from pause.
    void resume() noexcept { isPaused_ = false; }

    /// @brief Sets the pause state.
    void setPaused(bool paused) noexcept { isPaused_ = paused; }

    /// @brief Returns true if clock is currently paused.
    [[nodiscard]] constexpr bool isPaused() const noexcept { return isPaused_; }

    /// @brief Sets the simulation time scale.
    ///
    /// Policy: Values < 0.0f are clamped to 0.0f to guarantee forward time monotonicity.
    ///
    /// @param scale Time multiplier (1.0 = normal, 0.5 = half speed, 0.0 = frozen).
    void setTimeScale(float scale) noexcept;

    /// @brief Returns the current time scale multiplier.
    [[nodiscard]] constexpr float timeScale() const noexcept { return timeScale_; }

    /// @brief Scaled delta time for the current frame (returns zero if paused).
    [[nodiscard]] constexpr Duration deltaTime() const noexcept { return deltaTime_; }

    /// @brief Real, unscaled monotonic delta time for the current frame.
    [[nodiscard]] constexpr Duration realDeltaTime() const noexcept { return realDeltaTime_; }

    /// @brief Total accumulated scaled simulation time since start or last reset.
    [[nodiscard]] constexpr Duration totalTime() const noexcept { return totalTime_; }

    /// @brief Total accumulated real monotonic time since start or last reset.
    [[nodiscard]] constexpr Duration totalRealTime() const noexcept { return totalRealTime_; }

private:
    Timestamp lastTimestamp_;
    Duration deltaTime_{Duration::zero()};
    Duration realDeltaTime_{Duration::zero()};
    Duration totalTime_{Duration::zero()};
    Duration totalRealTime_{Duration::zero()};
    float timeScale_{1.0F};
    bool isPaused_{false};
    bool hasTicked_{false};
};

} // namespace eng
