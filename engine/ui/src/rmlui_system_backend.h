/// @file rmlui_system_backend.h
/// @brief RmlUi SystemInterface implementation bridging logging and timing.

#pragma once

#include <RmlUi/Core/SystemInterface.h>

namespace eng::ui {

class HikariSystemInterface final : public Rml::SystemInterface {
public:
    HikariSystemInterface();
    ~HikariSystemInterface() override = default;

    double GetElapsedTime() override;
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;

    void advanceTime(double deltaSeconds);

private:
    double elapsedTime_{0.0};
};

} // namespace eng::ui
