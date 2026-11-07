/// @file rmlui_system_backend.cpp
/// @brief Implementation of RmlUi SystemInterface for Hikari Engine.

#include "rmlui_system_backend.h"

#include "core/log.h"

namespace eng::ui {

HikariSystemInterface::HikariSystemInterface() = default;

double HikariSystemInterface::GetElapsedTime() {
    return elapsedTime_;
}

void HikariSystemInterface::advanceTime(double deltaSeconds) {
    if (deltaSeconds > 0.0) {
        elapsedTime_ += deltaSeconds;
    }
}

bool HikariSystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message) {
    switch (type) {
    case Rml::Log::LT_INFO:
        ENG_LOG_INFO("RmlUi", message);
        break;
    case Rml::Log::LT_WARNING:
        ENG_LOG_WARN("RmlUi", message);
        break;
    case Rml::Log::LT_ERROR:
        ENG_LOG_ERROR("RmlUi", message);
        break;
    case Rml::Log::LT_ASSERT:
        ENG_LOG_CRITICAL("RmlUi", message);
        break;
    case Rml::Log::LT_ALWAYS:
    case Rml::Log::LT_DEBUG:
    default:
        ENG_LOG_DEBUG("RmlUi", message);
        break;
    }
    return true;
}

} // namespace eng::ui
