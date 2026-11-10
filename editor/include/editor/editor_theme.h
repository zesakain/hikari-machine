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

/// @enum ThemeType
/// @brief Available visual theme presets for the Hikari Editor.
enum class ThemeType : uint8_t {
    Dark = 0,     ///< Hikari Dark (graphite and charcoal, default).
    Midnight = 1, ///< Hikari Midnight (deep obsidian with electric cyan accent).
    Light = 2,    ///< Hikari Light (studio neutral light with dark typography).
};

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

    /// Returns a copy of this color with a modified alpha value.
    [[nodiscard]] constexpr Color4f withAlpha(float newAlpha) const noexcept {
        return Color4f(r, g, b, newAlpha);
    }

    /// Returns perceived ITU-R BT.709 relative luminance.
    [[nodiscard]] constexpr float luminance() const noexcept {
        return (0.2126F * r) + (0.7152F * g) + (0.0722F * b);
    }

    /// Returns true if this color is considered light (luminance > 0.5).
    [[nodiscard]] constexpr bool isLight() const noexcept { return luminance() > 0.5F; }
};

/// @struct ThemeMetrics
/// @brief Centralized sizing, padding, and layout metrics for the Hikari Editor.
struct ThemeMetrics {
    // Header & bar dimensions
    static constexpr float kToolbarHeight = 42.0F;
    static constexpr float kStatusBarHeight = 28.0F;
    static constexpr float kMenuBarHeight = 28.0F;

    // Corner rounding scale
    static constexpr float kWindowRounding = 0.0F;
    static constexpr float kChildRounding = 4.0F;
    static constexpr float kFrameRounding = 4.0F;
    static constexpr float kPopupRounding = 6.0F;
    static constexpr float kTabRounding = 4.0F;
    static constexpr float kScrollbarRounding = 4.0F;
    static constexpr float kGrabRounding = 3.0F;

    // Border thickness
    static constexpr float kWindowBorderSize = 1.0F;
    static constexpr float kChildBorderSize = 1.0F;
    static constexpr float kPopupBorderSize = 1.0F;
    static constexpr float kFrameBorderSize = 1.0F;
    static constexpr float kTabBorderSize = 1.0F;

    // Control sizing
    static constexpr float kButtonHeightStandard = 26.0F;
    static constexpr float kButtonHeightCompact = 22.0F;
    static constexpr float kPropertyLabelWidth = 115.0F;

    // Spacing scale (8pt grid system)
    static constexpr float kSpacingXXS = 2.0F;
    static constexpr float kSpacingXS = 4.0F;
    static constexpr float kSpacingS = 6.0F;
    static constexpr float kSpacingM = 8.0F;
    static constexpr float kSpacingL = 12.0F;
    static constexpr float kSpacingXL = 16.0F;
    static constexpr float kSpacingXXL = 24.0F;
};

/// @struct ThemeColors
/// @brief Palette of centralized semantic design tokens for the Hikari editor interface.
struct ThemeColors {
    // Surface & background tokens
    Color4f backgroundDarkest{0.043F, 0.047F, 0.059F, 1.0F};   // Status bar, gutter
    Color4f backgroundApp{0.063F, 0.071F, 0.086F, 1.0F};       // Window frame, menu bar
    Color4f backgroundWorkspace{0.078F, 0.086F, 0.106F, 1.0F}; // Dockspace canvas
    Color4f backgroundPanel{0.098F, 0.106F, 0.129F, 1.0F};     // Panel bodies
    Color4f surfaceHeader{0.125F, 0.137F, 0.169F, 1.0F};       // Tabs, collapsing headers
    Color4f surfaceElevated{0.149F, 0.165F, 0.200F, 1.0F};     // Cards, inputs, controls
    Color4f surfacePopup{0.086F, 0.094F, 0.118F, 0.98F};       // Popups, tooltips, dialogs
    Color4f surfaceCard{0.082F, 0.090F, 0.110F, 1.0F};         // Nested inspection cards
    Color4f surfaceHighlight{0.180F, 0.200F, 0.243F, 1.0F};    // Subtle element highlighting

    // Text & typography tokens
    Color4f textPrimary{0.949F, 0.957F, 0.969F, 1.0F};   // Primary readable text
    Color4f textSecondary{0.588F, 0.627F, 0.690F, 1.0F}; // Metadata, property labels
    Color4f textMuted{0.400F, 0.435F, 0.490F, 1.0F};     // Placeholder, secondary hints
    Color4f textDisabled{0.275F, 0.306F, 0.353F, 1.0F};  // Disabled items, shortcuts
    Color4f textInverse{0.043F, 0.047F, 0.059F, 1.0F};   // Text on bright accents

    // Borders & dividers
    Color4f borderSubtle{0.149F, 0.165F, 0.200F, 1.0F}; // Panel & frame dividers
    Color4f borderMedium{0.208F, 0.231F, 0.278F, 1.0F}; // Outlines, active tabs
    Color4f borderFocus{0.055F, 0.647F, 0.914F, 0.85F}; // Input focus outline
    Color4f borderActive{0.220F, 0.741F, 0.973F, 0.90F};

    // Interactive & selection tokens
    Color4f buttonNormal{0.137F, 0.153F, 0.184F, 1.0F};
    Color4f buttonHovered{0.180F, 0.200F, 0.243F, 1.0F};
    Color4f buttonActive{0.220F, 0.243F, 0.298F, 1.0F};
    Color4f buttonPrimary{0.055F, 0.647F, 0.914F, 1.0F};
    Color4f buttonPrimaryHovered{0.220F, 0.741F, 0.973F, 1.0F};
    Color4f buttonPrimaryActive{0.008F, 0.518F, 0.780F, 1.0F};

    // Accent tokens (Hikari Studio Electric Cyan / Blue)
    Color4f accentPrimary{0.055F, 0.647F, 0.914F, 1.0F};    // #0ea5e9 - Primary accent
    Color4f accentHovered{0.220F, 0.741F, 0.973F, 1.0F};    // #38bdf8 - Lighter hover accent
    Color4f accentActive{0.008F, 0.518F, 0.780F, 1.0F};     // #0284c7 - Deep active accent
    Color4f accentSubtle{0.055F, 0.647F, 0.914F, 0.15F};    // Transparent wash
    Color4f selectionActive{0.055F, 0.647F, 0.914F, 0.30F}; // Selection highlight

    // Transport simulation controls
    Color4f transportPlay{0.063F, 0.725F, 0.506F, 1.0F};   // Green
    Color4f transportPause{0.961F, 0.620F, 0.043F, 1.0F};  // Amber
    Color4f transportResume{0.918F, 0.702F, 0.031F, 1.0F}; // Yellow
    Color4f transportStep{0.220F, 0.741F, 0.973F, 1.0F};   // Light blue
    Color4f transportStop{0.937F, 0.267F, 0.267F, 1.0F};   // Red

    // Transform coordinate axis colors (standard 3D engine conventions)
    Color4f colorAxisX{0.937F, 0.267F, 0.267F, 1.0F}; // #ef4444 - Red X
    Color4f colorAxisY{0.133F, 0.773F, 0.369F, 1.0F}; // #22c55e - Green Y
    Color4f colorAxisZ{0.231F, 0.510F, 0.965F, 1.0F}; // #3b82f6 - Blue Z
    Color4f colorAxisW{0.918F, 0.702F, 0.031F, 1.0F}; // #eab308 - Yellow W

    // Diagnostic & status tokens
    Color4f statusSuccess{0.063F, 0.725F, 0.506F, 1.0F}; // #10b981
    Color4f statusWarning{0.961F, 0.620F, 0.043F, 1.0F}; // #f59e0b
    Color4f statusError{0.957F, 0.247F, 0.369F, 1.0F};   // #f43f5e
    Color4f statusInfo{0.220F, 0.741F, 0.973F, 1.0F};    // #38bdf8

    // Asset & format badges
    Color4f badgeGlb{0.20F, 0.85F, 0.45F, 1.0F};     // Soft cyan/green
    Color4f badgeGltf{0.95F, 0.55F, 0.20F, 1.0F};    // Amber
    Color4f badgeObj{0.20F, 0.70F, 0.95F, 1.0F};     // Periwinkle
    Color4f badgeScript{0.75F, 0.52F, 0.99F, 1.0F};  // Violet
    Color4f badgePhysics{0.98F, 0.45F, 0.09F, 1.0F}; // Orange
    Color4f badgeActive{0.063F, 0.725F, 0.506F, 1.0F};
    Color4f badgeStandby{0.961F, 0.620F, 0.043F, 1.0F};

    // Theme Preset Factories
    [[nodiscard]] static constexpr ThemeColors hikariDark() noexcept { return ThemeColors{}; }

    [[nodiscard]] static constexpr ThemeColors hikariMidnight() noexcept {
        ThemeColors c{};
        c.backgroundDarkest = Color4f{0.020F, 0.024F, 0.031F, 1.0F};
        c.backgroundApp = Color4f{0.035F, 0.043F, 0.055F, 1.0F};
        c.backgroundWorkspace = Color4f{0.051F, 0.063F, 0.078F, 1.0F};
        c.backgroundPanel = Color4f{0.071F, 0.082F, 0.106F, 1.0F};
        c.surfaceHeader = Color4f{0.086F, 0.106F, 0.133F, 1.0F};
        c.surfaceElevated = Color4f{0.102F, 0.125F, 0.157F, 1.0F};
        c.surfacePopup = Color4f{0.047F, 0.055F, 0.071F, 0.98F};
        c.surfaceCard = Color4f{0.059F, 0.071F, 0.090F, 1.0F};
        c.surfaceHighlight = Color4f{0.133F, 0.161F, 0.200F, 1.0F};

        c.textPrimary = Color4f{0.941F, 0.965F, 0.988F, 1.0F};
        c.textSecondary = Color4f{0.545F, 0.580F, 0.620F, 1.0F};
        c.textMuted = Color4f{0.431F, 0.463F, 0.506F, 1.0F};
        c.textDisabled = Color4f{0.282F, 0.310F, 0.345F, 1.0F};
        c.textInverse = Color4f{0.004F, 0.016F, 0.035F, 1.0F};

        c.borderSubtle = Color4f{0.122F, 0.141F, 0.173F, 1.0F};
        c.borderMedium = Color4f{0.188F, 0.212F, 0.239F, 1.0F};
        c.borderFocus = Color4f{0.220F, 0.741F, 0.973F, 0.85F};
        c.borderActive = Color4f{0.490F, 0.827F, 0.988F, 0.90F};

        c.buttonNormal = Color4f{0.086F, 0.106F, 0.133F, 1.0F};
        c.buttonHovered = Color4f{0.129F, 0.149F, 0.176F, 1.0F};
        c.buttonActive = Color4f{0.188F, 0.212F, 0.239F, 1.0F};

        c.buttonPrimary = Color4f{0.008F, 0.518F, 0.780F, 1.0F};
        c.buttonPrimaryHovered = Color4f{0.012F, 0.412F, 0.631F, 1.0F};
        c.buttonPrimaryActive = Color4f{0.027F, 0.349F, 0.522F, 1.0F};

        c.accentPrimary = Color4f{0.220F, 0.741F, 0.973F, 1.0F}; // Luminous cyan
        c.accentHovered = Color4f{0.490F, 0.827F, 0.988F, 1.0F};
        c.accentActive = Color4f{0.055F, 0.647F, 0.914F, 1.0F};
        c.accentSubtle = Color4f{0.220F, 0.741F, 0.973F, 0.15F};
        c.selectionActive = Color4f{0.035F, 0.353F, 0.525F, 0.65F};
        return c;
    }

    [[nodiscard]] static constexpr ThemeColors hikariLight() noexcept {
        ThemeColors c{};
        c.backgroundDarkest = Color4f{0.886F, 0.894F, 0.910F, 1.0F};   // #e2e4e8
        c.backgroundApp = Color4f{0.918F, 0.922F, 0.937F, 1.0F};       // #eaebef
        c.backgroundWorkspace = Color4f{0.929F, 0.933F, 0.949F, 1.0F}; // #edeef2
        c.backgroundPanel = Color4f{0.957F, 0.961F, 0.973F, 1.0F};     // #f4f5f8
        c.surfaceHeader = Color4f{0.890F, 0.898F, 0.918F, 1.0F};       // #e3e5ea
        c.surfaceElevated = Color4f{1.000F, 1.000F, 1.000F, 1.0F};     // #ffffff
        c.surfacePopup = Color4f{1.000F, 1.000F, 1.000F, 0.98F};
        c.surfaceCard = Color4f{0.976F, 0.980F, 0.984F, 1.0F};      // #f9fafb
        c.surfaceHighlight = Color4f{0.859F, 0.918F, 0.996F, 1.0F}; // #dbeafe

        c.textPrimary = Color4f{0.067F, 0.094F, 0.153F, 1.0F};   // #111827
        c.textSecondary = Color4f{0.294F, 0.333F, 0.388F, 1.0F}; // #4b5563
        c.textMuted = Color4f{0.420F, 0.447F, 0.502F, 1.0F};     // #6b7280
        c.textDisabled = Color4f{0.612F, 0.639F, 0.686F, 1.0F};  // #9ca3af
        c.textInverse = Color4f{1.000F, 1.000F, 1.000F, 1.0F};

        c.borderSubtle = Color4f{0.820F, 0.835F, 0.859F, 1.0F}; // #d1d5db
        c.borderMedium = Color4f{0.612F, 0.639F, 0.686F, 1.0F}; // #9ca3af
        c.borderFocus = Color4f{0.145F, 0.388F, 0.922F, 0.85F}; // #2563eb
        c.borderActive = Color4f{0.114F, 0.306F, 0.847F, 0.90F};

        c.buttonNormal = Color4f{0.898F, 0.906F, 0.922F, 1.0F};  // #e5e7eb
        c.buttonHovered = Color4f{0.820F, 0.835F, 0.859F, 1.0F}; // #d1d5db
        c.buttonActive = Color4f{0.612F, 0.639F, 0.686F, 1.0F};  // #9ca3af

        c.buttonPrimary = Color4f{0.145F, 0.388F, 0.922F, 1.0F};        // #2563eb
        c.buttonPrimaryHovered = Color4f{0.114F, 0.306F, 0.847F, 1.0F}; // #1d4ed8
        c.buttonPrimaryActive = Color4f{0.118F, 0.251F, 0.686F, 1.0F};  // #1e40af

        c.accentPrimary = Color4f{0.145F, 0.388F, 0.922F, 1.0F}; // #2563eb - Cobalt
        c.accentHovered = Color4f{0.231F, 0.510F, 0.965F, 1.0F};
        c.accentActive = Color4f{0.114F, 0.306F, 0.847F, 1.0F};
        c.accentSubtle = Color4f{0.145F, 0.388F, 0.922F, 0.15F};
        c.selectionActive = Color4f{0.753F, 0.855F, 0.996F, 0.65F};

        c.transportPlay = Color4f{0.020F, 0.588F, 0.412F, 1.0F};   // #059669
        c.transportPause = Color4f{0.851F, 0.467F, 0.024F, 1.0F};  // #d97706
        c.transportResume = Color4f{0.792F, 0.541F, 0.016F, 1.0F}; // #ca8a04
        c.transportStep = Color4f{0.008F, 0.518F, 0.780F, 1.0F};   // #0284c7
        c.transportStop = Color4f{0.863F, 0.149F, 0.149F, 1.0F};   // #dc2626

        c.statusSuccess = Color4f{0.020F, 0.588F, 0.412F, 1.0F};
        c.statusWarning = Color4f{0.851F, 0.467F, 0.024F, 1.0F};
        c.statusError = Color4f{0.863F, 0.149F, 0.149F, 1.0F};
        c.statusInfo = Color4f{0.008F, 0.518F, 0.780F, 1.0F};
        return c;
    }

    /// Returns the default dark graphite theme tokens.
    [[nodiscard]] static constexpr ThemeColors defaultDark() noexcept { return hikariDark(); }
};

/// Applies the given theme preset to the active ImGui context.
void applyEditorTheme(ThemeType type);

/// Applies the given custom theme tokens and spacing metrics to the active ImGui context.
void applyEditorTheme(const ThemeColors& colors);

/// Returns the currently active theme preset type.
[[nodiscard]] ThemeType getActiveThemeType() noexcept;

/// Returns the currently active theme color tokens.
[[nodiscard]] const ThemeColors& getActiveThemeColors() noexcept;

/// Returns the human-readable display name of a theme preset.
[[nodiscard]] const char* getThemeName(ThemeType type) noexcept;

/// Loads crisp modern desktop typography if available on the host system.
bool loadEditorFonts(float fontScale = 1.0F);

} // namespace eng::editor
