/// @file budget.h
/// @brief Monotonic execution-time budget tracker with microsecond accounting.
///
/// Purpose:
/// Measures and limits execution duration for time-sliced operations.
/// Primary mechanism for budgeting Mode::prepare() standby preparation (target <= 0.5 ms)
/// and incremental script GC cycles.
///
/// Determinism & Testing:
/// Supports checking elapsed/remaining time against explicit injected timestamps as well as
/// sampling live monotonic time via Timestamp::now().
///
/// Thread-safety:
/// Value type. Read operations on const references are safe.
///
/// Performance:
/// Microsecond accounting via integer subtraction. Zero heap allocation, hot-path safe.

#pragma once

#include <algorithm>
#include <core/time/duration.h>
#include <core/time/timestamp.h>
#include <cstdint>

namespace eng {

/// @class Budget
/// @brief Time-limited execution budget with elapsed and remaining duration queries.
class Budget {
public:
    /// @brief Constructs a budget with an explicit time limit and optional starting timestamp.
    ///
    /// @param limit Maximum allowed execution duration.
    /// @param start Monotonic start timestamp (defaults to Timestamp::now()).
    explicit Budget(Duration limit, Timestamp start = Timestamp::now()) noexcept;

    /// @brief Factory: creates a budget from an integer microsecond limit.
    [[nodiscard]] static Budget fromMicroseconds(int64_t microseconds,
                                                 Timestamp start = Timestamp::now()) noexcept {
        return Budget{Duration::fromMicroseconds(microseconds), start};
    }

    /// @brief Factory: creates a budget from an integer millisecond limit.
    [[nodiscard]] static Budget fromMilliseconds(int64_t milliseconds,
                                                 Timestamp start = Timestamp::now()) noexcept {
        return Budget{Duration::fromMilliseconds(milliseconds), start};
    }

    /// @brief Returns the total allocated time limit for this budget.
    [[nodiscard]] constexpr Duration limit() const noexcept { return limit_; }

    /// @brief Returns the monotonic timestamp when this budget was started.
    [[nodiscard]] constexpr Timestamp startTime() const noexcept { return start_; }

    /// @brief Returns elapsed time since start by sampling Timestamp::now().
    [[nodiscard]] Duration elapsed() const noexcept { return elapsed(Timestamp::now()); }

    /// @brief Returns elapsed time against an explicit monotonic timestamp (deterministic).
    [[nodiscard]] Duration elapsed(Timestamp now) const noexcept;

    /// @brief Returns remaining time before expiration by sampling Timestamp::now().
    [[nodiscard]] Duration remaining() const noexcept { return remaining(Timestamp::now()); }

    /// @brief Returns remaining time before expiration against an explicit timestamp
    /// (deterministic).
    [[nodiscard]] Duration remaining(Timestamp now) const noexcept;

    /// @brief Returns true if elapsed time has reached or exceeded the allocated limit.
    [[nodiscard]] bool isExpired() const noexcept { return isExpired(Timestamp::now()); }

    /// @brief Returns true if elapsed time has reached or exceeded the allocated limit at now.
    [[nodiscard]] bool isExpired(Timestamp now) const noexcept;

    /// @brief Returns remaining budget formatted as integer microseconds.
    [[nodiscard]] int64_t remainingMicroseconds() const noexcept {
        return remainingMicroseconds(Timestamp::now());
    }

    /// @brief Returns remaining budget formatted as integer microseconds at now.
    [[nodiscard]] int64_t remainingMicroseconds(Timestamp now) const noexcept {
        return remaining(now).asMicroseconds();
    }

    /// @brief Resets the starting timestamp to now.
    void reset(Timestamp now = Timestamp::now()) noexcept { start_ = now; }

private:
    Timestamp start_;
    Duration limit_{Duration::zero()};
};

} // namespace eng
