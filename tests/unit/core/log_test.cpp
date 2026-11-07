/// @file log_test.cpp
/// @brief Comprehensive unit tests for Core structured logging subsystem.

#include <array>
#include <core/log.h>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace {

#define TEST_CHECK(condition)                                                                      \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

int testLogLevelsAndStrings() {
    TEST_CHECK(eng::toString(eng::LogLevel::Trace) == "TRACE");
    TEST_CHECK(eng::toString(eng::LogLevel::Debug) == "DEBUG");
    TEST_CHECK(eng::toString(eng::LogLevel::Info) == "INFO");
    TEST_CHECK(eng::toString(eng::LogLevel::Warning) == "WARN");
    TEST_CHECK(eng::toString(eng::LogLevel::Error) == "ERROR");
    TEST_CHECK(eng::toString(eng::LogLevel::Critical) == "CRIT");

    return 0;
}

int testLevelFiltering() {
    eng::Logger logger;
    TEST_CHECK(logger.minLogLevel() == eng::LogLevel::Info);
    TEST_CHECK(logger.isEmpty());
    TEST_CHECK(logger.count() == 0);
    TEST_CHECK(logger.droppedCount() == 0);

    // Filtered levels should return false without incrementing droppedCount_
    TEST_CHECK(!logger.log(eng::LogLevel::Trace, "Core", "Trace message"));
    TEST_CHECK(!logger.log(eng::LogLevel::Debug, "Core", "Debug message"));
    TEST_CHECK(logger.count() == 0);
    TEST_CHECK(logger.droppedCount() == 0);

    // Info and above accepted
    TEST_CHECK(logger.log(eng::LogLevel::Info, "Core", "Info message"));
    TEST_CHECK(logger.count() == 1);

    // Adjust threshold
    logger.setMinLogLevel(eng::LogLevel::Warning);
    TEST_CHECK(logger.minLogLevel() == eng::LogLevel::Warning);

    TEST_CHECK(!logger.log(eng::LogLevel::Info, "Core", "Second info"));
    TEST_CHECK(logger.count() == 1);

    TEST_CHECK(logger.log(eng::LogLevel::Warning, "Physics", "Warn message"));
    TEST_CHECK(logger.log(eng::LogLevel::Error, "Render", "Error message"));
    TEST_CHECK(logger.log(eng::LogLevel::Critical, "Conductor", "Critical message"));
    TEST_CHECK(logger.count() == 4);
    TEST_CHECK(logger.droppedCount() == 0);

    return 0;
}

int testCategoriesAndFormatting() {
    eng::Logger logger;
    logger.setMinLogLevel(eng::LogLevel::Trace);

    // Plain message recording and inspection
    TEST_CHECK(logger.log(eng::LogLevel::Info, "Render", "Buffer initialized"));
    const eng::LogRecord* rec0 = logger.peek(0);
    TEST_CHECK(rec0 != nullptr);
    TEST_CHECK(rec0->level == eng::LogLevel::Info);
    TEST_CHECK(rec0->category == "Render");
    TEST_CHECK(rec0->messageView() == "Buffer initialized");
    TEST_CHECK(rec0->messageLength == 18);

    // Printf-style formatted log message
    TEST_CHECK(logger.logFormat(eng::LogLevel::Warning, "Audio", std::source_location::current(),
                                "%s: %d channels at %d Hz", "Stereo", 2, 48000));
    const eng::LogRecord* rec1 = logger.peek(1);
    TEST_CHECK(rec1 != nullptr);
    TEST_CHECK(rec1->level == eng::LogLevel::Warning);
    TEST_CHECK(rec1->category == "Audio");
    TEST_CHECK(rec1->messageView() == "Stereo: 2 channels at 48000 Hz");

    // Out-of-bounds peek
    TEST_CHECK(logger.peek(2) == nullptr);
    TEST_CHECK(logger.peek(999) == nullptr);

    // Long message truncation test
    std::array<char, 300> longMsg{};
    longMsg.fill('X');
    longMsg.back() = '\0';

    TEST_CHECK(
        logger.log(eng::LogLevel::Info, "Core", std::string_view{longMsg.data(), longMsg.size()}));
    const eng::LogRecord* rec2 = logger.peek(2);
    TEST_CHECK(rec2 != nullptr);
    TEST_CHECK(rec2->messageLength == eng::LogRecord::kMaxMessageCapacity - 1);
    TEST_CHECK(rec2->message[eng::LogRecord::kMaxMessageCapacity - 1] == '\0');
    TEST_CHECK(rec2->message[0] == 'X');

    return 0;
}

int testBufferFullAndDroppedCount() {
    eng::Logger logger;
    logger.setMinLogLevel(eng::LogLevel::Trace);

    TEST_CHECK(eng::Logger::capacity() == 256);

    // Fill buffer completely
    for (size_t i = 0; i < eng::Logger::capacity(); ++i) {
        TEST_CHECK(!logger.isFull());
        TEST_CHECK(logger.log(eng::LogLevel::Info, "Test", "Msg"));
        TEST_CHECK(logger.count() == i + 1);
    }

    TEST_CHECK(logger.isFull());
    TEST_CHECK(logger.count() == eng::Logger::capacity());
    TEST_CHECK(logger.droppedCount() == 0);

    // Attempt overflow logging
    for (size_t i = 0; i < 15; ++i) {
        TEST_CHECK(!logger.log(eng::LogLevel::Info, "Test", "Overflow"));
        TEST_CHECK(logger.count() == eng::Logger::capacity());
        TEST_CHECK(logger.droppedCount() == i + 1);
    }

    TEST_CHECK(logger.droppedCount() == 15);

    // Formatted log should also be rejected when full
    TEST_CHECK(!logger.logFormat(eng::LogLevel::Info, "Test", std::source_location::current(),
                                 "Fmt overflow"));
    TEST_CHECK(logger.droppedCount() == 16);

    return 0;
}

int testFlushAndClear() {
    eng::Logger logger;
    logger.setMinLogLevel(eng::LogLevel::Trace);

    // Fill partially and trigger some drops
    for (size_t i = 0; i < eng::Logger::capacity(); ++i) {
        logger.log(eng::LogLevel::Info, "Bulk", "Item");
    }
    logger.log(eng::LogLevel::Error, "Bulk", "Dropped item 1");
    logger.log(eng::LogLevel::Error, "Bulk", "Dropped item 2");
    TEST_CHECK(logger.droppedCount() == 2);

    // Flush to temporary file
    std::FILE* tempFile = std::tmpfile();
    TEST_CHECK(tempFile != nullptr);

    const size_t flushed = logger.flush(tempFile);
    TEST_CHECK(flushed == eng::Logger::capacity());
    TEST_CHECK(logger.isEmpty());
    TEST_CHECK(logger.count() == 0);
    TEST_CHECK(logger.droppedCount() == 0);

    // Inspect flushed file output
    TEST_CHECK(std::fseek(tempFile, 0, SEEK_SET) == 0);
    std::array<char, 2048> readBuf{};
    const size_t bytesRead = std::fread(readBuf.data(), 1, readBuf.size() - 1, tempFile);
    readBuf[bytesRead] = '\0';
    std::fclose(tempFile);

    // Check that warning about dropped records is present
    const std::string_view output{readBuf.data(), bytesRead};
    TEST_CHECK(output.find("2 log record(s) dropped") != std::string_view::npos);
    TEST_CHECK(output.find("[INFO ] [Bulk] Item") != std::string_view::npos);

    // Test clear()
    logger.log(eng::LogLevel::Info, "Test", "Clear me");
    TEST_CHECK(logger.count() == 1);
    logger.clear();
    TEST_CHECK(logger.isEmpty());
    TEST_CHECK(logger.count() == 0);
    TEST_CHECK(logger.droppedCount() == 0);

    return 0;
}

int testDefaultLoggerAndMacros() {
    eng::Logger& def = eng::Logger::defaultLogger();
    def.clear();
    def.setMinLogLevel(eng::LogLevel::Trace);

    ENG_LOG_TRACE("TraceCat", "Trace entry");
    ENG_LOG_DEBUG("DebugCat", "Debug entry");
    ENG_LOG_INFO("InfoCat", "Info entry");
    ENG_LOG_WARN("WarnCat", "Warn entry");
    ENG_LOG_ERROR("ErrorCat", "Error entry");
    ENG_LOG_CRITICAL("CritCat", "Critical entry");

    TEST_CHECK(def.count() == 6);
    TEST_CHECK(def.peek(0)->category == "TraceCat");
    TEST_CHECK(def.peek(1)->category == "DebugCat");
    TEST_CHECK(def.peek(2)->category == "InfoCat");
    TEST_CHECK(def.peek(3)->category == "WarnCat");
    TEST_CHECK(def.peek(4)->category == "ErrorCat");
    TEST_CHECK(def.peek(5)->category == "CritCat");

    // Format macro
    ENG_LOG_FORMAT(eng::LogLevel::Info, "FmtCat", "Value=%d", 1234);
    TEST_CHECK(def.count() == 7);
    TEST_CHECK(def.peek(6)->messageView() == "Value=1234");

    // Reset default logger state for clean test environment
    def.clear();
    def.setMinLogLevel(eng::LogLevel::Info);

    return 0;
}

} // namespace

int main() {
    if (testLogLevelsAndStrings() != 0) {
        return 1;
    }
    if (testLevelFiltering() != 0) {
        return 2;
    }
    if (testCategoriesAndFormatting() != 0) {
        return 3;
    }
    if (testBufferFullAndDroppedCount() != 0) {
        return 4;
    }
    if (testFlushAndClear() != 0) {
        return 5;
    }
    if (testDefaultLoggerAndMacros() != 0) {
        return 6;
    }

    return 0;
}
