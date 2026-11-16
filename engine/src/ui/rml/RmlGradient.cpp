#include "RmlGradient.h"

#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/Math.h>

#include <algorithm>
#include <cassert>
#include <cmath>

namespace
{
constexpr std::size_t kRampBytesPerTexel = 4;

std::vector<std::byte> BuildRampPixels(const Rml::ColorStopList& stops)
{
    assert(std::is_sorted(stops.begin(), stops.end(), [](const Rml::ColorStop& a, const Rml::ColorStop& b) {
        return a.position.number < b.position.number;
    }) && "RmlUi resolves stop positions to numbers along the gradient, never decreasing");
    std::vector<std::byte> pixels(kUiGradientRampTexels * kRampBytesPerTexel);
    const float first = stops.front().position.number;
    const float last = stops.back().position.number;
    std::size_t segmentEnd = 1;
    for (std::uint32_t texel = 0; texel < kUiGradientRampTexels; ++texel)
    {
        const float at = first + (last - first) * static_cast<float>(texel)
            / static_cast<float>(kUiGradientRampTexels - 1);
        while (segmentEnd + 1 < stops.size() && stops[segmentEnd].position.number < at)
            ++segmentEnd;

        Rml::ColourbPremultiplied colour = stops.back().color;
        if (stops.size() > 1)
        {
            const Rml::ColorStop& from = stops[segmentEnd - 1];
            const Rml::ColorStop& to = stops[segmentEnd];
            const float span = to.position.number - from.position.number;
            const float share = span > 0.0f ? std::clamp((at - from.position.number) / span, 0.0f, 1.0f)
                                            : (at < to.position.number ? 0.0f : 1.0f);
            colour = Rml::Math::RoundedLerp(share, from.color, to.color);
        }
        std::byte* out = pixels.data() + texel * kRampBytesPerTexel;
        out[0] = static_cast<std::byte>(colour.red);
        out[1] = static_cast<std::byte>(colour.green);
        out[2] = static_cast<std::byte>(colour.blue);
        out[3] = static_cast<std::byte>(colour.alpha);
    }
    return pixels;
}

float InverseRadius(float radius)
{
    constexpr float kZeroRadiusPutsEveryPointPastTheLastStop = 1.0e6f;
    return radius > 0.0f ? 1.0f / radius : kZeroRadiusPutsEveryPointPastTheLastStop;
}

Vec2d PointParameter(const Rml::Dictionary& parameters, const Rml::String& key)
{
    const Rml::Vector2f point = Rml::Get(parameters, key, Rml::Vector2f(0.0f));
    return Vec2d{ point.x, point.y };
}
} // namespace

std::optional<UiGradientShape> GradientShapeForShader(std::string_view shaderName)
{
    if (shaderName == "linear-gradient")
        return UiGradientShape::Linear;
    if (shaderName == "radial-gradient")
        return UiGradientShape::Radial;
    if (shaderName == "conic-gradient")
        return UiGradientShape::Conic;
    return std::nullopt;
}

std::optional<CompiledGradient> CompileGradient(UiGradientShape shape, const Rml::Dictionary& parameters)
{
    const Rml::Variant* stopList = Rml::GetIf(parameters, "color_stop_list");
    if (stopList == nullptr || stopList->GetType() != Rml::Variant::COLORSTOPLIST)
        return std::nullopt;
    const Rml::ColorStopList& stops = stopList->GetReference<Rml::ColorStopList>();
    if (stops.empty())
        return std::nullopt;

    UiGradient gradient;
    gradient.Shape = shape;
    gradient.Repeating = Rml::Get(parameters, "repeating", false);
    gradient.FirstStop = stops.front().position.number;
    gradient.LastStop = stops.back().position.number;
    switch (shape)
    {
    case UiGradientShape::Linear:
        gradient.Origin = PointParameter(parameters, "p0");
        gradient.Axis = PointParameter(parameters, "p1") - gradient.Origin;
        break;
    case UiGradientShape::Radial:
    {
        gradient.Origin = PointParameter(parameters, "center");
        const Rml::Vector2f radius = Rml::Get(parameters, "radius", Rml::Vector2f(1.0f));
        gradient.Axis = Vec2d{ InverseRadius(radius.x), InverseRadius(radius.y) };
        break;
    }
    case UiGradientShape::Conic:
    {
        gradient.Origin = PointParameter(parameters, "center");
        const float angle = Rml::Get(parameters, "angle", 0.0f);
        gradient.Axis = Vec2d{ std::cos(angle), std::sin(angle) };
        break;
    }
    }
    return CompiledGradient{ gradient, BuildRampPixels(stops) };
}
