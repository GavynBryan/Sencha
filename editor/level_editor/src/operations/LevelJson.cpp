#include "LevelJson.h"

#include <cmath>

JsonValue MetresJson(double value)
{
    return JsonValue(std::round(value * 1000.0) / 1000.0);
}

JsonValue PointJson(const Vec3d& point)
{
    return JsonValue(JsonValue::Array{ MetresJson(point.X), MetresJson(point.Y), MetresJson(point.Z) });
}

JsonValue GroundJson(const Vec2d& point)
{
    return JsonValue(JsonValue::Array{ MetresJson(point.X), MetresJson(point.Y) });
}

Vec3d PointFromJson(const JsonValue& value)
{
    const JsonValue::Array& a = value.AsArray();
    return Vec3d{ static_cast<float>(a[0].AsNumber()), static_cast<float>(a[1].AsNumber()),
                  static_cast<float>(a[2].AsNumber()) };
}
