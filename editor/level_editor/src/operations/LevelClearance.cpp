#include "LevelClearance.h"

#include "LevelJson.h"

#include "document/EditorDocument.h"
#include "document/NavigationCookInput.h"
#include "operations/LevelTarget.h"
#include "viewport/Picking.h"

#include <core/metadata/DataSchemaFields.h>
#include <math/geometry/3d/Ray3d.h>

#include <algorithm>
#include <optional>

namespace
{
    // Far enough for any room; a ray that reaches it meets nothing.
    constexpr float kReach = 100.0f;

    std::optional<float> DistanceTo(const EditorScene& scene, const Vec3d& from, const Vec3d& direction)
    {
        const std::optional<SurfaceHit> hit =
            CastRayAgainstBrushes(scene, Ray3d(from, direction), kReach, false);
        if (!hit)
            return std::nullopt;
        return (hit->Point - from).Magnitude();
    }

    JsonValue Maybe(const std::optional<float>& value)
    {
        return value ? MetresJson(*value) : JsonValue{};
    }

    DataFieldSchema Metres(std::string key, std::string summary)
    {
        return DataField::Optional(std::move(key), {}, std::move(summary),
                                   DataField::Field({}, DataFieldKind::Float, {}, {}), true);
    }
}

EditorOperationEntry ClearanceOperation(const LevelTarget& level)
{
    EditorOperationEntry entry;
    entry.Definition.Name = "measure.clearance";
    entry.Definition.Category = "Level";
    entry.Definition.Description =
        "The floor, ceiling and free width around a point in a passage, measured against the "
        "visible brushes; width is taken across the direction of travel at body height. With "
        "navigation settings, says whether the profile's capsule fits. Null means nothing bounds "
        "that side within 100 m.";
    entry.Definition.Parameters = DataField::Record({}, {}, {}, {
        DataField::Vector("point", {}, "a world position inside the passage"),
        DataField::Optional("direction", {}, "horizontal direction of travel; north (-z) when absent",
                            DataField::Vector({}, {}, {})),
    });
    entry.Definition.Result = DataField::Record({}, {}, {}, {
        Metres("floor", "world height of the floor under the point"),
        Metres("ceiling", "world height of the ceiling over the point"),
        Metres("height", "floor to ceiling"),
        Metres("width", "wall to wall across the direction of travel"),
        DataField::Optional("profile", {}, {},
            DataField::Record({}, {}, {}, {
                DataField::Field("name", DataFieldKind::String, {}, {}),
                DataField::Field("radius", DataFieldKind::Float, {}, "metres"),
                DataField::Field("height", DataFieldKind::Float, {}, "metres"),
                DataField::Field("fits_width", DataFieldKind::Bool, {}, {}),
                DataField::Field("fits_height", DataFieldKind::Bool, {}, {}),
            }),
            true),
    });

    entry.Execute = [&level](const JsonValue& arguments) {
        const EditorScene& scene = level.Document().GetScene();
        const Vec3d point = PointFromJson(*arguments.Find("point"));
        Vec3d travel{ 0.0f, 0.0f, -1.0f };
        if (const JsonValue* given = arguments.Find("direction"); given != nullptr && given->IsArray())
            travel = PointFromJson(*given);
        travel.Y = 0.0f;
        if (travel.SqrMagnitude() < 1.0e-8f)
            return EditorOperationOutcome::Refused("measure.direction_vertical",
                                                   "the direction of travel has no horizontal part");
        travel = travel.Normalized();
        const Vec3d across{ -travel.Z, 0.0f, travel.X };

        const std::optional<float> down = DistanceTo(scene, point, { 0.0f, -1.0f, 0.0f });
        const std::optional<float> up = DistanceTo(scene, point, { 0.0f, 1.0f, 0.0f });
        const std::optional<float> floor = down ? std::optional<float>(point.Y - *down) : std::nullopt;
        const std::optional<float> ceiling = up ? std::optional<float>(point.Y + *up) : std::nullopt;
        const std::optional<float> height =
            floor && ceiling ? std::optional<float>(*ceiling - *floor) : std::nullopt;

        std::optional<NavigationProfileSetting> profile;
        if (!level.Document().ContentRoots().empty())
        {
            NavigationSettingsLookup settings = FindNavigationSettings(level.Document().ContentRoots().front());
            if (settings.Settings && !settings.Settings->Profiles.empty())
                profile = settings.Settings->Profiles.front();
        }

        // Across the passage at the middle of a standing body, or the middle of
        // the gap when the ceiling is lower than that.
        float bodyHeight = profile ? profile->Build.Height : 1.8f;
        if (height)
            bodyHeight = std::min(bodyHeight, *height);
        Vec3d waist = point;
        if (floor)
            waist.Y = *floor + 0.5f * bodyHeight;
        const std::optional<float> left = DistanceTo(scene, waist, across * -1.0f);
        const std::optional<float> right = DistanceTo(scene, waist, across);
        const std::optional<float> width =
            left && right ? std::optional<float>(*left + *right) : std::nullopt;

        JsonValue fits;
        if (profile)
            fits = JsonValue(JsonValue::Object{
                { "name", JsonValue(profile->Name) },
                { "radius", MetresJson(profile->Build.Radius) },
                { "height", MetresJson(profile->Build.Height) },
                { "fits_width", JsonValue(!width || *width >= 2.0f * profile->Build.Radius) },
                { "fits_height", JsonValue(!height || *height >= profile->Build.Height) },
            });

        return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
            { "floor", Maybe(floor) },
            { "ceiling", Maybe(ceiling) },
            { "height", Maybe(height) },
            { "width", Maybe(width) },
            { "profile", std::move(fits) },
        }));
    };
    return entry;
}
