/// @file budget.cpp
/// @brief Budget implementation.

#include <core/time/budget.h>

namespace eng {

Budget::Budget(Duration limit, Timestamp start) noexcept
    : start_(start), limit_(limit < Duration::zero() ? Duration::zero() : limit) {}

Duration Budget::elapsed(Timestamp now) const noexcept {
    const Duration dt = now - start_;
    return dt < Duration::zero() ? Duration::zero() : dt;
}

Duration Budget::remaining(Timestamp now) const noexcept {
    const Duration el = elapsed(now);
    if (el >= limit_) {
        return Duration::zero();
    }
    return limit_ - el;
}

bool Budget::isExpired(Timestamp now) const noexcept {
    return elapsed(now) >= limit_;
}

} // namespace eng
