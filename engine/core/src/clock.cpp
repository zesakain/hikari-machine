/// @file clock.cpp
/// @brief Clock implementation.

#include <core/time/clock.h>

namespace eng {

void Clock::tick(Duration realDelta) noexcept {
    if (realDelta < Duration::zero()) {
        realDelta = Duration::zero(); // Protect against negative time travel
    }

    realDeltaTime_ = realDelta;
    totalRealTime_ += realDelta;

    if (isPaused_) {
        deltaTime_ = Duration::zero();
    } else {
        deltaTime_ = realDelta * static_cast<double>(timeScale_);
        totalTime_ += deltaTime_;
    }

    hasTicked_ = true;
}

void Clock::tick(Timestamp now) noexcept {
    if (!hasTicked_) {
        lastTimestamp_ = now;
        hasTicked_ = true;
        deltaTime_ = Duration::zero();
        realDeltaTime_ = Duration::zero();
        return;
    }

    const Duration elapsed = now - lastTimestamp_;
    lastTimestamp_ = now;
    tick(elapsed);
}

void Clock::tick() noexcept {
    tick(Timestamp::now());
}

void Clock::setTimeScale(float scale) noexcept {
    // Policy: Negative scaling is clamped to 0.0f
    timeScale_ = (scale < 0.0F) ? 0.0F : scale;
}

void Clock::reset() noexcept {
    lastTimestamp_ = Timestamp{};
    deltaTime_ = Duration::zero();
    realDeltaTime_ = Duration::zero();
    totalTime_ = Duration::zero();
    totalRealTime_ = Duration::zero();
    timeScale_ = 1.0F;
    isPaused_ = false;
    hasTicked_ = false;
}

void Clock::reset(Timestamp startTime) noexcept {
    reset();
    lastTimestamp_ = startTime;
    hasTicked_ = true;
}

} // namespace eng
