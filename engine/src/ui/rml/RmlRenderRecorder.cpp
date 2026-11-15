#include "RmlRenderRecorder.h"

#include <core/logging/Logger.h>

#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/Dictionary.h>

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

// A gradient's colour ramp: kUiGradientRampTexels texels from the first stop to
// the last, each the colour CSS gives that point, in premultiplied sRGB. The
// stops arrive resolved: positions are numbers along the gradient, never
// decreasing.
std::vector<std::byte> BuildGradientRamp(const Rml::ColorStopList& stops)
{
    std::vector<std::byte> ramp(static_cast<std::size_t>(kUiGradientRampTexels) * 4);
    const float first = stops.front().position.number;
    const float last = stops.back().position.number;
    std::size_t segment = 1;
    for (std::uint32_t texel = 0; texel < kUiGradientRampTexels; ++texel)
    {
        const float at = first + (last - first) * static_cast<float>(texel)
            / static_cast<float>(kUiGradientRampTexels - 1);
        while (segment + 1 < stops.size() && stops[segment].position.number < at)
            ++segment;

        Rml::ColourbPremultiplied colour = stops.back().color;
        if (stops.size() > 1)
        {
            const Rml::ColorStop& from = stops[segment - 1];
            const Rml::ColorStop& to = stops[segment];
            const float span = to.position.number - from.position.number;
            const float share = span > 0.0f ? std::clamp((at - from.position.number) / span, 0.0f, 1.0f)
                                            : (at < to.position.number ? 0.0f : 1.0f);
            const auto mix = [share](Rml::byte a, Rml::byte b) {
                return static_cast<Rml::byte>(std::lround(static_cast<float>(a)
                    + (static_cast<float>(b) - static_cast<float>(a)) * share));
            };
            colour = Rml::ColourbPremultiplied(mix(from.color.red, to.color.red),
                                               mix(from.color.green, to.color.green),
                                               mix(from.color.blue, to.color.blue),
                                               mix(from.color.alpha, to.color.alpha));
        }
        std::byte* out = ramp.data() + static_cast<std::size_t>(texel) * 4;
        out[0] = static_cast<std::byte>(colour.red);
        out[1] = static_cast<std::byte>(colour.green);
        out[2] = static_cast<std::byte>(colour.blue);
        out[3] = static_cast<std::byte>(colour.alpha);
    }
    return ramp;
}

Vec2d ToVec2d(Rml::Vector2f value)
{
    return Vec2d{ value.x, value.y };
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

UiTextureChanges RmlRenderRecorder::TakeTextureChanges()
{
    for (const UiGeneratedTextureId ramp : UnusedRamps)
    {
        const auto pixels = RampPixels.find(ramp);
        if (pixels == RampPixels.end())
            continue;
        const auto record = Ramps.find(pixels->second);
        if (record == Ramps.end() || record->second.Users > 0)
            continue;
        Changes.Released.push_back(ramp);
        Ramps.erase(record);
        RampPixels.erase(pixels);
    }
    UnusedRamps.clear();
    return std::exchange(Changes, UiTextureChanges{});
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

    if (std::string_view(source).starts_with(kUiHostImageScheme))
    {
        UiHostImageId host = kInvalidUiHostImage;
        RenderExtent hostSize{};
        const std::string_view name = std::string_view(source).substr(kUiHostImageScheme.size());
        if (!Resolver->ResolveHostImage(name, host, hostSize))
        {
            Log.Error("ui: '{}' names a host image the host never registered", source);
            return 0;
        }
        texture_dimensions = Rml::Vector2i(static_cast<int>(hostSize.Width),
                                           static_cast<int>(hostSize.Height));
        const std::uint64_t hostKey = NextTextureHandle++;
        UiTextureRef hostRef;
        hostRef.Kind = UiTextureKind::Host;
        hostRef.Host = host;
        Textures.emplace(hostKey, TextureRecord{ hostRef });
        return static_cast<Rml::TextureHandle>(hostKey);
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
    ConvertPremultipliedSrgbToLinear(source, upload.PremultipliedRgba8);

    const std::uint64_t key = NextTextureHandle++;
    UiTextureRef ref;
    ref.Kind = UiTextureKind::Generated;
    ref.Generated = upload.Id;
    Textures.emplace(key, TextureRecord{ ref });

    Changes.Uploads.push_back(std::move(upload));
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
        Changes.Released.push_back(it->second.Ref.Generated);

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

// -- Gradients ---------------------------------------------------------------

Rml::CompiledShaderHandle RmlRenderRecorder::CompileShader(const Rml::String& name,
                                                           const Rml::Dictionary& parameters)
{
    UiGradient gradient;
    if (name == "linear-gradient")
    {
        gradient.Shape = UiGradientShape::Linear;
        const Rml::Vector2f start = Rml::Get(parameters, "p0", Rml::Vector2f(0.0f));
        gradient.Origin = ToVec2d(start);
        gradient.Axis = ToVec2d(Rml::Get(parameters, "p1", Rml::Vector2f(0.0f)) - start);
    }
    else if (name == "radial-gradient")
    {
        gradient.Shape = UiGradientShape::Radial;
        gradient.Origin = ToVec2d(Rml::Get(parameters, "center", Rml::Vector2f(0.0f)));
        const Rml::Vector2f radius = Rml::Get(parameters, "radius", Rml::Vector2f(1.0f));
        // A zero radius puts every point past the last stop.
        constexpr float kFar = 1.0e6f;
        gradient.Axis = Vec2d{ radius.x > 0.0f ? 1.0f / radius.x : kFar,
                               radius.y > 0.0f ? 1.0f / radius.y : kFar };
    }
    else if (name == "conic-gradient")
    {
        gradient.Shape = UiGradientShape::Conic;
        gradient.Origin = ToVec2d(Rml::Get(parameters, "center", Rml::Vector2f(0.0f)));
        const float angle = Rml::Get(parameters, "angle", 0.0f);
        gradient.Axis = Vec2d{ std::cos(angle), std::sin(angle) };
    }
    else
    {
        ReportUnsupported("shader '" + name + "'");
        return {};
    }
    gradient.Repeating = Rml::Get(parameters, "repeating", false);

    const auto found = parameters.find("color_stop_list");
    if (found == parameters.end() || found->second.GetType() != Rml::Variant::COLORSTOPLIST)
        return {};
    const Rml::ColorStopList& stops = found->second.GetReference<Rml::ColorStopList>();
    if (stops.empty())
        return {};
    gradient.FirstStop = stops.front().position.number;
    gradient.LastStop = stops.back().position.number;

    gradient.Ramp = AcquireRamp(BuildGradientRamp(stops));

    const std::uint64_t handle = NextShaderHandle++;
    Shaders.emplace(handle, gradient);
    return static_cast<Rml::CompiledShaderHandle>(handle);
}

void RmlRenderRecorder::RenderShader(Rml::CompiledShaderHandle shader,
                                     Rml::CompiledGeometryHandle geometry,
                                     Rml::Vector2f translation,
                                     Rml::TextureHandle)
{
    const auto compiled = Shaders.find(static_cast<std::uint64_t>(shader));
    const auto it = Geometry.find(static_cast<std::uint64_t>(geometry));
    if (compiled == Shaders.end() || it == Geometry.end())
        return;

    UiDrawCommand command;
    command.Geometry = it->second;
    command.Texture.Kind = UiTextureKind::Generated;
    command.Texture.Generated = compiled->second.Ramp;
    command.Gradient = compiled->second;
    command.Translation = Vec2d{ translation.x, translation.y };
    command.Clip = Clip;
    command.Transform = Transform;
    command.ClipMaskEnabled = ClipMaskEnabled;
    Frame.Commands.push_back(std::move(command));
}

void RmlRenderRecorder::ReleaseShader(Rml::CompiledShaderHandle shader)
{
    const auto it = Shaders.find(static_cast<std::uint64_t>(shader));
    if (it == Shaders.end())
        return;
    ReleaseRamp(it->second.Ramp);
    Shaders.erase(it);
}

UiGeneratedTextureId RmlRenderRecorder::AcquireRamp(std::vector<std::byte> pixels)
{
    std::string key(reinterpret_cast<const char*>(pixels.data()), pixels.size());
    RampRecord& record = Ramps[key];
    if (record.Id == kInvalidUiGeneratedTexture)
    {
        UiTextureUpload upload;
        upload.Id = NextGeneratedId++;
        upload.Size = RenderExtent{ kUiGradientRampTexels, 1 };
        upload.PremultipliedRgba8 = std::move(pixels);
        record.Id = upload.Id;
        RampPixels.emplace(upload.Id, std::move(key));
        Changes.Uploads.push_back(std::move(upload));
    }
    ++record.Users;
    return record.Id;
}

void RmlRenderRecorder::ReleaseRamp(UiGeneratedTextureId ramp)
{
    const auto pixels = RampPixels.find(ramp);
    if (pixels == RampPixels.end())
        return;
    RampRecord& record = Ramps[pixels->second];
    if (record.Users > 0 && --record.Users == 0)
        UnusedRamps.push_back(ramp);
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

