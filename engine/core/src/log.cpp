/// @file log.cpp
/// @brief Core structured logger implementation.

#include <algorithm>
#include <core/log.h>

namespace eng {

Logger& Logger::defaultLogger() noexcept {
    static Logger sDefaultLogger;
    return sDefaultLogger;
}

bool Logger::log(LogLevel level, std::string_view category, std::string_view message,
                 std::source_location location) noexcept {
    if (level < minLevel_) {
        return false;
    }

    if (count_ >= kDefaultCapacity) {
        ++droppedCount_;
        return false; // Bounded ring buffer is full; reject non-blockingly
    }

    LogRecord& record = buffer_[head_];
    record.level = level;
    record.category = category;
    record.timestamp = Timestamp::now();
    record.location = location;

    const size_t copyLen = std::min(message.size(), LogRecord::kMaxMessageCapacity - 1);
    std::memcpy(record.message.data(), message.data(), copyLen);
    record.message[copyLen] = '\0';
    record.messageLength = copyLen;

    head_ = (head_ + 1) % kDefaultCapacity;
    ++count_;
    return true;
}

// NOLINTNEXTLINE(modernize-avoid-variadic-functions)
bool Logger::logFormat(LogLevel level, std::string_view category, std::source_location location,
                       const char* fmt, ...) noexcept {
    if (level < minLevel_) {
        return false;
    }

    if (count_ >= kDefaultCapacity) {
        ++droppedCount_;
        return false;
    }

    LogRecord& record = buffer_[head_];
    record.level = level;
    record.category = category;
    record.timestamp = Timestamp::now();
    record.location = location;

    va_list args;
    va_start(args, fmt);
    const int written =
        std::vsnprintf(record.message.data(), LogRecord::kMaxMessageCapacity, fmt, args);
    va_end(args);

    if (written < 0) {
        record.message[0] = '\0';
        record.messageLength = 0;
    } else {
        const auto len = static_cast<size_t>(written);
        record.messageLength = std::min(len, LogRecord::kMaxMessageCapacity - 1);
        record.message[record.messageLength] = '\0';
    }

    head_ = (head_ + 1) % kDefaultCapacity;
    ++count_;
    return true;
}

size_t Logger::flush(FILE* stream) noexcept {
    if (stream == nullptr) {
        return 0;
    }

    if (droppedCount_ > 0) {
        std::fprintf(stream, "[WARN] [Log] %zu log record(s) dropped due to buffer overflow.\n",
                     droppedCount_);
        droppedCount_ = 0;
    }

    size_t flushedCount = 0;
    while (count_ > 0) {
        const LogRecord& record = buffer_[tail_];
        const double sec = static_cast<double>(record.timestamp.nanosecondsSinceEpoch()) / 1e9;

        const std::string_view lvlStr = toString(record.level);
        if (record.level >= LogLevel::Warning) {
            std::fprintf(stream, "[%.6fs] [%-5.*s] [%.*s] %.*s (%s:%u)\n", sec,
                         static_cast<int>(lvlStr.size()), lvlStr.data(),
                         static_cast<int>(record.category.size()), record.category.data(),
                         static_cast<int>(record.messageLength), record.message.data(),
                         record.location.file_name(), record.location.line());
        } else {
            std::fprintf(stream, "[%.6fs] [%-5.*s] [%.*s] %.*s\n", sec,
                         static_cast<int>(lvlStr.size()), lvlStr.data(),
                         static_cast<int>(record.category.size()), record.category.data(),
                         static_cast<int>(record.messageLength), record.message.data());
        }

        tail_ = (tail_ + 1) % kDefaultCapacity;
        --count_;
        ++flushedCount;
    }

    std::fflush(stream);
    return flushedCount;
}

void Logger::clear() noexcept {
    head_ = 0;
    tail_ = 0;
    count_ = 0;
    droppedCount_ = 0;
}

const LogRecord* Logger::peek(size_t relativeIndex) const noexcept {
    if (relativeIndex >= count_) {
        return nullptr;
    }
    const size_t actualIndex = (tail_ + relativeIndex) % kDefaultCapacity;
    return &buffer_[actualIndex];
}

} // namespace eng
