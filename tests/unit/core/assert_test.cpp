/// @file assert_test.cpp
/// @brief Unit tests for ENG_ASSERT and ENG_VERIFY behaviors across configurations.

#include <core/assert.h>

int main() {
    // 1. Basic truthy verification
    ENG_VERIFY(true, "True condition must not trigger verification failure");
    ENG_ASSERT(true, "True condition must not trigger assertion failure");

    // 2. ENG_VERIFY must ALWAYS evaluate side effects across ALL build configurations
    int verifyCounter = 0;
    ENG_VERIFY(++verifyCounter == 1, "verifyCounter must increment via ENG_VERIFY");
    if (verifyCounter != 1) {
        return 1; // Side effect was not evaluated
    }

    // 3. ENG_ASSERT evaluation depends on NDEBUG
    int assertCounter = 0;
    ENG_ASSERT(++assertCounter == 1, "assertCounter condition expression");

#ifdef NDEBUG
    // In Release builds, ENG_ASSERT condition must NOT be evaluated
    if (assertCounter != 0) {
        return 2; // ENG_ASSERT unexpectedly evaluated in Release build
    }
#else
    // In Debug/Sanitizer builds, ENG_ASSERT condition MUST be evaluated
    if (assertCounter != 1) {
        return 3; // ENG_ASSERT did not evaluate in Debug build
    }
#endif

    return 0;
}
