/// @file rmlui_render_backend.cpp
/// @brief Implementation of custom RmlUi RenderInterface for Hikari RHI.

#include "rmlui_render_backend.h"

#include "core/assert.h"
#include "rhi/command_list.h"
#include "rhi/rhi_device.h"

namespace eng::ui {

HikariRenderInterface::HikariRenderInterface(rhi::RhiDevice* device) : device_(device) {}

HikariRenderInterface::~HikariRenderInterface() = default;

Rml::CompiledGeometryHandle
HikariRenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                       Rml::Span<const int> indices) {
    auto* buf = new GeometryBuffer();
    buf->vertices.assign(vertices.begin(), vertices.end());
    buf->indices.assign(indices.begin(), indices.end());
    return reinterpret_cast<Rml::CompiledGeometryHandle>(buf);
}

void HikariRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometry,
                                           Rml::Vector2f /*translation*/,
                                           Rml::TextureHandle /*texture*/
) {
    auto* buf = reinterpret_cast<GeometryBuffer*>(geometry);
    if (buf == nullptr) {
        return;
    }

    stats_.drawCalls++;
    stats_.triangleCount += static_cast<uint32_t>(buf->indices.size() / 3);

    if (currentCmdList_ != nullptr) {
        // If an active command list is bound, dispatch RHI commands
        if (scissorEnabled_) {
            currentCmdList_->setScissor(
                scissorRegion_.Left(), scissorRegion_.Top(),
                static_cast<uint32_t>(std::max(0, scissorRegion_.Width())),
                static_cast<uint32_t>(std::max(0, scissorRegion_.Height())));
        }
    }
}

void HikariRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
    delete reinterpret_cast<GeometryBuffer*>(geometry);
}

Rml::TextureHandle HikariRenderInterface::LoadTexture(Rml::Vector2i& texture_dimensions,
                                                      const Rml::String& /*source*/
) {
    texture_dimensions.x = 1;
    texture_dimensions.y = 1;
    return static_cast<Rml::TextureHandle>(1);
}

Rml::TextureHandle HikariRenderInterface::GenerateTexture(Rml::Span<const Rml::byte> /*source*/,
                                                          Rml::Vector2i /*source_dimensions*/
) {
    return static_cast<Rml::TextureHandle>(1);
}

void HikariRenderInterface::ReleaseTexture(Rml::TextureHandle /*texture*/) {
    // No-op for generated textures
}

void HikariRenderInterface::EnableScissorRegion(bool enable) {
    scissorEnabled_ = enable;
}

void HikariRenderInterface::SetScissorRegion(Rml::Rectanglei region) {
    scissorRegion_ = region;
}

} // namespace eng::ui
