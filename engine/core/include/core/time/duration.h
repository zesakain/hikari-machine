/// @file duration.h
/// @brief Type-safe, integer-based time duration representation.
///
/// Purpose:
/// Represents an elapsed duration of time with nanosecond precision.
/// Backed by a 64-bit signed integer to prevent floating-point drift over long execution periods.
///
/// Units and Conversions:
/// Stored internally as integer nanoseconds. Provides explicit conversion factories
/// (fromSeconds, fromMilliseconds, fromMicroseconds, fromNanoseconds) and accessors.
///
/// Thread-safety:
/// Value type with pure constexpr operations. Safe to copy and compare concurrently across threads.
///
/// Performance:
/// 64-bit POD struct passing in a single CPU register. Zero heap allocation.

#pragma once

#include <compare>
#include <concepts>
#include <cstdint>
#include <limits>

namespace eng {

/// @class Duration
/// @brief Type-safe nanosecond-resolution time duration.
class Duration {
public:
    static constexpr int64_t kNanosecondsPerMicrosecond = 1'000LL;
    static constexpr int64_t kNanosecondsPerMillisecond = 1'000'000LL;
    static constexpr int64_t kNanosecondsPerSecond = 1'000'000'000LL;

    /// @brief Constructs a zero duration.
    constexpr Duration() noexcept = default;

    /// @brief Factory: creates duration from nanoseconds.
    [[nodiscard]] static constexpr Duration fromNanoseconds(int64_t nanoseconds) noexcept {
        return Duration{nanoseconds};
    }

    /// @brief Factory: creates duration from microseconds.
    [[nodiscard]] static constexpr Duration fromMicroseconds(int64_t microseconds) noexcept {
        if (microseconds > std::numeric_limits<int64_t>::max() / kNanosecondsPerMicrosecond) {
            return Duration{std::numeric_limits<int64_t>::max()};
        }
        if (microseconds < std::numeric_limits<int64_t>::min() / kNanosecondsPerMicrosecond) {
            return Duration{std::numeric_limits<int64_t>::min()};
        }
        return Duration{microseconds * kNanosecondsPerMicrosecond};
    }

    /// @brief Factory: creates duration from milliseconds.
    [[nodiscard]] static constexpr Duration fromMilliseconds(int64_t milliseconds) noexcept {
        if (milliseconds > std::numeric_limits<int64_t>::max() / kNanosecondsPerMillisecond) {
            return Duration{std::numeric_limits<int64_t>::max()};
        }
        if (milliseconds < std::numeric_limits<int64_t>::min() / kNanosecondsPerMillisecond) {
            return Duration{std::numeric_limits<int64_t>::min()};
        }
        return Duration{milliseconds * kNanosecondsPerMillisecond};
    }

    /// @brief Factory: creates duration from floating-point seconds.
    [[nodiscard]] static constexpr Duration fromSeconds(double seconds) noexcept {
        constexpr double kMaxSec = static_cast<double>(std::numeric_limits<int64_t>::max()) / 1e9;
        constexpr double kMinSec = static_cast<double>(std::numeric_limits<int64_t>::min()) / 1e9;
        if (seconds >= kMaxSec) {
            return Duration{std::numeric_limits<int64_t>::max()};
        }
        if (seconds <= kMinSec) {
            return Duration{std::numeric_limits<int64_t>::min()};
        }
        return Duration{static_cast<int64_t>(seconds * 1e9)};
    }

    /// @brief Factory: canonical zero duration.
    [[nodiscard]] static constexpr Duration zero() noexcept { return Duration{0}; }

    /// @brief Factory: maximum representable duration (~292 years).
    [[nodiscard]] static constexpr Duration max() noexcept {
        return Duration{std::numeric_limits<int64_t>::max()};
    }

    /// @brief Factory: minimum representable duration (-~292 years).
    [[nodiscard]] static constexpr Duration min() noexcept {
        return Duration{std::numeric_limits<int64_t>::min()};
    }

    /// @brief Accessor: exact integer nanoseconds.
    [[nodiscard]] constexpr int64_t asNanoseconds() const noexcept { return nanoseconds_; }

    /// @brief Accessor: integer microseconds (truncated).
    [[nodiscard]] constexpr int64_t asMicroseconds() const noexcept {
        return nanoseconds_ / kNanosecondsPerMicrosecond;
    }

    /// @brief Accessor: integer milliseconds (truncated).
    [[nodiscard]] constexpr int64_t asMilliseconds() const noexcept {
        return nanoseconds_ / kNanosecondsPerMillisecond;
    }

    /// @brief Accessor: floating-point seconds.
    [[nodiscard]] constexpr double toSeconds() const noexcept {
        return static_cast<double>(nanoseconds_) / 1e9;
    }

    /// @brief Accessor: single-precision floating-point seconds (for gameplay systems).
    [[nodiscard]] constexpr float toSecondsF() const noexcept {
        return static_cast<float>(static_cast<double>(nanoseconds_) / 1e9);
    }

    /// @brief Returns true if duration is exactly zero.
    [[nodiscard]] constexpr bool isZero() const noexcept { return nanoseconds_ == 0; }

    /// @brief Returns true if duration is strictly positive (> 0).
    [[nodiscard]] constexpr bool isPositive() const noexcept { return nanoseconds_ > 0; }

    /// @brief Returns true if duration is strictly negative (< 0).
    [[nodiscard]] constexpr bool isNegative() const noexcept { return nanoseconds_ < 0; }

    // Arithmetic operators
    [[nodiscard]] constexpr Duration operator-() const noexcept { return Duration{-nanoseconds_}; }

    [[nodiscard]] constexpr Duration operator+(const Duration& rhs) const noexcept {
        return Duration{nanoseconds_ + rhs.nanoseconds_};
    }

    [[nodiscard]] constexpr Duration operator-(const Duration& rhs) const noexcept {
        return Duration{nanoseconds_ - rhs.nanoseconds_};
    }

    template <std::integral T> [[nodiscard]] constexpr Duration operator*(T scalar) const noexcept {
        return Duration{nanoseconds_ * static_cast<int64_t>(scalar)};
    }

    template <std::floating_point T>
    [[nodiscard]] constexpr Duration operator*(T scalar) const noexcept {
        return Duration{
            static_cast<int64_t>(static_cast<double>(nanoseconds_) * static_cast<double>(scalar))};
    }

    template <std::integral T>
    [[nodiscard]] constexpr Duration operator/(T divisor) const noexcept {
        if (divisor == 0) {
            return zero();
        }
        return Duration{nanoseconds_ / static_cast<int64_t>(divisor)};
    }

    template <std::floating_point T>
    [[nodiscard]] constexpr Duration operator/(T divisor) const noexcept {
        if (divisor == 0.0) {
            return zero();
        }
        return Duration{
            static_cast<int64_t>(static_cast<double>(nanoseconds_) / static_cast<double>(divisor))};
    }

    [[nodiscard]] constexpr double operator/(const Duration& rhs) const noexcept {
        if (rhs.nanoseconds_ == 0) {
            return 0.0;
        }
        return static_cast<double>(nanoseconds_) / static_cast<double>(rhs.nanoseconds_);
    }

    constexpr Duration& operator+=(const Duration& rhs) noexcept {
        nanoseconds_ += rhs.nanoseconds_;
        return *this;
    }

    constexpr Duration& operator-=(const Duration& rhs) noexcept {
        nanoseconds_ -= rhs.nanoseconds_;
        return *this;
    }

    template <std::integral T> constexpr Duration& operator*=(T scalar) noexcept {
        nanoseconds_ *= static_cast<int64_t>(scalar);
        return *this;
    }

    template <std::floating_point T> constexpr Duration& operator*=(T scalar) noexcept {
        nanoseconds_ =
            static_cast<int64_t>(static_cast<double>(nanoseconds_) * static_cast<double>(scalar));
        return *this;
    }

    template <std::integral T> constexpr Duration& operator/=(T divisor) noexcept {
        if (divisor != 0) {
            nanoseconds_ /= static_cast<int64_t>(divisor);
        }
        return *this;
    }

    template <std::floating_point T> constexpr Duration& operator/=(T divisor) noexcept {
        if (divisor != 0.0) {
            nanoseconds_ = static_cast<int64_t>(static_cast<double>(nanoseconds_) /
                                                static_cast<double>(divisor));
        }
        return *this;
    }

    // Comparison operators (C++20 spaceship)
    [[nodiscard]] constexpr auto operator<=>(const Duration&) const noexcept = default;
    [[nodiscard]] constexpr bool operator==(const Duration&) const noexcept = default;

private:
    constexpr explicit Duration(int64_t nanoseconds) noexcept : nanoseconds_(nanoseconds) {}

    int64_t nanoseconds_{0};
};

template <std::integral T>
[[nodiscard]] constexpr Duration operator*(T scalar, const Duration& duration) noexcept {
    return duration * scalar;
}

template <std::floating_point T>
[[nodiscard]] constexpr Duration operator*(T scalar, const Duration& duration) noexcept {
    return duration * scalar;
}

} // namespace eng
