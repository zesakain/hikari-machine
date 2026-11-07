/// @file ui_system.h
/// @brief Runtime User Interface subsystem integrating RmlUi with Hikari RHI and Render Graph.
///
/// Purpose:
/// Manages runtime HUD, menu, and UI documents using RmlUi. Connects deterministic platform
/// input snapshots to UI contexts, decouples UI animation updates using unscaled UiClock delta,
/// and renders UI passes via the Render Graph and RHI without exposing RmlUi headers to consumers.
///
/// Ownership and Lifetime:
/// Owned by the simulation runtime, Framework subsystem, or application. Outlives individual
/// UI documents and modes.
///
/// Thread-safety:
/// Must be updated and rendered on the main application / rendering thread.
///
/// Performance:
/// Minimal frame overhead; dynamic UI meshes are submitted via RenderGraph passes.
/// Compatible with NullDevice for headless continuous simulation tests.
///
/// Header self-containment:
/// Does NOT include <RmlUi/...> headers. All third-party structures are encapsulated in Impl.

#pragma once

#include "core/result.h"
#include "core/time/duration.h"
#include "ui/ui_types.h"

#include <memory>
#include <string_view>

namespace eng::platform {
struct InputSnapshot;
} // namespace eng::platform

namespace eng::rhi {
class RhiDevice;
class RhiCommandList;
} // namespace eng::rhi

namespace eng::render {
class RenderGraph;
} // namespace eng::render

namespace eng::ui {

/// @class UiSystem
/// @brief Central runtime UI manager encapsulating RmlUi contexts, system/render backends,
/// and document lifecycles.
class UiSystem {
public:
    UiSystem();
    ~UiSystem();

    UiSystem(const UiSystem&) = delete;
    UiSystem& operator=(const UiSystem&) = delete;

    UiSystem(UiSystem&& other) noexcept;
    UiSystem& operator=(UiSystem&& other) noexcept;

    /// Initializes the RmlUi runtime, system backend, custom RHI render interface, and default
    /// context.
    ///
    /// Preconditions : RHI device must be initialized.
    /// Postconditions: UI context created matching config dimensions.
    /// Thread-safety : Main thread only.
    [[nodiscard]] Result<void, UiError> initialize(const UiConfig& config,
                                                   rhi::RhiDevice& rhiDevice);

    /// Tears down loaded documents, contexts, and releases RmlUi core resources.
    void shutdown();

    /// Checks whether the UI system is currently initialized and active.
    [[nodiscard]] bool isInitialized() const noexcept;

    /// Parses and instantiates an RML document from an in-memory string.
    ///
    /// @param rmlContent Raw XML/RML markup string.
    /// @param documentName Diagnostic identifier for the document.
    /// @return Generational handle to the document, or UiError on parse failure.
    [[nodiscard]] Result<UiDocumentHandle, UiError>
    loadDocumentFromMemory(std::string_view rmlContent, std::string_view documentName = "document");

    /// Closes and releases a loaded UI document.
    void closeDocument(UiDocumentHandle handle);

    /// Shows and focuses a loaded UI document.
    void showDocument(UiDocumentHandle handle);

    /// Hides a loaded UI document from layout and rendering.
    void hideDocument(UiDocumentHandle handle);

    /// Advances UI animations, transitions, and timers using unscaled UI clock delta time.
    ///
    /// Note: Always use unscaled duration from UiClock so HUD animations and menus remain
    /// fluid during gameplay slow-motion.
    void update(Duration unscaledUiDelta);

    /// Records UI rendering commands directly to an active RHI command list.
    void render(rhi::RhiCommandList& cmdList);

    /// Registers a UI render pass into the engine's Render Graph.
    void render(render::RenderGraph& renderGraph);

    /// Translates platform input snapshot states into RmlUi mouse, key, and scroll events.
    void processInput(const platform::InputSnapshot& currentInput,
                      const platform::InputSnapshot& previousInput);

    /// Updates viewport dimensions on window resize.
    void resize(uint32_t width, uint32_t height);

    /// Returns diagnostic performance statistics for the current frame.
    [[nodiscard]] const UiRenderStats& renderStats() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace eng::ui
