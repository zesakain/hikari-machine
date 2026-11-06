/// @file smoke_test.cpp
/// @brief Dependency-free build and compiler policy smoke test.
///
/// Purpose:
/// Validates Phase 0 compiler settings, C++20 standard compliance, and engine
/// architecture constraints (no exceptions, no RTTI, standard layout handles)
/// without relying on any third-party test framework dependencies.
///
/// Thread-safety: Main thread single-shot validation.
/// Performance: Microsecond execution; zero dynamic allocations.

#include <concepts>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

// INVARIANT: Engine targets must never enable C++ exceptions.
// Symptom if violated: Build error below; prevents hidden unwinding table overhead.
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#error "Engine target violation: C++ exceptions are enabled (-fno-exceptions /EHs-c- required)"
#endif

// INVARIANT: Engine targets must never enable C++ runtime type information (RTTI).
// Symptom if violated: Build error below; prevents vtable typeinfo bloat and dynamic_cast usage.
#if defined(__cpp_rtti) || defined(_CPPRTTI)
#error "Engine target violation: C++ RTTI is enabled (-fno-rtti /GR- required)"
#endif

namespace eng::test {

/// @brief Concept verifying that a type supports standard arithmetic operations.
/// Used here to prove C++20 concept compilation under the active toolchain.
template <typename T>
concept Arithmetic = std::is_arithmetic_v<T>;

/// @brief Representative generational handle POD layout.
/// Encodes an index and a generation counter in 64 bits to validate handle alignment.
struct Handle {
    uint32_t index{0};
    uint32_t generation{0};

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return generation != 0;
    }
};

static_assert(std::is_standard_layout_v<Handle>,
              "Generational handles must be standard layout PODs for cache efficiency");
static_assert(sizeof(Handle) == 8,
              "Handle size must be exactly 8 bytes (32-bit index + 32-bit generation)");

/// @brief Computes a sum over a non-owning span of arithmetic values.
///
/// Purpose       : Validates C++20 std::span and concepts in an allocation-free function.
/// Preconditions : Span must reference valid contiguous memory.
/// Thread-safety : Safe to call concurrently from any thread; reads data immutably.
/// Performance   : O(N) where N is span size. Zero heap allocations.
///
/// @tparam T Arithmetic value type.
/// @param values Non-owning view of contiguous values.
/// @return Accumulated sum of values.
template <Arithmetic T>
[[nodiscard]] constexpr T accumulateSpan(std::span<const T> values) noexcept {
    T sum{};
    for (const T& val : values) {
        sum += val;
    }
    return sum;
}

} // namespace eng::test

int main() {
    using namespace eng::test;

    // 1. Verify C++20 std::span and concepts execution
    constexpr int32_t kRawNumbers[] = {1, 2, 3, 4, 5};
    const std::span<const int32_t> numberSpan{kRawNumbers};
    const int32_t total = accumulateSpan(numberSpan);

    if (total != 15) {
        return 1; // Sum mismatch
    }

    // 2. Verify std::string_view constexpr parsing
    constexpr std::string_view kEngineName = "Hikari-Machine";
    if (kEngineName.empty() || kEngineName.length() != 14) {
        return 2; // String view validation failure
    }

    // 3. Verify std::optional semantics without exceptions
    const std::optional<Handle> testHandle = Handle{.index = 42, .generation = 1};
    if (!testHandle.has_value() || !testHandle->isValid()) {
        return 3; // Handle validation failure
    }

    if (testHandle->index != 42 || testHandle->generation != 1) {
        return 4; // Handle layout corruption
    }

    // All Phase 0 compiler and language checks passed
    return 0;
}
