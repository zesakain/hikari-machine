/// @file assert.h
/// @brief Assertion and release-verification infrastructure for Hikari-Machine.
///
/// Purpose:
/// Provides compile-time selectable runtime verification:
/// - ENG_ASSERT: Programmer invariant check; compiled out in Release builds.
/// - ENG_VERIFY: Essential invariant check; condition is ALWAYS evaluated across all builds.
///
/// Ownership and Lifetime:
/// Diagnostic strings passed to assertions are non-owning string views.
///
/// Thread-safety:
/// Safe to evaluate from any thread. On failure, logs to standard error and terminates process.
///
/// Performance:
/// - ENG_ASSERT in Release: Zero runtime evaluation and zero binary overhead.
/// - ENG_VERIFY in all builds: Evaluates condition once; zero allocation on hot path.
///
/// Failure Mode:
/// Violating an assertion or verification triggers an immediate terminal trap via std::abort().

#pragma once

#include <source_location>
#include <string_view>

namespace eng::detail {

/// @brief Dispatches assertion failure diagnostic and terminates execution.
///
/// Preconditions : None.
/// Postconditions: Does not return; terminates process.
/// Thread-safety : Safe to call concurrently from any thread.
/// Performance   : Formats directly to standard error with zero heap allocation.
///
/// @param condition Evaluated expression as string.
/// @param message Descriptive explanation of the invariant violated.
/// @param location Call-site source location.
[[noreturn]] void handleAssertFailure(std::string_view condition, std::string_view message,
                                      std::source_location location) noexcept;

} // namespace eng::detail

/// @def ENG_VERIFY(condition, message)
/// @brief Invariant check that evaluates condition in ALL build configurations.
///
/// In both Debug and Release builds, condition is executed. If condition evaluates
/// to false, a diagnostic is printed to standard error and process execution terminates.
///
/// Example:
/// @code
/// ENG_VERIFY(file.isOpen(), "Configuration file must be open for reading");
/// @endcode
#define ENG_VERIFY(condition, message)                                                             \
    do {                                                                                           \
        if (!(condition)) [[unlikely]] {                                                           \
            ::eng::detail::handleAssertFailure(#condition, (message),                              \
                                               ::std::source_location::current());                 \
        }                                                                                          \
    } while (false)

/// @def ENG_ASSERT(condition, message)
/// @brief Programmer contract assertion active only in Debug / Sanitizer builds.
///
/// When NDEBUG is defined (Release builds), the condition expression is not evaluated
/// at runtime, incurring zero performance penalty.
/// In non-Release builds, behaves identically to ENG_VERIFY.
///
/// Example:
/// @code
/// ENG_ASSERT(index < size, "Index out of bounds in component array");
/// @endcode
#ifdef NDEBUG
#define ENG_ASSERT(condition, message)                                                             \
    do {                                                                                           \
        (void)sizeof(condition);                                                                   \
        (void)sizeof(message);                                                                     \
    } while (false)
#else
#define ENG_ASSERT(condition, message) ENG_VERIFY(condition, message)
#endif
