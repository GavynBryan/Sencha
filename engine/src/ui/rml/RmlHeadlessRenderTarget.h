#pragma once

#include <RmlUi/Core/RenderInterface.h>

#include <cstdint>

//=============================================================================
// RmlHeadlessRenderTarget
//
// Satisfies the document engine's render interface without a device. Geometry
// is compiled and released, textures are handed out and taken back, and nothing
// is drawn.
//
// Not a stub for the real one to replace: parsing, the cascade, layout and
// measurement are all device-free, and this is what keeps them reachable
// without one. It is the target under a headless test, a dedicated server that
// still loads a package to validate it, and any build where UI rendering is
// compiled out. The recording target that produces real draw data sits beside
// it rather than over it.
//
// Handles are minted monotonically and never reused, so a double release or a
// use-after-release reads as invalid rather than as whichever resource took the
// slot. Counts are kept because "did anything leak" is the only question this
// target can usefully answer, and it is one a test wants to ask.
//=============================================================================
class RmlHeadlessRenderTarget final : public Rml::RenderInterface
{
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry,
                        Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

    Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions,
                                   const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                       Rml::Vector2i source_dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;

    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

    // Outstanding resources. Both must be zero once every document is closed
    // and the engine is shut down; a nonzero count is a leak in the runtime's
    // own lifetime handling, not in the document.
    [[nodiscard]] std::uint32_t LiveGeometryCount() const { return LiveGeometry; }
    [[nodiscard]] std::uint32_t LiveTextureCount() const { return LiveTextures; }

private:
    std::uint64_t NextHandle = 1;
    std::uint32_t LiveGeometry = 0;
    std::uint32_t LiveTextures = 0;
};
