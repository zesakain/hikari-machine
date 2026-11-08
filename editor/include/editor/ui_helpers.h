/// @file ui_helpers.h
/// @brief Reusable UI drawing helpers, semantic components, and iconography tokens.
///
/// Purpose:
/// Provides standardized design-system widgets (badges, status pills, section headers,
/// property labels, cards, and buttons) consuming centralized ThemeColors and ThemeMetrics.
///
/// Invariants & Rules:
/// - Built ONLY when HIKARI_BUILD_EDITOR=ON.
/// - Encapsulates ImGui types within private implementation; public API uses POD types.
/// - Exception-free and RTTI-free.
///
/// Thread Safety:
/// Main thread only (invokes Dear ImGui draw commands).

#pragma once

#include "editor/editor_theme.h"

namespace eng::editor {

/// @struct EditorIcons
/// @brief Standardized Unicode and ASCII icon glyphs for consistent editor iconography.
struct EditorIcons {
    // Simulation transport
    static constexpr const char* kPlay = "▶";
    static constexpr const char* kPause = "⏸";
    static constexpr const char* kResume = "⏩";
    static constexpr const char* kStep = "⏭";
    static constexpr const char* kStop = "⏹";

    // Viewport & camera
    static constexpr const char* kCamera = "🎥";
    static constexpr const char* kOrbit = "↺";
    static constexpr const char* kFly = "✈";
    static constexpr const char* kGpu = "⚡";
    static constexpr const char* kCpu = "💻";
    static constexpr const char* kGrid = "⊞";

    // Content & assets
    static constexpr const char* kFolder = "📁";
    static constexpr const char* kFile = "📄";
    static constexpr const char* kMesh = "🧊";
    static constexpr const char* kScene = "🎬";
    static constexpr const char* kTexture = "🖼";
    static constexpr const char* kScript = "📜";
    static constexpr const char* kAudio = "🔊";
    static constexpr const char* kPhysics = "⚛";

    // Common actions
    static constexpr const char* kAdd = "+";
    static constexpr const char* kTrash = "🗑";
    static constexpr const char* kClose = "✕";
    static constexpr const char* kSearch = "🔍";
    static constexpr const char* kSettings = "⚙";
    static constexpr const char* kSave = "💾";
    static constexpr const char* kReset = "⟲";
    static constexpr const char* kMenu = "☰";
    static constexpr const char* kEyeOpen = "👁";
    static constexpr const char* kEyeClosed = "⊘";

    // Status indicators
    static constexpr const char* kDot = "●";
    static constexpr const char* kCheck = "✓";
    static constexpr const char* kInfo = "ℹ";
    static constexpr const char* kWarning = "⚠";
    static constexpr const char* kError = "✖";
};

/// Draws a stylized format/status badge with rounded corners and contrasting text.
void drawBadge(const char* label, const Color4f& badgeColor,
               const Color4f& textColor = Color4f{1.0F, 1.0F, 1.0F, 1.0F});

/// Draws an indicator dot followed by descriptive status text.
void drawStatusPill(const char* label, const Color4f& color, bool outlineOnly = false);

/// Draws a clean section heading with subtle underlying divider.
bool drawSectionHeader(const char* label, bool defaultOpen = true);

/// Draws a standard property label aligned to kPropertyLabelWidth with optional tooltip.
void drawPropertyLabel(const char* label, const char* tooltip = nullptr,
                       float width = ThemeMetrics::kPropertyLabelWidth);

/// Draws muted/placeholder text using theme textMuted color.
void drawMutedText(const char* fmt, ...);

/// Draws secondary/metadata text using theme textSecondary color.
void drawSecondaryText(const char* fmt, ...);

/// Begins a nested card container using theme surfaceCard color.
void beginCard(const char* id, float width = 0.0F, float height = 0.0F);

/// Ends a nested card container.
void endCard();

/// Draws a transport simulation control button with distinct color coding.
bool drawTransportButton(const char* icon, const char* label, const Color4f& accentColor,
                         bool disabled = false, bool activeHighlight = false);

} // namespace eng::editor
