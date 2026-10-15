#include "LevelJson.h"

#include <core/metadata/DataSchemaFields.h>

#include <array>
#include <cmath>
#include <numbers>
#include <vector>

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

std::string_view CompassBearing(const Vec3d& from, const Vec3d& to)
{
    const float east = to.X - from.X;
    const float north = from.Z - to.Z;
    if (east * east + north * north < 1.0e-6f)
        return "none";
    static constexpr std::array<std::string_view, 8> kWords = { "n", "ne", "e", "se", "s", "sw", "w", "nw" };
    const double turns = std::atan2(east, north) / (2.0 * std::numbers::pi) + 1.0 / 16.0;
    const double wrapped = turns - std::floor(turns);
    return kWords[static_cast<std::size_t>(wrapped * 8.0) % 8];
}

DataFieldSchema CompassBearingField(std::string key, std::string summary)
{
    std::vector<DataEnumChoice> words;
    for (std::string_view word : { "n", "ne", "e", "se", "s", "sw", "w", "nw", "none" })
        words.push_back({ std::string(word), std::string(word), {} });
    return DataField::Enum(std::move(key), {}, std::move(summary), std::move(words));
}

std::optional<Vec3d> AxisDirection(std::string_view name)
{
    if (name == "north") return Vec3d{ 0, 0, -1 };
    if (name == "south") return Vec3d{ 0, 0, 1 };
    if (name == "east") return Vec3d{ 1, 0, 0 };
    if (name == "west") return Vec3d{ -1, 0, 0 };
    if (name == "up") return Vec3d{ 0, 1, 0 };
    if (name == "down") return Vec3d{ 0, -1, 0 };
    return std::nullopt;
}

DataFieldSchema AxisDirectionField(std::string key, std::string summary)
{
    return DataField::Enum(std::move(key), {}, std::move(summary),
                           { { "north", "North", "-Z" }, { "south", "South", "+Z" }, { "east", "East", "+X" },
                             { "west", "West", "-X" }, { "up", "Up", "+Y" }, { "down", "Down", "-Y" } });
}
