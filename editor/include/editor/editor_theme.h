/// @file editor_theme.h
/// @brief Design token system and theme styling for the Hikari Editor.
///
/// Purpose:
/// Defines centralized design tokens (colors, layout metrics, typography scale)
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

/// @struct ThemeMetrics
/// @brief Centralized sizing, padding, and layout metrics for the Hikari Editor.
struct ThemeMetrics {
    static constexpr float kToolbarHeight = 42.0F;
    static constexpr float kStatusBarHeight = 26.0F;
    static constexpr float kWindowRounding = 0.0F;
    static constexpr float kFrameRounding = 3.0F;
    static constexpr float kPopupRounding = 4.0F;
    static constexpr float kTabRounding = 3.0F;
    static constexpr float kPropertyLabelWidth = 110.0F;
};

/// @struct ThemeColors
/// @brief Palette of centralized design tokens for the Hikari editor interface.
struct ThemeColors {
    // Surface & background tokens
    Color4f backgroundDarkest{0.059F, 0.063F, 0.075F, 1.0F};   // #0f1013 - Status bar, gutter
    Color4f backgroundApp{0.082F, 0.086F, 0.098F, 1.0F};       // #151619 - Window frame, menu bar
    Color4f backgroundWorkspace{0.102F, 0.106F, 0.122F, 1.0F}; // #1a1b1f - Dockspace canvas
    Color4f backgroundPanel{0.125F, 0.133F, 0.153F, 1.0F};     // #202227 - Panel bodies
    Color4f surfaceHeader{0.145F, 0.157F, 0.188F, 1.0F};       // #252830 - Tabs, collapsing headers
    Color4f surfaceElevated{0.165F, 0.176F, 0.212F, 1.0F};     // #2a2d36 - Cards, inputs
    Color4f surfacePopup{0.118F, 0.125F, 0.149F, 0.98F};       // #1e2026 - Popups, tooltips

    // Text & typography tokens
    Color4f textPrimary{0.953F, 0.957F, 0.965F, 1.0F};   // #f3f4f6 - Primary readable text
    Color4f textSecondary{0.612F, 0.639F, 0.686F, 1.0F}; // #9ca3af - Metadata, property labels
    Color4f textDisabled{0.294F, 0.333F, 0.388F, 1.0F};  // #4b5563 - Disabled items, hints

    // Borders & dividers
    Color4f borderSubtle{0.169F, 0.184F, 0.220F, 1.0F}; // #2b2f38 - Panel & frame dividers
    Color4f borderMedium{0.224F, 0.243F, 0.290F, 1.0F}; // #393e4a - Outlines, active tabs
    Color4f borderFocus{0.231F, 0.510F, 0.965F, 0.80F}; // Cobalt focus outline

    // Interactive & selection tokens
    Color4f buttonNormal{0.165F, 0.176F, 0.212F, 1.0F};  // #2a2d36
    Color4f buttonHovered{0.220F, 0.239F, 0.290F, 1.0F}; // #383d4a
    Color4f buttonActive{0.255F, 0.278F, 0.341F, 1.0F};  // #414757

    Color4f accentPrimary{0.231F, 0.510F, 0.965F, 1.0F};    // #3b82f6 - Cobalt active accent
    Color4f accentHovered{0.376F, 0.647F, 0.980F, 1.0F};    // #60a5fa - Cobalt light hover
    Color4f accentActive{0.145F, 0.388F, 0.922F, 1.0F};     // #2563eb - Cobalt deep active
    Color4f selectionActive{0.118F, 0.227F, 0.541F, 0.65F}; // Selection highlight

    // Transform coordinate axis colors (Unity / Unreal / Blender standard)
    Color4f colorAxisX{0.937F, 0.267F, 0.267F, 1.0F}; // #ef4444 - Red X
    Color4f colorAxisY{0.133F, 0.773F, 0.369F, 1.0F}; // #22c55e - Green Y
    Color4f colorAxisZ{0.231F, 0.510F, 0.965F, 1.0F}; // #3b82f6 - Blue Z
    Color4f colorAxisW{0.918F, 0.702F, 0.031F, 1.0F}; // #eab308 - Yellow W

    // Diagnostic & status tokens
    Color4f statusSuccess{0.063F, 0.725F, 0.506F, 1.0F}; // #10b981
    Color4f statusWarning{0.961F, 0.620F, 0.043F, 1.0F}; // #f59e0b
    Color4f statusError{0.957F, 0.247F, 0.369F, 1.0F};   // #f43f5e
    Color4f statusInfo{0.220F, 0.741F, 0.973F, 1.0F};    // #38bdf8

    /// Returns the default dark graphite theme tokens.
    [[nodiscard]] static constexpr ThemeColors defaultDark() noexcept { return ThemeColors{}; }
};

/// Applies the given theme tokens and spacing metrics to the active ImGui context.
///
/// Preconditions : An active ImGuiContext must exist on the calling thread.
/// Postconditions: ImGuiStyle colors, rounding, and padding are configured.
void applyEditorTheme(const ThemeColors& colors = ThemeColors::defaultDark());

/// Loads crisp modern desktop typography if available on the host system.
/// Gracefully falls back to default bitmap font if no TrueType font files are found.
///
/// @param fontScale DPI / user interface scaling factor (default 1.0).
/// @return True if a TrueType font was loaded; false if falling back to default.
bool loadEditorFonts(float fontScale = 1.0F);

/// Returns the currently active theme color tokens.
[[nodiscard]] const ThemeColors& getActiveThemeColors() noexcept;

} // namespace eng::editor
