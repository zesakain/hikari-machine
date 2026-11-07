/// @file log.h
/// @brief Core structured logging subsystem with bounded ring buffer and zero heap allocation.
///
/// Purpose:
/// Provides lightweight, non-blocking, structured log recording for engine subsystems.
/// Designed for low-overhead logging in the hot path without heap allocation, mutex contention,
/// or blocking I/O.
///
/// Log Levels & Filtering:
/// Levels in ascending severity: Trace, Debug, Info, Warning, Error, Critical.
/// Messages below the active minimum log level are dropped immediately with zero formatting cost.
///
/// Bounded Ring Buffer & Overflow Policy:
/// Records are stored in a fixed-capacity ring buffer (default 256 records, 256 bytes per message).
/// When the buffer is full, subsequent log entries are rejected non-blockingly, and an
/// atomic/integer dropped counter is incremented. No heap memory is ever allocated, and no memory
/// is overwritten.
///
/// Flushed Execution & Cold Path I/O:
/// Buffered records are written to standard error (or an explicit stream) only when flush() is
/// invoked outside the hot path (e.g. frame boundaries, mode switches, or error recovery).
///
/// Thread-safety:
/// By design, Logger instances do not employ mutexes or locks to prevent hot-path contention.
/// Individual Logger instances must be accessed on their owning thread, or synchronized externally.
///
/// Performance:
/// O(1) ring buffer append. Zero heap allocation. Zero blocking file I/O on log calls.

#pragma once

#include <array>
#include <core/time/timestamp.h>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <source_location>
#include <string_view>

namespace eng {

/// @enum LogLevel
/// @brief Severity level for structured log records.
enum class LogLevel : uint8_t { Trace = 0, Debug, Info, Warning, Error, Critical };

/// @brief Converts a LogLevel to a stable uppercase 4-5 character string representation.
[[nodiscard]] constexpr std::string_view toString(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Trace:
        return "TRACE";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Critical:
        return "CRIT";
    }
    return "UNKNOWN";
}

/// @struct LogRecord
/// @brief Single structured log record stored within the bounded ring buffer.
struct LogRecord {
    /// @brief Maximum character capacity for formatted log messages (including null terminator).
    static constexpr size_t kMaxMessageCapacity = 256;

    LogLevel level{LogLevel::Info};
    std::string_view category{"Core"};
    Timestamp timestamp;
    std::source_location location;
    size_t messageLength{0};
    std::array<char, kMaxMessageCapacity> message{};

    /// @brief Returns a non-owning string view over the recorded message content.
    [[nodiscard]] std::string_view messageView() const noexcept {
        return std::string_view{message.data(), messageLength};
    }
};

/// @class Logger
/// @brief Bounded ring-buffer logger with level filtering and non-blocking dropped-record
/// accounting.
class Logger {
public:
    /// @brief Default maximum capacity of buffered records before overflow occurs.
    static constexpr size_t kDefaultCapacity = 256;

    /// @brief Constructs an empty logger with Info minimum log level.
    constexpr Logger() noexcept = default;

    /// @brief Returns the shared default engine logger instance.
    [[nodiscard]] static Logger& defaultLogger() noexcept;

    /// @brief Records a plain text log message into the ring buffer.
    ///
    /// Preconditions : message must be a valid null-terminated or sized string.
    /// Postconditions: If level >= minLogLevel and buffer is not full, record is stored.
    ///                 If buffer is full, record is dropped and droppedCount is incremented.
    /// Thread-safety : Calling thread only. Not re-entrant.
    /// Performance   : O(1) ring buffer append. Zero heap allocation. Zero blocking I/O.
    ///
    /// @param level Severity level.
    /// @param category Subsystem or component category tag.
    /// @param message String view of log text.
    /// @param location Source location (defaults to caller location).
    /// @return True if recorded; false if filtered by level or dropped due to buffer overflow.
    bool log(LogLevel level, std::string_view category, std::string_view message,
             std::source_location location = std::source_location::current()) noexcept;

    /// @brief Records a formatted log message via vsnprintf into the ring buffer.
    ///
    /// @param level Severity level.
    /// @param category Subsystem or component category tag.
    /// @param location Source location of the call site.
    /// @param fmt Printf-style format string.
    /// @return True if recorded; false if filtered by level or dropped due to buffer overflow.
    // NOLINTNEXTLINE(modernize-avoid-variadic-functions)
    bool logFormat(LogLevel level, std::string_view category, std::source_location location,
                   const char* fmt, ...) noexcept;

    /// @brief Flushes all buffered log records in FIFO order to the target stream.
    ///
    /// If records were dropped due to buffer overflow, emits an overflow diagnostic warning.
    ///
    /// Postconditions: Buffer is emptied (count == 0) and droppedCount is reset to 0.
    /// Performance   : Formatted stream I/O. Intended for cold-path frame boundaries / shutdown.
    ///
    /// @param stream Target FILE pointer (defaults to stderr).
    /// @return Number of records successfully written to stream.
    size_t flush(FILE* stream = stderr) noexcept;

    /// @brief Discards all buffered records and resets dropped record counter.
    void clear() noexcept;

    /// @brief Sets the minimum severity required for records to be accepted.
    void setMinLogLevel(LogLevel level) noexcept { minLevel_ = level; }

    /// @brief Returns the current minimum severity level.
    [[nodiscard]] constexpr LogLevel minLogLevel() const noexcept { return minLevel_; }

    /// @brief Returns the number of un-flushed records currently stored in the ring buffer.
    [[nodiscard]] constexpr size_t count() const noexcept { return count_; }

    /// @brief Returns the total storage capacity of the ring buffer.
    [[nodiscard]] static constexpr size_t capacity() noexcept { return kDefaultCapacity; }

    /// @brief Returns the total count of records dropped due to buffer overflow since last
    /// flush/clear.
    [[nodiscard]] constexpr size_t droppedCount() const noexcept { return droppedCount_; }

    /// @brief Returns true if no records are currently buffered.
    [[nodiscard]] constexpr bool isEmpty() const noexcept { return count_ == 0; }

    /// @brief Returns true if the ring buffer has reached its capacity limit.
    [[nodiscard]] constexpr bool isFull() const noexcept { return count_ == kDefaultCapacity; }

    /// @brief Inspects a buffered record by relative index (0 = oldest unflushed record).
    ///
    /// Useful for test assertions and diagnostic inspection without consuming records.
    ///
    /// @param relativeIndex Offset from the read head (0 <= relativeIndex < count()).
    /// @return Pointer to record, or nullptr if relativeIndex is out of range.
    [[nodiscard]] const LogRecord* peek(size_t relativeIndex) const noexcept;

private:
    std::array<LogRecord, kDefaultCapacity> buffer_{};
    size_t head_{0};         // Write index
    size_t tail_{0};         // Read index
    size_t count_{0};        // Active count
    size_t droppedCount_{0}; // Dropped count
    LogLevel minLevel_{LogLevel::Info};
};

} // namespace eng

// --- Practical Logging Macros ---

#define ENG_LOG(level, category, message)                                                          \
    ::eng::Logger::defaultLogger().log((level), (category), (message),                             \
                                       ::std::source_location::current())

#define ENG_LOG_TRACE(category, message) ENG_LOG(::eng::LogLevel::Trace, category, message)
#define ENG_LOG_DEBUG(category, message) ENG_LOG(::eng::LogLevel::Debug, category, message)
#define ENG_LOG_INFO(category, message) ENG_LOG(::eng::LogLevel::Info, category, message)
#define ENG_LOG_WARN(category, message) ENG_LOG(::eng::LogLevel::Warning, category, message)
#define ENG_LOG_ERROR(category, message) ENG_LOG(::eng::LogLevel::Error, category, message)
#define ENG_LOG_CRITICAL(category, message) ENG_LOG(::eng::LogLevel::Critical, category, message)

#define ENG_LOG_FORMAT(level, category, fmt, ...)                                                  \
    ::eng::Logger::defaultLogger().logFormat(                                                      \
        (level), (category), ::std::source_location::current(), (fmt), ##__VA_ARGS__)
