/// @file time_test.cpp
/// @brief Comprehensive unit tests for Duration, Timestamp, Clock, and Budget.

#include <core/time.h>
#include <cstdint>

namespace {

#define TEST_CHECK(condition)                                                                      \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

int testDurationConversionsAndArithmetic() {
    // Zero, positive, negative factories
    const eng::Duration dZero = eng::Duration::zero();
    TEST_CHECK(dZero.isZero());
    TEST_CHECK(!dZero.isPositive());
    TEST_CHECK(!dZero.isNegative());
    TEST_CHECK(dZero.asNanoseconds() == 0);

    const eng::Duration dSec = eng::Duration::fromSeconds(1.5);
    TEST_CHECK(dSec.asNanoseconds() == 1'500'000'000LL);
    TEST_CHECK(dSec.asMicroseconds() == 1'500'000LL);
    TEST_CHECK(dSec.asMilliseconds() == 1'500LL);
    TEST_CHECK(dSec.toSeconds() == 1.5);
    TEST_CHECK(dSec.isPositive());

    const eng::Duration dMs = eng::Duration::fromMilliseconds(250);
    TEST_CHECK(dMs.asNanoseconds() == 250'000'000LL);

    const eng::Duration dUs = eng::Duration::fromMicroseconds(500);
    TEST_CHECK(dUs.asNanoseconds() == 500'000LL);

    const eng::Duration dNs = eng::Duration::fromNanoseconds(42);
    TEST_CHECK(dNs.asNanoseconds() == 42);

    // Unary minus
    const eng::Duration dNeg = -dSec;
    TEST_CHECK(dNeg.isNegative());
    TEST_CHECK(dNeg.asNanoseconds() == -1'500'000'000LL);

    // Addition and Subtraction
    const eng::Duration dSum = dSec + dMs;
    TEST_CHECK(dSum.asMilliseconds() == 1750LL);

    const eng::Duration dDiff = dSec - dMs;
    TEST_CHECK(dDiff.asMilliseconds() == 1250LL);

    // Multiplication and Division
    const eng::Duration dScaledInt = dMs * 4;
    TEST_CHECK(dScaledInt.asMilliseconds() == 1000LL);

    const eng::Duration dScaledFloat = dMs * 0.5;
    TEST_CHECK(dScaledFloat.asMilliseconds() == 125LL);

    const eng::Duration dDivided = dSec / 3;
    TEST_CHECK(dDivided.asMilliseconds() == 500LL);

    // Ratio division
    const double ratio = dSec / dMs;
    TEST_CHECK(ratio == 6.0);

    // Compound assignment
    eng::Duration dMut = eng::Duration::fromMilliseconds(100);
    dMut += eng::Duration::fromMilliseconds(50);
    TEST_CHECK(dMut.asMilliseconds() == 150LL);
    dMut -= eng::Duration::fromMilliseconds(50);
    TEST_CHECK(dMut.asMilliseconds() == 100LL);
    dMut *= 2;
    TEST_CHECK(dMut.asMilliseconds() == 200LL);
    dMut /= 4;
    TEST_CHECK(dMut.asMilliseconds() == 50LL);

    // Comparisons
    TEST_CHECK(dMs < dSec);
    TEST_CHECK(dSec > dMs);
    TEST_CHECK(dMs <= dSec);
    TEST_CHECK(dSec >= dMs);
    TEST_CHECK(dSec == eng::Duration::fromMilliseconds(1500));
    TEST_CHECK(dSec != dMs);

    return 0;
}

int testDurationOverflowAndBoundaries() {
    const eng::Duration dMax = eng::Duration::max();
    TEST_CHECK(dMax.isPositive());

    const eng::Duration dMin = eng::Duration::min();
    TEST_CHECK(dMin.isNegative());

    // Overflow clamping in factories
    const eng::Duration dHugeMs = eng::Duration::fromMilliseconds(INT64_MAX);
    TEST_CHECK(dHugeMs == dMax);

    const eng::Duration dHugeUs = eng::Duration::fromMicroseconds(INT64_MAX);
    TEST_CHECK(dHugeUs == dMax);

    const eng::Duration dHugeSec = eng::Duration::fromSeconds(1e25);
    TEST_CHECK(dHugeSec == dMax);

    const eng::Duration dMinSec = eng::Duration::fromSeconds(-1e25);
    TEST_CHECK(dMinSec == dMin);

    return 0;
}

int testTimestampOperations() {
    // Monotonic now() sampling
    const eng::Timestamp t1 = eng::Timestamp::now();
    const eng::Timestamp t2 = eng::Timestamp::now();
    TEST_CHECK(t2 >= t1);

    // Deterministic timestamp factories
    const eng::Timestamp tA = eng::Timestamp::fromNanoseconds(1'000'000'000LL);
    const eng::Timestamp tB = eng::Timestamp::fromNanoseconds(2'500'000'000LL);

    // Subtraction produces duration
    const eng::Duration elapsed = tB - tA;
    TEST_CHECK(elapsed == eng::Duration::fromMilliseconds(1500));

    // Addition with Duration
    const eng::Timestamp tC = tA + eng::Duration::fromMilliseconds(500);
    TEST_CHECK(tC.nanosecondsSinceEpoch() == 1'500'000'000LL);
    TEST_CHECK(tC > tA);
    TEST_CHECK(tC < tB);

    // Subtraction with Duration
    const eng::Timestamp tD = tB - eng::Duration::fromSeconds(1.0);
    TEST_CHECK(tD.nanosecondsSinceEpoch() == 1'500'000'000LL);
    TEST_CHECK(tD == tC);

    return 0;
}

int testClockTickingAndReset() {
    eng::Clock clock;

    TEST_CHECK(clock.deltaTime().isZero());
    TEST_CHECK(clock.realDeltaTime().isZero());
    TEST_CHECK(clock.totalTime().isZero());
    TEST_CHECK(clock.totalRealTime().isZero());
    TEST_CHECK(!clock.isPaused());
    TEST_CHECK(clock.timeScale() == 1.0F);

    // 1. Explicit deterministic delta tick
    const eng::Duration dt1 = eng::Duration::fromMilliseconds(16);
    clock.tick(dt1);
    TEST_CHECK(clock.deltaTime() == dt1);
    TEST_CHECK(clock.realDeltaTime() == dt1);
    TEST_CHECK(clock.totalTime() == dt1);
    TEST_CHECK(clock.totalRealTime() == dt1);

    const eng::Duration dt2 = eng::Duration::fromMilliseconds(16);
    clock.tick(dt2);
    TEST_CHECK(clock.deltaTime() == dt2);
    TEST_CHECK(clock.totalTime() == eng::Duration::fromMilliseconds(32));
    TEST_CHECK(clock.totalRealTime() == eng::Duration::fromMilliseconds(32));

    // 2. Reset
    clock.reset();
    TEST_CHECK(clock.deltaTime().isZero());
    TEST_CHECK(clock.realDeltaTime().isZero());
    TEST_CHECK(clock.totalTime().isZero());
    TEST_CHECK(clock.totalRealTime().isZero());
    TEST_CHECK(clock.timeScale() == 1.0F);
    TEST_CHECK(!clock.isPaused());

    // 3. Timestamp-based ticking
    const eng::Timestamp t0 = eng::Timestamp::fromNanoseconds(100'000'000LL);
    clock.tick(t0); // First tick sets reference time; delta is 0
    TEST_CHECK(clock.deltaTime().isZero());

    const eng::Timestamp t1 = eng::Timestamp::fromNanoseconds(116'000'000LL);
    clock.tick(t1); // Second tick computes delta 16ms
    TEST_CHECK(clock.deltaTime() == eng::Duration::fromMilliseconds(16));
    TEST_CHECK(clock.totalTime() == eng::Duration::fromMilliseconds(16));

    return 0;
}

int testIndependentClocks() {
    eng::Clock worldClock;
    eng::Clock actionClock;
    eng::Clock uiClock;

    // Mutate actionClock to slow motion (0.5x)
    actionClock.setTimeScale(0.5F);
    TEST_CHECK(actionClock.timeScale() == 0.5F);
    TEST_CHECK(worldClock.timeScale() == 1.0F);
    TEST_CHECK(uiClock.timeScale() == 1.0F);

    // Pause worldClock
    worldClock.pause();
    TEST_CHECK(worldClock.isPaused());
    TEST_CHECK(!actionClock.isPaused());
    TEST_CHECK(!uiClock.isPaused());

    // Advance all with same real frame delta (20ms)
    const eng::Duration frameDt = eng::Duration::fromMilliseconds(20);
    worldClock.tick(frameDt);
    actionClock.tick(frameDt);
    uiClock.tick(frameDt);

    // World clock: paused -> deltaTime = 0, realDeltaTime = 20ms
    TEST_CHECK(worldClock.deltaTime().isZero());
    TEST_CHECK(worldClock.realDeltaTime() == frameDt);

    // Action clock: 0.5x -> deltaTime = 10ms, realDeltaTime = 20ms
    TEST_CHECK(actionClock.deltaTime() == eng::Duration::fromMilliseconds(10));
    TEST_CHECK(actionClock.realDeltaTime() == frameDt);

    // UI clock: 1.0x -> deltaTime = 20ms, realDeltaTime = 20ms
    TEST_CHECK(uiClock.deltaTime() == frameDt);
    TEST_CHECK(uiClock.realDeltaTime() == frameDt);

    return 0;
}

int testTimeScalingAndNegativePolicy() {
    eng::Clock clock;

    // Normal scale 1.0x
    clock.tick(eng::Duration::fromMilliseconds(100));
    TEST_CHECK(clock.deltaTime() == eng::Duration::fromMilliseconds(100));
    TEST_CHECK(clock.realDeltaTime() == eng::Duration::fromMilliseconds(100));

    // Double speed 2.0x
    clock.setTimeScale(2.0F);
    clock.tick(eng::Duration::fromMilliseconds(100));
    TEST_CHECK(clock.deltaTime() == eng::Duration::fromMilliseconds(200));
    TEST_CHECK(clock.realDeltaTime() == eng::Duration::fromMilliseconds(100));
    TEST_CHECK(clock.totalTime() == eng::Duration::fromMilliseconds(300));
    TEST_CHECK(clock.totalRealTime() == eng::Duration::fromMilliseconds(200));

    // Zero scale (frozen simulation, real time advances)
    clock.setTimeScale(0.0F);
    clock.tick(eng::Duration::fromMilliseconds(100));
    TEST_CHECK(clock.deltaTime().isZero());
    TEST_CHECK(clock.realDeltaTime() == eng::Duration::fromMilliseconds(100));
    TEST_CHECK(clock.totalTime() == eng::Duration::fromMilliseconds(300));
    TEST_CHECK(clock.totalRealTime() == eng::Duration::fromMilliseconds(300));

    // Negative scale policy: clamped to 0.0f
    clock.setTimeScale(-1.5F);
    TEST_CHECK(clock.timeScale() == 0.0F);
    clock.tick(eng::Duration::fromMilliseconds(100));
    TEST_CHECK(clock.deltaTime().isZero());
    TEST_CHECK(clock.realDeltaTime() == eng::Duration::fromMilliseconds(100));

    // Negative real delta tick protection: clamped to zero
    clock.setTimeScale(1.0F);
    clock.tick(eng::Duration::fromMilliseconds(-50));
    TEST_CHECK(clock.deltaTime().isZero());
    TEST_CHECK(clock.realDeltaTime().isZero());

    return 0;
}

int testPauseAndResume() {
    eng::Clock clock;

    clock.tick(eng::Duration::fromMilliseconds(16));
    TEST_CHECK(clock.deltaTime() == eng::Duration::fromMilliseconds(16));

    // Pause
    clock.pause();
    TEST_CHECK(clock.isPaused());

    clock.tick(eng::Duration::fromMilliseconds(16));
    TEST_CHECK(clock.deltaTime().isZero());
    TEST_CHECK(clock.realDeltaTime() == eng::Duration::fromMilliseconds(16));
    TEST_CHECK(clock.totalTime() == eng::Duration::fromMilliseconds(16));
    TEST_CHECK(clock.totalRealTime() == eng::Duration::fromMilliseconds(32));

    // Resume
    clock.resume();
    TEST_CHECK(!clock.isPaused());

    clock.tick(eng::Duration::fromMilliseconds(16));
    TEST_CHECK(clock.deltaTime() == eng::Duration::fromMilliseconds(16));
    TEST_CHECK(clock.totalTime() == eng::Duration::fromMilliseconds(32));
    TEST_CHECK(clock.totalRealTime() == eng::Duration::fromMilliseconds(48));

    return 0;
}

int testBudgetAccounting() {
    const eng::Timestamp tStart = eng::Timestamp::fromNanoseconds(1'000'000LL); // 1 ms
    const eng::Duration limit = eng::Duration::fromMicroseconds(500);           // 0.5 ms budget
    const eng::Budget budget(limit, tStart);

    TEST_CHECK(budget.limit() == limit);
    TEST_CHECK(budget.startTime() == tStart);

    // Initial check (at tStart)
    TEST_CHECK(budget.elapsed(tStart).isZero());
    TEST_CHECK(budget.remaining(tStart) == limit);
    TEST_CHECK(budget.remainingMicroseconds(tStart) == 500LL);
    TEST_CHECK(!budget.isExpired(tStart));

    // Partial elapsed (200 us later)
    const eng::Timestamp tMid = tStart + eng::Duration::fromMicroseconds(200);
    TEST_CHECK(budget.elapsed(tMid) == eng::Duration::fromMicroseconds(200));
    TEST_CHECK(budget.remaining(tMid) == eng::Duration::fromMicroseconds(300));
    TEST_CHECK(budget.remainingMicroseconds(tMid) == 300LL);
    TEST_CHECK(!budget.isExpired(tMid));

    // Exact limit reached (500 us later)
    const eng::Timestamp tLimit = tStart + eng::Duration::fromMicroseconds(500);
    TEST_CHECK(budget.elapsed(tLimit) == limit);
    TEST_CHECK(budget.remaining(tLimit).isZero());
    TEST_CHECK(budget.remainingMicroseconds(tLimit) == 0LL);
    TEST_CHECK(budget.isExpired(tLimit));

    // Over-budget (700 us later)
    const eng::Timestamp tOver = tStart + eng::Duration::fromMicroseconds(700);
    TEST_CHECK(budget.elapsed(tOver) == eng::Duration::fromMicroseconds(700));
    TEST_CHECK(budget.remaining(tOver).isZero()); // Remaining does not underflow to negative
    TEST_CHECK(budget.remainingMicroseconds(tOver) == 0LL);
    TEST_CHECK(budget.isExpired(tOver));

    // Factory creation
    const eng::Budget bFromUs = eng::Budget::fromMicroseconds(250, tStart);
    TEST_CHECK(bFromUs.limit() == eng::Duration::fromMicroseconds(250));

    const eng::Budget bFromMs = eng::Budget::fromMilliseconds(2, tStart);
    TEST_CHECK(bFromMs.limit() == eng::Duration::fromMilliseconds(2));

    // Negative limit clamped to zero
    const eng::Budget bNeg(eng::Duration::fromMicroseconds(-100), tStart);
    TEST_CHECK(bNeg.limit().isZero());
    TEST_CHECK(bNeg.isExpired(tStart));

    return 0;
}

int testFrameTelemetryBasic() {
    eng::FrameTelemetryTracker tracker;
    TEST_CHECK(tracker.totalFrames() == 0);
    TEST_CHECK(tracker.sampleCount() == 0);
    TEST_CHECK(tracker.instantaneousFps() == 0.0F);

    // 60 FPS: 16.666 ms frame, 4 ms CPU
    const auto frameTime = eng::Duration::fromMicroseconds(16666);
    const auto cpuTime = eng::Duration::fromMilliseconds(4);

    tracker.recordFrame(frameTime, cpuTime);
    TEST_CHECK(tracker.totalFrames() == 1);
    TEST_CHECK(tracker.sampleCount() == 1);
    TEST_CHECK(tracker.historyIndex() == 1);

    // Instantaneous FPS ~ 60.0
    TEST_CHECK(tracker.instantaneousFps() >= 59.9F && tracker.instantaneousFps() <= 60.1F);
    TEST_CHECK(tracker.averageFps() == tracker.instantaneousFps());
    TEST_CHECK(tracker.minFps() == tracker.instantaneousFps());
    TEST_CHECK(tracker.maxFps() == tracker.instantaneousFps());

    // Frame times in ms
    TEST_CHECK(tracker.frameTimeMs() >= 16.6F && tracker.frameTimeMs() <= 16.7F);
    TEST_CHECK(tracker.averageFrameTimeMs() == tracker.frameTimeMs());
    TEST_CHECK(tracker.cpuTimeMs() == 4.0F);
    TEST_CHECK(tracker.averageCpuTimeMs() == 4.0F);
    TEST_CHECK(!tracker.hasGpuTiming());

    return 0;
}

int testFrameTelemetryZeroAndInvalidDelta() {
    eng::FrameTelemetryTracker tracker;

    // Zero delta time should not divide by zero or produce NaN/Inf
    tracker.recordFrame(eng::Duration::zero(), eng::Duration::zero());
    TEST_CHECK(!std::isnan(tracker.instantaneousFps()));
    TEST_CHECK(!std::isinf(tracker.instantaneousFps()));
    TEST_CHECK(tracker.instantaneousFps() > 0.0F);
    TEST_CHECK(tracker.instantaneousFps() <= 10000.0F);

    // Negative delta time should clamp safely
    tracker.recordFrame(eng::Duration::fromMilliseconds(-50), eng::Duration::fromMilliseconds(-5));
    TEST_CHECK(!std::isnan(tracker.instantaneousFps()));
    TEST_CHECK(!std::isinf(tracker.instantaneousFps()));
    TEST_CHECK(tracker.instantaneousFps() > 0.0F);
    TEST_CHECK(tracker.cpuTimeMs() == 0.0F);

    // Massive delta time (e.g. 10 seconds debugger pause) clamped to ceiling (1.0 sec = 1 FPS)
    tracker.recordFrame(eng::Duration::fromSeconds(10.0), eng::Duration::fromSeconds(5.0));
    TEST_CHECK(tracker.frameTimeMs() == 1000.0F);
    TEST_CHECK(tracker.instantaneousFps() == 1.0F);

    return 0;
}

int testFrameTelemetryRollingAverageAndHistory() {
    eng::FrameTelemetryTracker tracker;

    // Record 60 frames of 10ms (100 FPS), CPU 2ms
    for (int i = 0; i < 60; ++i) {
        tracker.recordFrame(eng::Duration::fromMilliseconds(10),
                            eng::Duration::fromMilliseconds(2));
    }
    TEST_CHECK(tracker.totalFrames() == 60);
    TEST_CHECK(tracker.sampleCount() == 60);
    TEST_CHECK(tracker.historyIndex() == 60);
    TEST_CHECK(tracker.averageFps() >= 99.9F && tracker.averageFps() <= 100.1F);
    TEST_CHECK(tracker.averageFrameTimeMs() >= 9.9F && tracker.averageFrameTimeMs() <= 10.1F);
    TEST_CHECK(tracker.averageCpuTimeMs() == 2.0F);

    // Fill beyond capacity (120 capacity)
    for (int i = 0; i < 100; ++i) {
        tracker.recordFrame(eng::Duration::fromMilliseconds(20),
                            eng::Duration::fromMilliseconds(5));
    }
    TEST_CHECK(tracker.totalFrames() == 160);
    TEST_CHECK(tracker.sampleCount() == eng::FrameTelemetryTracker::kHistoryCapacity);
    TEST_CHECK(tracker.historyIndex() == 160 % eng::FrameTelemetryTracker::kHistoryCapacity);

    // Now min FPS is 50.0 (from 20ms frames) and max FPS is 100.0 (from remaining 10ms frames)
    TEST_CHECK(tracker.minFps() >= 49.9F && tracker.minFps() <= 50.1F);
    TEST_CHECK(tracker.maxFps() >= 99.9F && tracker.maxFps() <= 100.1F);

    // Reset clears state
    tracker.reset();
    TEST_CHECK(tracker.totalFrames() == 0);
    TEST_CHECK(tracker.sampleCount() == 0);
    TEST_CHECK(tracker.historyIndex() == 0);
    TEST_CHECK(tracker.instantaneousFps() == 0.0F);

    return 0;
}

int testFrameTelemetrySimulatedWorkloadChanges() {
    eng::FrameTelemetryTracker tracker;

    // Simulate Workload A: Lightweight scene (120 FPS = 8.33ms frame, 1.5ms CPU)
    for (int i = 0; i < 120; ++i) {
        tracker.recordFrame(eng::Duration::fromMicroseconds(8333),
                            eng::Duration::fromMicroseconds(1500));
    }
    float lightFps = tracker.averageFps();
    float lightCpu = tracker.averageCpuTimeMs();
    TEST_CHECK(lightFps >= 119.0F && lightFps <= 121.0F);
    TEST_CHECK(lightCpu >= 1.4F && lightCpu <= 1.6F);

    // Simulate Workload B: Heavy Sponza scene (30 FPS = 33.33ms frame, 12ms CPU)
    for (int i = 0; i < 120; ++i) {
        tracker.recordFrame(eng::Duration::fromMicroseconds(33333),
                            eng::Duration::fromMilliseconds(12));
    }
    float heavyFps = tracker.averageFps();
    float heavyCpu = tracker.averageCpuTimeMs();
    TEST_CHECK(heavyFps >= 29.5F && heavyFps <= 30.5F);
    TEST_CHECK(heavyCpu >= 11.9F && heavyCpu <= 12.1F);

    // Telemetry demonstrably responded to workload change
    TEST_CHECK(heavyFps < lightFps);
    TEST_CHECK(heavyCpu > lightCpu);

    return 0;
}

int testFrameTelemetryGpuTiming() {
    eng::FrameTelemetryTracker tracker;
    TEST_CHECK(!tracker.hasGpuTiming());
    TEST_CHECK(tracker.gpuTimeMs() == 0.0F);

    // Record frame with GPU timestamp
    tracker.recordFrame(eng::Duration::fromMilliseconds(16), eng::Duration::fromMilliseconds(3),
                        eng::Duration::fromMicroseconds(7500));
    TEST_CHECK(tracker.hasGpuTiming());
    TEST_CHECK(tracker.gpuTimeMs() == 7.5F);
    TEST_CHECK(tracker.averageGpuTimeMs() == 7.5F);

    // Subsequent frame without GPU timing reports false
    tracker.recordFrame(eng::Duration::fromMilliseconds(16), eng::Duration::fromMilliseconds(3),
                        std::nullopt);
    TEST_CHECK(!tracker.hasGpuTiming());
    TEST_CHECK(tracker.gpuTimeMs() == 0.0F);

    return 0;
}

} // namespace

int main() {
    if (testDurationConversionsAndArithmetic() != 0) {
        return 1;
    }
    if (testDurationOverflowAndBoundaries() != 0) {
        return 2;
    }
    if (testTimestampOperations() != 0) {
        return 3;
    }
    if (testClockTickingAndReset() != 0) {
        return 4;
    }
    if (testIndependentClocks() != 0) {
        return 5;
    }
    if (testTimeScalingAndNegativePolicy() != 0) {
        return 6;
    }
    if (testPauseAndResume() != 0) {
        return 7;
    }
    if (testBudgetAccounting() != 0) {
        return 8;
    }
    if (testFrameTelemetryBasic() != 0) {
        return 9;
    }
    if (testFrameTelemetryZeroAndInvalidDelta() != 0) {
        return 10;
    }
    if (testFrameTelemetryRollingAverageAndHistory() != 0) {
        return 11;
    }
    if (testFrameTelemetrySimulatedWorkloadChanges() != 0) {
        return 12;
    }
    if (testFrameTelemetryGpuTiming() != 0) {
        return 13;
    }

    return 0;
}
