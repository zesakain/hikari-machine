/// @file rmlui_render_backend.h
/// @brief RmlUi RenderInterface implementation for Hikari RHI and Render Graph.

#pragma once

#include "ui/ui_types.h"

#include <RmlUi/Core/RenderInterface.h>
#include <cstdint>
#include <vector>

namespace eng::rhi {
class RhiDevice;
class RhiCommandList;
} // namespace eng::rhi

namespace eng::ui {

class HikariRenderInterface final : public Rml::RenderInterface {
public:
    explicit HikariRenderInterface(rhi::RhiDevice* device = nullptr);
    ~HikariRenderInterface() override;

    void setDevice(rhi::RhiDevice* device) noexcept { device_ = device; }
    void setCommandList(rhi::RhiCommandList* cmdList) noexcept { currentCmdList_ = cmdList; }
    void resetStats() noexcept {
        stats_.drawCalls = 0;
        stats_.triangleCount = 0;
    }
    [[nodiscard]] const UiRenderStats& stats() const noexcept { return stats_; }
    void setActiveDocuments(uint32_t count) noexcept { stats_.activeDocuments = count; }

    // RmlUi RenderInterface Overrides
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override;

    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;

    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

    Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions,
                                   const Rml::String& source) override;

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                       Rml::Vector2i source_dimensions) override;

    void ReleaseTexture(Rml::TextureHandle texture) override;

    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

private:
    struct GeometryBuffer {
        std::vector<Rml::Vertex> vertices;
        std::vector<int> indices;
    };

    rhi::RhiDevice* device_{nullptr};
    rhi::RhiCommandList* currentCmdList_{nullptr};
    UiRenderStats stats_{};
    bool scissorEnabled_{false};
    Rml::Rectanglei scissorRegion_{};
};

} // namespace eng::ui
