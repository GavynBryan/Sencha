#pragma once

#include <RmlUi/Core/RenderInterface.h>

#include <graphics/RenderExtent.h>
#include <render/TextureHandle.h>
#include <render/ui/UiDrawFrame.h>

#include <cstdint>
#include <string_view>
#include <unordered_map>

class Logger;

//=============================================================================
// IUiTextureResolver
//
// How a content image named in a document becomes a texture. The recorder does
// not search for assets: it asks whoever opened the screen, which is the only
// thing that knows which leases that screen holds.
//=============================================================================
class IUiTextureResolver
{
public:
    virtual ~IUiTextureResolver() = default;

    // False when the source is not in the open screen's resource table, which
    // is an authoring error and is reported as one rather than fetched.
    [[nodiscard]] virtual bool ResolveTexture(std::string_view source,
                                              TextureHandle& outHandle,
                                              RenderExtent& outSize) = 0;
};

//=============================================================================
// RmlRenderRecorder
//
// The document engine's render interface, recording into UiDrawFrame instead of
// issuing commands.
//
// Device-free, and that is the whole design rather than a convenience. Geometry
// becomes immutable CPU blobs; a generated texture becomes pixels plus an id;
// a content image becomes an asset handle the screen already leases. Nothing
// here creates a GPU resource, so the runtime stays outside the renderer's
// ownership and there is no second path for a headless process to take -- it is
// the same recorder, with nobody consuming its frames.
//
// State (scissor, transform, clip mask) is sampled per command rather than
// emitted as separate commands. A consumer therefore never has to replay a
// state machine to know what a command meant.
//=============================================================================
class RmlRenderRecorder final : public Rml::RenderInterface
{
public:
    explicit RmlRenderRecorder(Logger& log);

    // Who answers a content image request. Null means content images cannot
    // resolve, which is the correct answer in a process with no texture cache.
    void SetTextureResolver(IUiTextureResolver* resolver) { Resolver = resolver; }

    // Opens recording for one surface. Commands issued until EndFrame land in
    // it, and the pixels of any texture generated meanwhile ride along.
    void BeginFrame(RenderExtent surface);
    [[nodiscard]] UiDrawFrame EndFrame();

    // Live CPU resources, for a test that wants to prove a closed screen left
    // nothing behind. Frames hold their own references, so a nonzero count
    // after everything closes is a leak in the runtime rather than in a
    // document.
    [[nodiscard]] std::uint32_t LiveGeometryCount() const
    {
        return static_cast<std::uint32_t>(Geometry.size());
    }
    [[nodiscard]] std::uint32_t LiveTextureCount() const
    {
        return static_cast<std::uint32_t>(Textures.size());
    }

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

    void SetTransform(const Rml::Matrix4f* transform) override;
    void EnableClipMask(bool enable) override;
    void RenderToClipMask(Rml::ClipMaskOperation operation,
                          Rml::CompiledGeometryHandle geometry,
                          Rml::Vector2f translation) override;

    // -- Tier 2: not implemented, and loud about it ---------------------------
    //
    // The base class defaults are silent no-ops, which is the worst outcome: a
    // document using one of these would draw subtly wrong with nothing said.
    // Each of these reports once per session and draws nothing, which is how an
    // unsupported construct that slipped past the cooker's lint still surfaces.
    Rml::LayerHandle PushLayer() override;
    void CompositeLayers(Rml::LayerHandle source,
                         Rml::LayerHandle destination,
                         Rml::BlendMode blend_mode,
                         Rml::Span<const Rml::CompiledFilterHandle> filters) override;
    void PopLayer() override;
    Rml::TextureHandle SaveLayerAsTexture() override;
    Rml::CompiledFilterHandle SaveLayerAsMaskImage() override;
    Rml::CompiledFilterHandle CompileFilter(const Rml::String& name,
                                            const Rml::Dictionary& parameters) override;
    void ReleaseFilter(Rml::CompiledFilterHandle filter) override;
    Rml::CompiledShaderHandle CompileShader(const Rml::String& name,
                                            const Rml::Dictionary& parameters) override;
    void RenderShader(Rml::CompiledShaderHandle shader,
                      Rml::CompiledGeometryHandle geometry,
                      Rml::Vector2f translation,
                      Rml::TextureHandle texture) override;
    void ReleaseShader(Rml::CompiledShaderHandle shader) override;

private:
    struct TextureRecord
    {
        UiTextureRef Ref;
    };

    void ReportUnsupported(std::string_view feature);
    [[nodiscard]] UiTextureRef TextureRefFor(Rml::TextureHandle texture) const;

    Logger& Log;
    IUiTextureResolver* Resolver = nullptr;

    std::unordered_map<std::uint64_t, UiGeometryRef> Geometry;
    std::unordered_map<std::uint64_t, TextureRecord> Textures;

    // Never reused, so a handle that outlives its release reads as invalid
    // rather than as whichever resource took the slot.
    std::uint64_t NextGeometryHandle = 1;
    std::uint64_t NextTextureHandle = 1;
    UiGeneratedTextureId NextGeneratedId = 1;

    UiDrawFrame Frame;
    UiClipRect Clip;
    std::optional<Mat4> Transform;
    bool ClipMaskEnabled = false;

    // One report per construct per session: a document redrawing every frame
    // would otherwise bury the log in the same line.
    std::vector<std::string> ReportedUnsupported;
};
