#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct Rgb
{
    double R = 0.0;
    double G = 0.0;
    double B = 0.0;
};

// Hue in turns from red, saturation and value from 0 to 1.
struct Hsv
{
    double H = 0.0;
    double S = 0.0;
    double V = 0.0;
};

[[nodiscard]] Hsv HsvOf(Rgb color);
[[nodiscard]] Rgb RgbOf(Hsv color);

// A colour as a property writes it: red, green and blue, then any further
// channel (alpha) carried through untouched.
struct ColorValue
{
    Rgb Color;
    std::vector<double> Rest = {};
};

[[nodiscard]] std::optional<ColorValue> ParseColorValue(std::string_view text);
[[nodiscard]] std::string ColorValueText(const ColorValue& value);
// The swatch a document paints for it, "rgba(r, g, b, 255)", channels clamped.
[[nodiscard]] std::string ColorSwatch(Rgb color);
