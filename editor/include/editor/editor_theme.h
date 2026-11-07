/// @file editor_theme.h
/// @brief Design token system and theme styling for the Hikari Editor.
///
/// Purpose:
/// Defines centralized design tokens (colors, layout metrics, typography parameters)
/// and applies the official dark graphite visual identity to Dear ImGui.
///
/// Invariants & Rules:
/// - Built ONLY when HIKARI_BUILD_EDITOR=ON.
/// - Encapsulates ImGui types within private implementation; public API uses POD color structs.
/// - Exception-free and RTTI-free.
///
/// Thread Safety:
/// Main thread only (applies to active ImGuiContext).

#pragma once

#include <cstdint>

namespace eng::editor {

/// @struct Color4f
/// @brief Normalized RGBA color token.
struct Color4f {
    float r{0.0F};
    float g{0.0F};
    float b{0.0F};
    float a{1.0F};

    constexpr Color4f() noexcept = default;
    constexpr Color4f(float red, float green, float blue, float alpha = 1.0F) noexcept
        : r(red), g(green), b(blue), a(alpha) {}

    /// Converts normalized color to 32-bit packed RGBA.
    [[nodiscard]] constexpr uint32_t toRgba32() const noexcept {
        auto ur = static_cast<uint32_t>(r * 255.0F + 0.5F) & 0xFFU;
        auto ug = static_cast<uint32_t>(g * 255.0F + 0.5F) & 0xFFU;
        auto ub = static_cast<uint32_t>(b * 255.0F + 0.5F) & 0xFFU;
        auto ua = static_cast<uint32_t>(a * 255.0F + 0.5F) & 0xFFU;
        return (ua << 24U) | (ub << 16U) | (ug << 8U) | ur;
    }
};

/// @struct ThemeColors
/// @brief Palette of centralized design tokens for the Hikari editor interface.
struct ThemeColors {
    // Surface & background tokens
    Color4f backgroundApp{0.086F, 0.090F, 0.098F, 1.0F};       // #161719 - Main application canvas
    Color4f backgroundWorkspace{0.102F, 0.106F, 0.118F, 1.0F}; // #1a1b1e - Dockspace canvas
    Color4f backgroundPanel{0.125F, 0.133F, 0.149F, 1.0F};     // #202226 - Panel bodies
    Color4f surfaceElevated{0.153F, 0.165F, 0.188F, 1.0F};     // #272a30 - Headers, cards, toolbars
    Color4f surfacePopup{0.133F, 0.141F, 0.161F, 0.98F};       // #222429 - Context menus, popups

    // Text & typography tokens
    Color4f textPrimary{0.910F, 0.920F, 0.935F, 1.0F};   // #e8ebef - Primary readable text
    Color4f textSecondary{0.615F, 0.640F, 0.682F, 1.0F}; // #9da3ae - Metadata, captions, headers
    Color4f textDisabled{0.360F, 0.380F, 0.420F, 1.0F};  // #5c616b - Disabled items, inactive tabs

    // Borders & dividers
    Color4f borderSubtle{0.180F, 0.196F, 0.231F, 1.0F}; // #2e323b - Panel & frame dividers
    Color4f borderFocus{0.231F, 0.510F, 0.965F, 0.65F}; // Cobalt focus outline

    // Interactive & selection tokens
    Color4f buttonNormal{0.153F, 0.165F, 0.196F, 1.0F};  // #272a32
    Color4f buttonHovered{0.200F, 0.216F, 0.259F, 1.0F}; // #333742
    Color4f buttonActive{0.247F, 0.267F, 0.322F, 1.0F};  // #3f4452

    Color4f accentPrimary{0.231F, 0.510F, 0.965F, 1.0F};   // #3b82f6 - Cobalt active accent
    Color4f accentHovered{0.376F, 0.647F, 0.980F, 1.0F};   // #60a5fa - Cobalt light hover
    Color4f selectionActive{0.118F, 0.227F, 0.541F, 0.7F}; // #1e3a8a - Selection highlight

    // Diagnostic & status tokens
    Color4f statusSuccess{0.063F, 0.725F, 0.506F, 1.0F}; // #10b981
    Color4f statusWarning{0.961F, 0.620F, 0.043F, 1.0F}; // #f59e0b
    Color4f statusError{0.937F, 0.267F, 0.267F, 1.0F};   // #ef4444
    Color4f statusInfo{0.220F, 0.741F, 0.973F, 1.0F};    // #38bdf8

    /// Returns the default dark graphite theme tokens.
    [[nodiscard]] static constexpr ThemeColors defaultDark() noexcept { return ThemeColors{}; }
};

/// Applies the given theme tokens and spacing metrics to the active ImGui context.
///
/// Preconditions : An active ImGuiContext must exist on the calling thread.
/// Postconditions: ImGuiStyle colors, rounding, and padding are configured.
void applyEditorTheme(const ThemeColors& colors = ThemeColors::defaultDark());

/// Returns the currently active theme color tokens.
[[nodiscard]] const ThemeColors& getActiveThemeColors() noexcept;

} // namespace eng::editor
