/// @file result.h
/// @brief Exception-free Result type for operations that may fail.
///
/// Purpose:
/// Provides explicit value-or-error return semantics across the engine runtime.
/// Replaces C++ exception unwinding and raw error-code out-parameters.
///
/// Ownership and Lifetime:
/// Result owns either the contained value T or error E. Move-only types (e.g. std::unique_ptr)
/// are fully supported. Moving from a Result transfers ownership of the contained value or error.
///
/// Thread-safety:
/// Distinct Result instances are safe to access concurrently. Shared instances require external
/// synchronization.
///
/// Performance:
/// Trivially forwards construction and destruction to std::variant or std::optional.
/// Incurs zero dynamic allocation overhead.
///
/// Invariants:
/// - A Result is ALWAYS in either a valid value state or a valid error state.
/// - Invalid access (e.g. calling value() on an error state) triggers an ENG_ASSERT.

#pragma once

#include <core/assert.h>
#include <core/error.h>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace eng {

/// @class Result<T, E>
/// @brief Represents either a successfully produced value T, or an error E.
///
/// @tparam T Contained value type (non-void, non-reference).
/// @tparam E Contained error type (defaults to eng::Error).
template <typename T, typename E = Error> class Result {
public:
    static_assert(!std::is_same_v<T, void>,
                  "Use Result<void, E> specialization for operations without return values");
    static_assert(!std::is_reference_v<T>, "Result does not support raw reference types");
    static_assert(!std::is_same_v<T, E>, "Result value and error types must not be identical");

    /// @brief Constructs a successful Result containing a value.
    constexpr Result(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : storage_(std::in_place_index<0>, std::move(value)) {}

    /// @brief Constructs a failed Result containing an error.
    constexpr Result(E error) noexcept(std::is_nothrow_move_constructible_v<E>)
        : storage_(std::in_place_index<1>, std::move(error)) {}

    Result(const Result&) = default;
    Result& operator=(const Result&) = default;
    Result(Result&&) noexcept(std::is_nothrow_move_constructible_v<T> &&
                              std::is_nothrow_move_constructible_v<E>) = default;
    Result& operator=(Result&&) noexcept(std::is_nothrow_move_assignable_v<T> &&
                                         std::is_nothrow_move_assignable_v<E>) = default;
    ~Result() = default;

    /// @brief Factory helper constructing a successful Result.
    [[nodiscard]] static constexpr Result<T, E>
    success(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return Result<T, E>(std::move(value));
    }

    /// @brief Factory helper constructing a failed Result.
    [[nodiscard]] static constexpr Result<T, E>
    failure(E error) noexcept(std::is_nothrow_move_constructible_v<E>) {
        return Result<T, E>(std::move(error));
    }

    /// @brief Returns true if this instance holds a successful value.
    [[nodiscard]] constexpr bool hasValue() const noexcept { return storage_.index() == 0; }

    /// @brief Returns true if this instance holds an error.
    [[nodiscard]] constexpr bool isError() const noexcept { return storage_.index() == 1; }

    /// @brief Contextual boolean conversion checking for value presence.
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return hasValue(); }

    /// @brief Accesses the contained value.
    /// @pre hasValue() == true
    [[nodiscard]] constexpr T& value() & noexcept {
        ENG_ASSERT(hasValue(), "Attempted to access value() on an error Result");
        return std::get<0>(storage_);
    }

    /// @brief Accesses the contained value (const lvalue).
    /// @pre hasValue() == true
    [[nodiscard]] constexpr const T& value() const& noexcept {
        ENG_ASSERT(hasValue(), "Attempted to access value() on an error Result");
        return std::get<0>(storage_);
    }

    /// @brief Accesses the contained value (rvalue).
    /// @pre hasValue() == true
    [[nodiscard]] constexpr T&& value() && noexcept {
        ENG_ASSERT(hasValue(), "Attempted to access value() on an error Result");
        return std::get<0>(std::move(storage_));
    }

    /// @brief Accesses the contained value (const rvalue).
    /// @pre hasValue() == true
    [[nodiscard]] constexpr const T&& value() const&& noexcept {
        ENG_ASSERT(hasValue(), "Attempted to access value() on an error Result");
        return std::get<0>(std::move(storage_));
    }

    /// @brief Accesses the contained error.
    /// @pre isError() == true
    [[nodiscard]] constexpr E& error() & noexcept {
        ENG_ASSERT(isError(), "Attempted to access error() on a successful Result");
        return std::get<1>(storage_);
    }

    /// @brief Accesses the contained error (const lvalue).
    /// @pre isError() == true
    [[nodiscard]] constexpr const E& error() const& noexcept {
        ENG_ASSERT(isError(), "Attempted to access error() on a successful Result");
        return std::get<1>(storage_);
    }

    /// @brief Accesses the contained error (rvalue).
    /// @pre isError() == true
    [[nodiscard]] constexpr E&& error() && noexcept {
        ENG_ASSERT(isError(), "Attempted to access error() on a successful Result");
        return std::get<1>(std::move(storage_));
    }

    /// @brief Accesses the contained error (const rvalue).
    /// @pre isError() == true
    [[nodiscard]] constexpr const E&& error() const&& noexcept {
        ENG_ASSERT(isError(), "Attempted to access error() on a successful Result");
        return std::get<1>(std::move(storage_));
    }

private:
    std::variant<T, E> storage_;
};

/// @class Result<void, E>
/// @brief Specialization of Result for operations that return no value on success.
///
/// @tparam E Contained error type (defaults to eng::Error).
template <typename E> class Result<void, E> {
public:
    /// @brief Default constructor represents success.
    constexpr Result() noexcept = default;

    /// @brief Constructs a failed void Result.
    constexpr Result(E error) noexcept(std::is_nothrow_move_constructible_v<E>)
        : error_(std::move(error)) {}

    Result(const Result&) = default;
    Result& operator=(const Result&) = default;
    Result(Result&&) noexcept(std::is_nothrow_move_constructible_v<E>) = default;
    Result& operator=(Result&&) noexcept(std::is_nothrow_move_assignable_v<E>) = default;
    ~Result() = default;

    /// @brief Factory helper constructing a successful void Result.
    [[nodiscard]] static constexpr Result<void, E> success() noexcept { return Result<void, E>{}; }

    /// @brief Factory helper constructing a failed void Result.
    [[nodiscard]] static constexpr Result<void, E>
    failure(E error) noexcept(std::is_nothrow_move_constructible_v<E>) {
        return Result<void, E>(std::move(error));
    }

    /// @brief Returns true if this instance represents success.
    [[nodiscard]] constexpr bool hasValue() const noexcept { return !error_.has_value(); }

    /// @brief Returns true if this instance represents an error.
    [[nodiscard]] constexpr bool isError() const noexcept { return error_.has_value(); }

    /// @brief Contextual boolean conversion checking for success.
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return hasValue(); }

    /// @brief Accesses the contained error.
    /// @pre isError() == true
    [[nodiscard]] constexpr E& error() & noexcept {
        ENG_VERIFY(error_.has_value(), "Attempted to access error() on a successful Result<void>");
        return *error_;
    }

    /// @brief Accesses the contained error (const lvalue).
    /// @pre isError() == true
    [[nodiscard]] constexpr const E& error() const& noexcept {
        ENG_VERIFY(error_.has_value(), "Attempted to access error() on a successful Result<void>");
        return *error_;
    }

    /// @brief Accesses the contained error (rvalue).
    /// @pre isError() == true
    [[nodiscard]] constexpr E&& error() && noexcept {
        ENG_VERIFY(error_.has_value(), "Attempted to access error() on a successful Result<void>");
        return *std::move(error_);
    }

    /// @brief Accesses the contained error (const rvalue).
    /// @pre isError() == true
    [[nodiscard]] constexpr const E&& error() const&& noexcept {
        ENG_VERIFY(error_.has_value(), "Attempted to access error() on a successful Result<void>");
        return *std::move(error_);
    }

private:
    std::optional<E> error_{std::nullopt};
};

} // namespace eng
