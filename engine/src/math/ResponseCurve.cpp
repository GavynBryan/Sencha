#include <math/ResponseCurve.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace
{
    // 0 at `from`, 1 at `to`, clamped. `from == to` is a step at that point.
    [[nodiscard]] float Ramp(float value, float from, float to)
    {
        if (from == to)
            return value >= to ? 1.0f : 0.0f;
        return std::clamp((value - from) / (to - from), 0.0f, 1.0f);
    }

    [[nodiscard]] float Band(const ResponseCurve& curve, float value)
    {
        if (value < curve.Low || value > curve.High)
            return 0.0f;
        if (value < curve.PreferredLow)
            return Ramp(value, curve.Low, curve.PreferredLow);
        if (value > curve.PreferredHigh)
            return 1.0f - Ramp(value, curve.PreferredHigh, curve.High);
        return 1.0f;
    }

    [[nodiscard]] DataFieldSchema Bound(std::string key, std::string displayName, bool required)
    {
        DataFieldSchema field = MakeDataField(DataFieldKind::Float, std::move(key), std::move(displayName));
        field.Required = required;
        return field;
    }
}

float EvaluateResponseCurve(const ResponseCurve& curve, float value)
{
    const bool rising = curve.Direction == ResponseCurveDirection::Rising;
    switch (curve.Shape)
    {
    case ResponseCurveShape::Linear:
    {
        const float rise = Ramp(value, curve.Low, curve.High);
        return rising ? rise : 1.0f - rise;
    }
    case ResponseCurveShape::Step:
        return (rising ? value >= curve.Low : value <= curve.Low) ? 1.0f : 0.0f;
    case ResponseCurveShape::Band:
        return Band(curve, value);
    }
    return 0.0f;
}

DataFieldSchema MakeResponseCurveField(std::string key, std::string displayName)
{
    DataFieldSchema shape = MakeDataField(DataFieldKind::Enum, "shape", "Shape");
    shape.EnumChoices = { { "linear", "Linear", "Low to high, clamped outside." },
                          { "step", "Step", "All or nothing at the threshold (low)." },
                          { "band", "Band", "Best inside the preferred range." } };

    DataFieldSchema direction = MakeDataField(DataFieldKind::Enum, "direction", "Direction");
    direction.EnumChoices = { { "rising", "Rising", "Higher values score higher." },
                              { "falling", "Falling", "Lower values score higher." } };
    direction.Required = false;
    direction.Default = std::string("rising");

    DataFieldSchema curve = MakeDataField(DataFieldKind::Record, std::move(key), std::move(displayName));
    curve.Children.push_back(std::move(shape));
    curve.Children.push_back(std::move(direction));
    curve.Children.push_back(Bound("low", "Low", true));
    curve.Children.push_back(Bound("high", "High", false));
    curve.Children.push_back(Bound("preferred_low", "Preferred low", false));
    curve.Children.push_back(Bound("preferred_high", "Preferred high", false));
    return curve;
}

std::optional<ResponseCurve> ReadResponseCurve(const JsonValue& value, std::string& error)
{
    ResponseCurve curve;
    const JsonValue* shape = value.Find("shape");
    const std::string shapeName = shape != nullptr && shape->IsString() ? shape->AsString() : "";
    if (shapeName == "step")
        curve.Shape = ResponseCurveShape::Step;
    else if (shapeName == "band")
        curve.Shape = ResponseCurveShape::Band;
    else if (shapeName != "linear")
    {
        error = std::format("unknown curve shape '{}'", shapeName);
        return std::nullopt;
    }

    const JsonValue* direction = value.Find("direction");
    if (direction != nullptr && direction->IsString() && direction->AsString() == "falling")
        curve.Direction = ResponseCurveDirection::Falling;

    curve.Low = static_cast<float>(value.NumberOr("low", 0.0));
    curve.High = static_cast<float>(value.NumberOr("high", curve.Low));
    curve.PreferredLow = static_cast<float>(value.NumberOr("preferred_low", curve.Low));
    curve.PreferredHigh = static_cast<float>(value.NumberOr("preferred_high", curve.High));

    if (curve.Shape == ResponseCurveShape::Linear && !(curve.High > curve.Low))
        error = "a linear curve needs high greater than low";
    else if (curve.Shape == ResponseCurveShape::Band
             && !(curve.Low <= curve.PreferredLow && curve.PreferredLow <= curve.PreferredHigh
                  && curve.PreferredHigh <= curve.High))
        error = "a band curve needs low <= preferred_low <= preferred_high <= high";
    if (!error.empty())
        return std::nullopt;
    return curve;
}
