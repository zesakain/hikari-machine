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

    return 0;
}
