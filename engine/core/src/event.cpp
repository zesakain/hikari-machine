/// @file event.cpp
/// @brief Core event system type ID generator implementation.

#include <core/event.h>

namespace eng::detail {

uint32_t getNextEventTypeId() noexcept {
    static uint32_t sNextId = 1;
    return sNextId++;
}

} // namespace eng::detail
