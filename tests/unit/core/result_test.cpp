/// @file result_test.cpp
/// @brief Unit tests for eng::Result and eng::Error.

#include <core/error.h>
#include <core/result.h>
#include <memory>
#include <string_view>
#include <utility>

namespace {

// Move-only test type to verify move-semantics support without dynamic heap allocations.
struct MoveOnly {
    int value{0};

    explicit MoveOnly(int val) noexcept : value(val) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&& other) noexcept : value(other.value) { other.value = -1; }
    MoveOnly& operator=(MoveOnly&& other) noexcept {
        if (this != &other) {
            value = other.value;
            other.value = -1;
        }
        return *this;
    }
    ~MoveOnly() = default;
};

// Simple non-throwing assertion helper for tests
bool testFailed = false;

#define TEST_CHECK(condition)                                                                      \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            testFailed = true;                                                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

int testValueSuccess() {
    const eng::Result<int> res(42);
    TEST_CHECK(res.hasValue());
    TEST_CHECK(!res.isError());
    TEST_CHECK(static_cast<bool>(res));
    TEST_CHECK(res.value() == 42);
    return 0;
}

int testValueFailure() {
    const eng::Error err{.code = eng::ErrorCode::InvalidArgument,
                         .message = "Test failure message"};
    const eng::Result<int> res(err);
    TEST_CHECK(!res.hasValue());
    TEST_CHECK(res.isError());
    TEST_CHECK(!static_cast<bool>(res));
    TEST_CHECK(res.error().code == eng::ErrorCode::InvalidArgument);
    TEST_CHECK(res.error().message == "Test failure message");
    return 0;
}

int testVoidSuccess() {
    const eng::Result<void> res = eng::Result<void>::success();
    TEST_CHECK(res.hasValue());
    TEST_CHECK(!res.isError());
    TEST_CHECK(static_cast<bool>(res));
    return 0;
}

int testVoidFailure() {
    const eng::Error err{.code = eng::ErrorCode::OutOfMemory, .message = "Arena capacity reached"};
    const eng::Result<void> res(err);
    TEST_CHECK(!res.hasValue());
    TEST_CHECK(res.isError());
    TEST_CHECK(!static_cast<bool>(res));
    TEST_CHECK(res.error().code == eng::ErrorCode::OutOfMemory);
    TEST_CHECK(res.error().message == "Arena capacity reached");
    return 0;
}

int testMoveOnlyValue() {
    eng::Result<MoveOnly> res(MoveOnly(100));
    TEST_CHECK(res.hasValue());
    TEST_CHECK(res.value().value == 100);

    // Test moving out of Result
    MoveOnly extracted = std::move(res).value();
    TEST_CHECK(extracted.value == 100);
    return 0;
}

int testMoveOnlyResult() {
    eng::Result<MoveOnly> original(MoveOnly(77));
    eng::Result<MoveOnly> movedDestination(std::move(original));
    TEST_CHECK(movedDestination.hasValue());
    TEST_CHECK(movedDestination.value().value == 77);
    return 0;
}

int testFactoryMethods() {
    const auto valSuccess = eng::Result<int>::success(123);
    TEST_CHECK(valSuccess.hasValue());
    TEST_CHECK(valSuccess.value() == 123);

    const auto valFailure = eng::Result<int>::failure(
        eng::Error{.code = eng::ErrorCode::OutOfBounds, .message = "Out of range"});
    TEST_CHECK(valFailure.isError());
    TEST_CHECK(valFailure.error().code == eng::ErrorCode::OutOfBounds);

    const auto voidSuccess = eng::Result<void>::success();
    TEST_CHECK(voidSuccess.hasValue());

    const auto voidFailure = eng::Result<void>::failure(
        eng::Error{.code = eng::ErrorCode::InvalidState, .message = "Not ready"});
    TEST_CHECK(voidFailure.isError());
    TEST_CHECK(voidFailure.error().code == eng::ErrorCode::InvalidState);

    return 0;
}

int testConstAccessors() {
    const eng::Result<int> res(55);
    const int& valRef = res.value();
    TEST_CHECK(valRef == 55);

    const eng::Result<int> errRes(
        eng::Error{.code = eng::ErrorCode::NotSupported, .message = "Unsupported"});
    const eng::Error& errRef = errRes.error();
    TEST_CHECK(errRef.code == eng::ErrorCode::NotSupported);
    return 0;
}

} // namespace

int main() {
    if (testValueSuccess() != 0) {
        return 1;
    }
    if (testValueFailure() != 0) {
        return 2;
    }
    if (testVoidSuccess() != 0) {
        return 3;
    }
    if (testVoidFailure() != 0) {
        return 4;
    }
    if (testMoveOnlyValue() != 0) {
        return 5;
    }
    if (testMoveOnlyResult() != 0) {
        return 6;
    }
    if (testFactoryMethods() != 0) {
        return 7;
    }
    if (testConstAccessors() != 0) {
        return 8;
    }

    return 0;
}
