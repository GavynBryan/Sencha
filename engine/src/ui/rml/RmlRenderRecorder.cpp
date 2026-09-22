#include "RmlRenderRecorder.h"

#include <core/logging/Logger.h>

#include <algorithm>
#include <span>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

namespace
{
// sRGB -> linear for an 8-bit channel, built once. The document engine hands
// over colour in sRGB, and every consumer downstream wants linear; 256 entries
// beats a pow() per channel per texel.
const std::array<float, 256>& SrgbToLinearTable()
{
    static const std::array<float, 256> table = [] {
        std::array<float, 256> values{};
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            const float srgb = static_cast<float>(i) / 255.0f;
            values[i] = srgb <= 0.04045f
                ? srgb / 12.92f
                : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
        }
        return values;
    }();
    return table;
}

std::uint8_t ToU8(float linear)
{
    return static_cast<std::uint8_t>(std::clamp(linear * 255.0f + 0.5f, 0.0f, 255.0f));
}

// Premultiplied sRGB -> premultiplied linear, per texel.
//
// The conversion has to happen on the straight colour, so the premultiply is
// undone and redone around it. For a coverage atlas -- premultiplied white,
// which is what every glyph is -- the round trip is exact: unpremultiplying
// gives 1.0, converting leaves 1.0, and remultiplying gives the alpha back.
void ConvertPremultipliedSrgbToLinear(std::span<const Rml::byte> source,
                                      std::vector<std::byte>& out)
{
    const std::array<float, 256>& toLinear = SrgbToLinearTable();
    out.resize(source.size());

    for (std::size_t i = 0; i + 3 < source.size(); i += 4)
    {
        const std::uint8_t alpha = static_cast<std::uint8_t>(source[i + 3]);
        if (alpha == 0)
        {
            // Nothing to recover, and dividing by zero would invent colour in a
            // texel that contributes none.
            out[i + 0] = std::byte{ 0 };
            out[i + 1] = std::byte{ 0 };
            out[i + 2] = std::byte{ 0 };
            out[i + 3] = std::byte{ 0 };
            continue;
        }

        const float alphaScale = static_cast<float>(alpha) / 255.0f;
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            const auto premultiplied = static_cast<std::uint8_t>(source[i + channel]);
            const float straightSrgb =
                std::clamp(static_cast<float>(premultiplied) / 255.0f / alphaScale, 0.0f, 1.0f);
            const float straightLinear =
                toLinear[static_cast<std::size_t>(ToU8(straightSrgb))];
            out[i + channel] = static_cast<std::byte>(ToU8(straightLinear * alphaScale));
        }
        out[i + 3] = static_cast<std::byte>(alpha);
    }
}

// Both sides use column-vector maths (M * v), but their storage orders are a
// build-time choice on the document engine's side. GetRow/GetColumn is used
// rather than data(), so this is right whichever way RmlUi was configured.
Mat4 ToMat4(const Rml::Matrix4f& source)
{
    Mat4 out{};
    for (int row = 0; row < 4; ++row)
    {
        const auto sourceRow = source.GetRow(row);
        for (int col = 0; col < 4; ++col)
            out.Data[row][col] = sourceRow[col];
    }
    return out;
}
} // namespace

RmlRenderRecorder::RmlRenderRecorder(Logger& log)
    : Log(log)
{
}

void RmlRenderRecorder::BeginFrame(RenderExtent surface)
{
    Frame = {};
    Frame.Surface = surface;
    // Not reset: scissor, transform and mask are the document engine's state and
    // it sets them as it goes. Clearing them here would drop a state it set
    // before the frame it applies to.
}

UiDrawFrame RmlRenderRecorder::EndFrame()
{
    return std::exchange(Frame, UiDrawFrame{});
}

Rml::CompiledGeometryHandle RmlRenderRecorder::CompileGeometry(
    Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
    auto blob = std::make_shared<UiGeometryBlob>();
    blob->Vertices.reserve(vertices.size());
    for (const Rml::Vertex& vertex : vertices)
    {
        UiVertex out;
        out.Position = Vec2d{ vertex.position.x, vertex.position.y };
        out.Uv = Vec2d{ vertex.tex_coord.x, vertex.tex_coord.y };
        out.ColorSrgbPremultiplied[0] = vertex.colour.red;
        out.ColorSrgbPremultiplied[1] = vertex.colour.green;
        out.ColorSrgbPremultiplied[2] = vertex.colour.blue;
        out.ColorSrgbPremultiplied[3] = vertex.colour.alpha;
        blob->Vertices.push_back(out);
    }

    blob->Indices.reserve(indices.size());
    for (const int index : indices)
        blob->Indices.push_back(static_cast<std::uint32_t>(index));

    const std::uint64_t handle = NextGeometryHandle++;
    Geometry.emplace(handle, std::move(blob));
    return static_cast<Rml::CompiledGeometryHandle>(handle);
}

UiTextureRef RmlRenderRecorder::TextureRefFor(Rml::TextureHandle texture) const
{
    if (texture == 0)
        return {};
    const auto it = Textures.find(static_cast<std::uint64_t>(texture));
    return it != Textures.end() ? it->second.Ref : UiTextureRef{};
}

void RmlRenderRecorder::RenderGeometry(Rml::CompiledGeometryHandle geometry,
                                       Rml::Vector2f translation,
                                       Rml::TextureHandle texture)
{
    const auto it = Geometry.find(static_cast<std::uint64_t>(geometry));
    if (it == Geometry.end())
        return;

    UiDrawCommand command;
    command.Geometry = it->second;
    command.Texture = TextureRefFor(texture);
    command.Translation = Vec2d{ translation.x, translation.y };
    command.Clip = Clip;
    command.Transform = Transform;
    command.ClipMaskEnabled = ClipMaskEnabled;
    Frame.Commands.push_back(std::move(command));
}

void RmlRenderRecorder::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
    // Drops this recorder's reference only. A frame already submitted holds its
    // own, so geometry released mid-flight stays alive exactly as long as
    // something still names it.
    Geometry.erase(static_cast<std::uint64_t>(geometry));
}

Rml::TextureHandle RmlRenderRecorder::LoadTexture(Rml::Vector2i& texture_dimensions,
                                                  const Rml::String& source)
{
    if (Resolver == nullptr)
    {
        Log.Error("ui: '{}' cannot be resolved; this process holds no textures", source);
        return 0;
    }

    TextureHandle handle{};
    RenderExtent size{};
    if (!Resolver->ResolveTexture(source, handle, size))
    {
        Log.Error("ui: '{}' is not in the open screen's resource table; "
                  "images a document uses have to be declared at cook time", source);
        return 0;
    }

    texture_dimensions = Rml::Vector2i(static_cast<int>(size.Width), static_cast<int>(size.Height));

    const std::uint64_t key = NextTextureHandle++;
    UiTextureRef ref;
    ref.Kind = UiTextureKind::Content;
    ref.Content = handle;
    Textures.emplace(key, TextureRecord{ ref });
    return static_cast<Rml::TextureHandle>(key);
}

Rml::TextureHandle RmlRenderRecorder::GenerateTexture(Rml::Span<const Rml::byte> source,
                                                      Rml::Vector2i source_dimensions)
{
    if (source_dimensions.x <= 0 || source_dimensions.y <= 0)
        return 0;

    UiTextureUpload upload;
    upload.Id = NextGeneratedId++;
    upload.Size = RenderExtent{ static_cast<std::uint32_t>(source_dimensions.x),
                                static_cast<std::uint32_t>(source_dimensions.y) };
    ConvertPremultipliedSrgbToLinear(source, upload.LinearPremultipliedRgba8);

    const std::uint64_t key = NextTextureHandle++;
    UiTextureRef ref;
    ref.Kind = UiTextureKind::Generated;
    ref.Generated = upload.Id;
    Textures.emplace(key, TextureRecord{ ref });

    Frame.PendingUploads.push_back(std::move(upload));
    return static_cast<Rml::TextureHandle>(key);
}

void RmlRenderRecorder::ReleaseTexture(Rml::TextureHandle texture)
{
    const auto it = Textures.find(static_cast<std::uint64_t>(texture));
    if (it == Textures.end())
        return;

    // Only a generated texture has a GPU resource this layer is responsible for.
    // A content texture's lifetime is the screen's lease, and releasing it here
    // would be releasing something this layer never acquired.
    if (it->second.Ref.Kind == UiTextureKind::Generated)
        Frame.ReleasedTextures.push_back(it->second.Ref.Generated);

    Textures.erase(it);
}

void RmlRenderRecorder::EnableScissorRegion(bool enable)
{
    Clip.Enabled = enable;
}

void RmlRenderRecorder::SetScissorRegion(Rml::Rectanglei region)
{
    Clip.Enabled = true;
    Clip.X = region.Left();
    Clip.Y = region.Top();
    Clip.Width = region.Width();
    Clip.Height = region.Height();
}

void RmlRenderRecorder::SetTransform(const Rml::Matrix4f* transform)
{
    Transform = transform != nullptr ? std::optional<Mat4>(ToMat4(*transform)) : std::nullopt;
}

void RmlRenderRecorder::EnableClipMask(bool enable)
{
    ClipMaskEnabled = enable;
}

void RmlRenderRecorder::RenderToClipMask(Rml::ClipMaskOperation operation,
                                         Rml::CompiledGeometryHandle geometry,
                                         Rml::Vector2f translation)
{
    const auto it = Geometry.find(static_cast<std::uint64_t>(geometry));
    if (it == Geometry.end())
        return;

    UiDrawCommand command;
    command.Geometry = it->second;
    command.Translation = Vec2d{ translation.x, translation.y };
    command.Clip = Clip;
    command.Transform = Transform;
    command.ClipMaskEnabled = ClipMaskEnabled;
    switch (operation)
    {
    case Rml::ClipMaskOperation::Set:        command.ClipMask = UiClipMaskOp::Set; break;
    case Rml::ClipMaskOperation::SetInverse: command.ClipMask = UiClipMaskOp::SetInverse; break;
    case Rml::ClipMaskOperation::Intersect:  command.ClipMask = UiClipMaskOp::Intersect; break;
    }
    Frame.Commands.push_back(std::move(command));
}

// -- Tier 2 ------------------------------------------------------------------

void RmlRenderRecorder::ReportUnsupported(std::string_view feature)
{
    const std::string name(feature);
    if (std::find(ReportedUnsupported.begin(), ReportedUnsupported.end(), name)
        != ReportedUnsupported.end())
    {
        return;
    }
    ReportedUnsupported.push_back(name);
    Log.Warn("ui: '{}' is outside the supported rendering profile and drew nothing "
             "(docs/ui/architecture.md, capability profile)", feature);
}

Rml::LayerHandle RmlRenderRecorder::PushLayer()
{
    ReportUnsupported("render layers");
    return {};
}

void RmlRenderRecorder::CompositeLayers(Rml::LayerHandle,
                                        Rml::LayerHandle,
                                        Rml::BlendMode,
                                        Rml::Span<const Rml::CompiledFilterHandle>)
{
    ReportUnsupported("layer compositing");
}

void RmlRenderRecorder::PopLayer()
{
}

Rml::TextureHandle RmlRenderRecorder::SaveLayerAsTexture()
{
    ReportUnsupported("render textures");
    return 0;
}

Rml::CompiledFilterHandle RmlRenderRecorder::SaveLayerAsMaskImage()
{
    ReportUnsupported("mask images");
    return {};
}

Rml::CompiledFilterHandle RmlRenderRecorder::CompileFilter(const Rml::String& name,
                                                           const Rml::Dictionary&)
{
    ReportUnsupported("filter '" + name + "'");
    return {};
}

void RmlRenderRecorder::ReleaseFilter(Rml::CompiledFilterHandle)
{
}

Rml::CompiledShaderHandle RmlRenderRecorder::CompileShader(const Rml::String& name,
                                                           const Rml::Dictionary&)
{
    ReportUnsupported("shader '" + name + "' (gradient decorators need this)");
    return {};
}

void RmlRenderRecorder::RenderShader(Rml::CompiledShaderHandle,
                                     Rml::CompiledGeometryHandle,
                                     Rml::Vector2f,
                                     Rml::TextureHandle)
{
}

void RmlRenderRecorder::ReleaseShader(Rml::CompiledShaderHandle)
{
}
