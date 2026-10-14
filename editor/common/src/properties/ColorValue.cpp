#include "properties/ColorValue.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

Hsv HsvOf(Rgb color)
{
    const double high = std::max({ color.R, color.G, color.B });
    const double low = std::min({ color.R, color.G, color.B });
    const double chroma = high - low;
    Hsv out{ .H = 0.0, .S = high > 0.0 ? chroma / high : 0.0, .V = high };
    if (chroma <= 0.0)
        return out;
    double sixths = 0.0;
    if (high == color.R)
        sixths = std::fmod((color.G - color.B) / chroma + 6.0, 6.0);
    else if (high == color.G)
        sixths = (color.B - color.R) / chroma + 2.0;
    else
        sixths = (color.R - color.G) / chroma + 4.0;
    out.H = sixths / 6.0;
    return out;
}

Rgb RgbOf(Hsv color)
{
    const double hue = color.H - std::floor(color.H);
    const double sixths = hue * 6.0;
    const double chroma = color.V * color.S;
    const double second = chroma * (1.0 - std::fabs(std::fmod(sixths, 2.0) - 1.0));
    const double low = color.V - chroma;
    const int sector = static_cast<int>(sixths) % 6;
    const double rgb[6][3] = { { chroma, second, 0.0 }, { second, chroma, 0.0 }, { 0.0, chroma, second },
                               { 0.0, second, chroma }, { second, 0.0, chroma }, { chroma, 0.0, second } };
    return { rgb[sector][0] + low, rgb[sector][1] + low, rgb[sector][2] + low };
}

std::optional<ColorValue> ParseColorValue(std::string_view text)
{
    std::vector<double> channels;
    while (!text.empty())
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == ','))
            text.remove_prefix(1);
        if (text.empty())
            break;
        double value = 0.0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc{})
            return std::nullopt;
        channels.push_back(value);
        text.remove_prefix(static_cast<std::size_t>(parsed.ptr - text.data()));
    }
    if (channels.size() < 3)
        return std::nullopt;
    return ColorValue{ .Color = { channels[0], channels[1], channels[2] },
                       .Rest = std::vector<double>(channels.begin() + 3, channels.end()) };
}

std::string ColorValueText(const ColorValue& value)
{
    std::string text = std::format("{:.4g}, {:.4g}, {:.4g}", value.Color.R, value.Color.G, value.Color.B);
    for (const double channel : value.Rest)
        text += std::format(", {:.4g}", channel);
    return text;
}

std::string ColorSwatch(Rgb color)
{
    const auto byte = [](double channel) { return static_cast<int>(std::clamp(channel, 0.0, 1.0) * 255.0 + 0.5); };
    return std::format("rgba({}, {}, {}, 255)", byte(color.R), byte(color.G), byte(color.B));
}
