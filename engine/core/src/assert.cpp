/// @file assert.cpp
/// @brief Assertion failure diagnostic reporting implementation.

#include <core/assert.h>
#include <cstdio>
#include <cstdlib>

namespace eng::detail {

[[noreturn]] void handleAssertFailure(std::string_view condition, std::string_view message,
                                      std::source_location location) noexcept {
    // Print diagnostic directly to stderr without heap allocations.
    // INVARIANT: Format string and buffers use stack/direct stream output to avoid allocator calls.
    std::fprintf(stderr,
                 "\n[FATAL ASSERTION FAILURE]\n"
                 "  File     : %s:%u\n"
                 "  Function : %s\n"
                 "  Condition: %.*s\n"
                 "  Message  : %.*s\n\n",
                 location.file_name(), location.line(), location.function_name(),
                 static_cast<int>(condition.size()), condition.data(),
                 static_cast<int>(message.size()), message.data());

    std::fflush(stderr);
    std::abort();
}

} // namespace eng::detail
