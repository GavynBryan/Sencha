#include "RmlHeadlessRenderTarget.h"

Rml::CompiledGeometryHandle RmlHeadlessRenderTarget::CompileGeometry(
    Rml::Span<const Rml::Vertex>, Rml::Span<const int>)
{
    ++LiveGeometry;
    return static_cast<Rml::CompiledGeometryHandle>(NextHandle++);
}

void RmlHeadlessRenderTarget::RenderGeometry(Rml::CompiledGeometryHandle,
                                             Rml::Vector2f,
                                             Rml::TextureHandle)
{
}

void RmlHeadlessRenderTarget::ReleaseGeometry(Rml::CompiledGeometryHandle)
{
    if (LiveGeometry > 0)
        --LiveGeometry;
}

Rml::TextureHandle RmlHeadlessRenderTarget::LoadTexture(Rml::Vector2i& texture_dimensions,
                                                        const Rml::String&)
{
    // A plausible non-zero size rather than nothing: a zero-sized texture makes
    // the layout engine collapse the element that used it, which would make
    // measured geometry under this target disagree with the real one.
    texture_dimensions = Rml::Vector2i(1, 1);
    ++LiveTextures;
    return static_cast<Rml::TextureHandle>(NextHandle++);
}

Rml::TextureHandle RmlHeadlessRenderTarget::GenerateTexture(Rml::Span<const Rml::byte>,
                                                            Rml::Vector2i)
{
    ++LiveTextures;
    return static_cast<Rml::TextureHandle>(NextHandle++);
}

void RmlHeadlessRenderTarget::ReleaseTexture(Rml::TextureHandle)
{
    if (LiveTextures > 0)
        --LiveTextures;
}

void RmlHeadlessRenderTarget::EnableScissorRegion(bool)
{
}

void RmlHeadlessRenderTarget::SetScissorRegion(Rml::Rectanglei)
{
}
