/// @file error.h
/// @brief Lightweight, allocation-free error representation for Hikari-Machine.
///
/// Purpose:
/// Represents operation failure without relying on C++ exceptions, dynamic allocations,
/// or runtime type information (RTTI). Encapsulates stable error codes, non-owning
/// diagnostic descriptions, and source-location tracing.
///
/// Ownership and Lifetime:
/// The diagnostic `message` is stored as a non-owning `std::string_view`.
/// The referenced buffer must outlive the `Error` object (typically string literals).
///
/// Thread-safety:
/// Value type with const accessors. Instances can be moved or copied freely across threads.
///
/// Performance:
/// Standard layout POD-like struct. Zero heap allocations.

#pragma once

#include <cstdint>
#include <source_location>
#include <string_view>

namespace eng {

/// @enum ErrorCode
/// @brief Numerical identifiers mapped to subsystem error catalogs.
///
/// Codes in range 0200–0299 correspond to Subsystem 02 (Core & Memory) in docs/errors.md.
// NOLINTNEXTLINE(performance-enum-size): Error codes span multi-subsystem ranges up to 4999.
enum class ErrorCode : uint16_t {
    /// Operation succeeded without error.
    Ok = 0,

    /// Provided argument is invalid or violates preconditions (E-0200).
    InvalidArgument = 200,

    /// Generational handle is expired, stale, or index is out of bounds (E-0201).
    HandleResolutionFailed = 201,

    /// Allocator capacity or system memory exhausted (E-0202).
    OutOfMemory = 202,

    /// Index or offset is outside the valid range of the container (E-0203).
    OutOfBounds = 203,

    /// Operation attempted on an object in an incompatible or uninitialized state (E-0204).
    InvalidState = 204,

    /// Requested feature, platform backend, or operation is not supported (E-0205).
    NotSupported = 205
};

/// @struct Error
/// @brief Structured error return value.
///
/// Carries an error code, a non-owning diagnostic message, and origin source location.
struct Error {
    /// Numerical error identifier.
    ErrorCode code{ErrorCode::Ok};

    /// Non-owning diagnostic explanation. Must point to valid memory (e.g., string literal).
    std::string_view message;

    /// Source-code location where the error originated.
    std::source_location location{std::source_location::current()};

    /// @brief Checks whether this instance represents a success state.
    [[nodiscard]] constexpr bool isSuccess() const noexcept { return code == ErrorCode::Ok; }

    /// @brief Checks whether this instance represents an error state.
    [[nodiscard]] constexpr bool isError() const noexcept { return code != ErrorCode::Ok; }

    /// @brief Equality comparison operator.
    [[nodiscard]] constexpr bool operator==(const Error& other) const noexcept {
        return code == other.code && message == other.message;
    }
};

} // namespace eng
