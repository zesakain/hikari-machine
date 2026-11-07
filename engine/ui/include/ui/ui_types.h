/// @file ui_types.h
/// @brief Types, handles, and configuration for the RmlUi runtime user interface subsystem.
///
/// Purpose:
/// Provides hardware-neutral document handles, configuration structures, performance statistics,
/// and error codes for runtime user interface layout and event handling.
///
/// Ownership and Lifetime:
/// Value types with standard layout and zero dynamic heap allocation in the hot path.
///
/// Thread-safety:
/// Value types are safe for concurrent read access. UI system operations must occur on the main
/// thread or frame boundary.
///
/// Performance:
/// Trivially copyable structs. Zero allocation for handle manipulation and stats queries.

#pragma once

#include "core/time/duration.h"

#include <cstdint>
#include <string_view>

namespace eng::ui {

/// @struct UiDocumentHandle
/// @brief Generational identifier representing a loaded RmlUi document instance.
struct UiDocumentHandle {
    static constexpr uint64_t kInvalidId = 0;

    uint64_t id{kInvalidId};

    constexpr UiDocumentHandle() noexcept = default;
    constexpr explicit UiDocumentHandle(uint64_t inId) noexcept : id(inId) {}

    [[nodiscard]] constexpr bool isValid() const noexcept { return id != kInvalidId; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return isValid(); }
    constexpr auto operator<=>(const UiDocumentHandle&) const = default;
};

/// @struct UiConfig
/// @brief Configuration settings for initializing the UiSystem.
struct UiConfig {
    /// Initial viewport width in pixels.
    uint32_t width{1280};

    /// Initial viewport height in pixels.
    uint32_t height{720};

    /// Global typography and layout DPI scaling factor.
    float fontScale{1.0f};

    /// Path to default font asset (e.g., "assets/fonts/NotoSans-Regular.ttf").
    std::string_view defaultFontPath{};
};

/// @struct UiRenderStats
/// @brief Diagnostic counters measuring per-frame UI rendering load.
struct UiRenderStats {
    /// Number of draw calls generated during the current frame.
    uint32_t drawCalls{0};

    /// Total number of triangles processed during the current frame.
    uint32_t triangleCount{0};

    /// Number of currently active, visible UI documents.
    uint32_t activeDocuments{0};
};

/// @enum UiError
/// @brief Error codes returned by UI operations.
enum class UiError : uint8_t {
    /// Operation succeeded without error.
    None = 0,

    /// Failed to initialize RmlUi core, system interface, or render interface.
    InitializationFailed,

    /// Failed to parse or instantiate RML document.
    DocumentLoadFailed,

    /// Failed to allocate or initialize RmlUi context.
    ContextCreationFailed,

    /// The specified document handle was invalid or has already been closed.
    InvalidHandle,

    /// Error occurred while recording or submitting UI draw passes.
    RenderFailed,

    /// UI system is uninitialized.
    InvalidState
};

/// Human-readable diagnostic description of a UiError.
[[nodiscard]] constexpr std::string_view getUiErrorMessage(UiError error) noexcept {
    switch (error) {
    case UiError::None:
        return "None";
    case UiError::InitializationFailed:
        return "RmlUi runtime initialization failed";
    case UiError::DocumentLoadFailed:
        return "RML document failed to load or parse";
    case UiError::ContextCreationFailed:
        return "RmlUi context creation failed";
    case UiError::InvalidHandle:
        return "Invalid or stale document handle";
    case UiError::RenderFailed:
        return "UI rendering execution failed";
    case UiError::InvalidState:
        return "UI system is uninitialized or in an invalid state";
    }
    return "Unknown UI error";
}

} // namespace eng::ui
