/// @file timestamp.h
/// @brief Monotonic point-in-time timestamp representation.
///
/// Purpose:
/// Represents a discrete, monotonically increasing point in time.
/// Uses the system steady clock (std::chrono::steady_clock) to guarantee that timestamps never
/// jump backwards due to NTP adjustments or timezone changes.
///
/// Distinguishing Monotonic from Wall-Clock Time:
/// Timestamps represent elapsed time from an arbitrary steady system epoch. They must NOT be
/// formatted as calendar dates or wall-clock hours, but are strictly used to compute relative
/// durations and frame deltas.
///
/// Thread-safety:
/// Value type with pure constexpr arithmetic. Safe to copy and compare concurrently across threads.
///
/// Performance:
/// 64-bit POD struct passing in a single CPU register. Zero heap allocation.

#pragma once

#include <chrono>
#include <compare>
#include <core/time/duration.h>
#include <cstdint>

namespace eng {

/// @class Timestamp
/// @brief Monotonic point-in-time representation.
class Timestamp {
public:
    /// @brief Constructs an uninitialized epoch timestamp (nanoseconds = 0).
    constexpr Timestamp() noexcept = default;

    /// @brief Samples the current monotonic time from std::chrono::steady_clock.
    [[nodiscard]] static Timestamp now() noexcept {
        const auto steadyNow = std::chrono::steady_clock::now().time_since_epoch();
        const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(steadyNow).count();
        return Timestamp{nanos};
    }

    /// @brief Factory: creates an explicit timestamp from monotonic nanoseconds (for deterministic
    /// testing).
    [[nodiscard]] static constexpr Timestamp fromNanoseconds(int64_t nanoseconds) noexcept {
        return Timestamp{nanoseconds};
    }

    /// @brief Accessor: raw monotonic ticks since steady clock epoch.
    [[nodiscard]] constexpr int64_t nanosecondsSinceEpoch() const noexcept { return nanoseconds_; }

    /// @brief Subtraction between two timestamps yields elapsed Duration.
    [[nodiscard]] constexpr Duration operator-(const Timestamp& rhs) const noexcept {
        return Duration::fromNanoseconds(nanoseconds_ - rhs.nanoseconds_);
    }

    /// @brief Advances timestamp by duration.
    [[nodiscard]] constexpr Timestamp operator+(const Duration& duration) const noexcept {
        return Timestamp{nanoseconds_ + duration.asNanoseconds()};
    }

    /// @brief Rewinds timestamp by duration.
    [[nodiscard]] constexpr Timestamp operator-(const Duration& duration) const noexcept {
        return Timestamp{nanoseconds_ - duration.asNanoseconds()};
    }

    constexpr Timestamp& operator+=(const Duration& duration) noexcept {
        nanoseconds_ += duration.asNanoseconds();
        return *this;
    }

    constexpr Timestamp& operator-=(const Duration& duration) noexcept {
        nanoseconds_ -= duration.asNanoseconds();
        return *this;
    }

    // Comparison operators (C++20 spaceship)
    [[nodiscard]] constexpr auto operator<=>(const Timestamp&) const noexcept = default;
    [[nodiscard]] constexpr bool operator==(const Timestamp&) const noexcept = default;

private:
    constexpr explicit Timestamp(int64_t nanoseconds) noexcept : nanoseconds_(nanoseconds) {}

    int64_t nanoseconds_{0};
};

} // namespace eng
