#pragma once

#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>

#include <cstdint>
#include <optional>
#include <string>

// Maps a raw value onto [0, 1] against authored bounds. Bounds are absolute,
// never the range of whatever values happen to be present.

enum class ResponseCurveShape : std::uint8_t
{
    // 0 at Low, 1 at High (Rising), clamped outside.
    Linear,
    // 1 at or past Low in the curve's direction, 0 otherwise.
    Step,
    // 1 inside [PreferredLow, PreferredHigh], falling to 0 at Low and High.
    Band,
};

enum class ResponseCurveDirection : std::uint8_t
{
    Rising,
    Falling,
};

struct ResponseCurve
{
    ResponseCurveShape Shape = ResponseCurveShape::Linear;
    ResponseCurveDirection Direction = ResponseCurveDirection::Rising;
    float Low = 0.0f;
    float High = 1.0f;
    float PreferredLow = 0.0f;
    float PreferredHigh = 0.0f;
};

[[nodiscard]] float EvaluateResponseCurve(const ResponseCurve& curve, float value);

// A record field describing one curve, for any data schema that authors one.
[[nodiscard]] DataFieldSchema MakeResponseCurveField(std::string key, std::string displayName);

// Reads a value the curve field has already accepted, then checks what a schema
// cannot: bounds order. Nullopt with `error` set when they are inconsistent.
[[nodiscard]] std::optional<ResponseCurve> ReadResponseCurve(const JsonValue& value, std::string& error);
