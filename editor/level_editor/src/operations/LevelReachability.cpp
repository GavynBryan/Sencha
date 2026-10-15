#include "LevelReachability.h"

#include "LevelNavigation.h"

#include "document/EditorDocument.h"
#include "operations/LevelTarget.h"
#include "operations/LevelJson.h"

#include <core/metadata/DataSchemaFields.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <navigation/NavQueryContext.h>
#include <navigation/NavigationQuery.h>
#include <navigation/ZoneNavigation.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{
    DataFieldSchema Names(std::string key, std::string summary)
    {
        return DataField::ArrayOf(std::move(key), {}, std::move(summary),
                                  DataField::Field({}, DataFieldKind::String, {}, {}));
    }

    DataFieldSchema Point(std::string key, std::string summary)
    {
        return DataField::Vector(std::move(key), {}, std::move(summary));
    }

    JsonValue NamesJson(const std::vector<std::string>& names)
    {
        JsonValue::Array array;
        for (const std::string& name : names)
            array.push_back(JsonValue(name));
        return JsonValue(std::move(array));
    }

    struct Walk
    {
        NavStatus Status = NavStatus::NoPath;
        JsonValue Route;
        std::vector<std::string> Crossed;
    };

    Walk Search(const ZoneNavigation& navigation, NavQueryContext& context,
                const NavQueryRequest& request, const NavLocation& from, const NavLocation& to,
                const GameplayTagRegistry& tags)
    {
        Walk walk;
        NavRouteBuffer route(256, 4096, 256);
        walk.Status = NavFindRoute(navigation, context, request, from, to, route);
        if (walk.Status != NavStatus::Success)
            return walk;

        JsonValue::Array steps;
        double length = 0.0;
        Vec3d at = from.Position;
        const std::span<const Vec3d> corners = route.Corners();
        for (const NavRouteStep& step : route.Steps())
        {
            JsonValue traversal;
            Vec3d start = at;
            if (step.Kind == NavRouteStepKind::Walk)
            {
                for (std::uint32_t i = 0; i < step.CornerCount; ++i)
                {
                    const Vec3d& corner = corners[step.FirstCorner + i];
                    length += (corner - at).Magnitude();
                    at = corner;
                }
            }
            else
            {
                const std::string name(tags.GetName(step.Traversal));
                traversal = JsonValue(name);
                walk.Crossed.push_back(name);
                start = step.Entry;
                length += (step.Entry - at).Magnitude() + (step.Exit - step.Entry).Magnitude();
                at = step.Exit;
            }
            steps.push_back(JsonValue(JsonValue::Object{
                { "kind", JsonValue(step.Kind == NavRouteStepKind::Walk ? "walk" : "link") },
                { "traversal", std::move(traversal) },
                { "from", PointJson(start) },
                { "to", PointJson(at) },
            }));
        }
        walk.Route = JsonValue(JsonValue::Object{
            { "length", MetresJson(length) },
            { "steps", JsonValue(std::move(steps)) },
        });
        return walk;
    }
}

EditorOperationEntry ReachabilityOperation(const LevelTarget& level)
{
    EditorOperationEntry entry;
    entry.Definition.Name = "nav.reachable";
    entry.Definition.Category = "Level";
    entry.Definition.Description =
        "Whether the walkable surface connects two points, asked twice: with only the held "
        "traversal kinds, and with every kind the scenario leaves unknown as well. Reached and "
        "unreached are verified under the stated assumptions; depends_on names the unknown kinds "
        "the cheapest open route crosses.";
    std::vector<DataFieldSchema> parameters{ Point("from", "world position"), Point("to", "world position") };
    for (DataFieldSchema& field : ScenarioParameters())
        parameters.push_back(std::move(field));
    entry.Definition.Parameters = DataField::Record({}, {}, {}, std::move(parameters));

    const DataFieldSchema routeSchema = DataField::Record({}, {}, {}, {
        DataField::Field("length", DataFieldKind::Float, {}, "metres"),
        DataField::ArrayOf("steps", {}, {},
            DataField::Record({}, {}, {}, {
                DataField::Enum("kind", {}, {}, { { "walk", "Walk", {} }, { "link", "Link", {} } }),
                DataField::Optional("traversal", {}, {}, DataField::Field({}, DataFieldKind::String, {}, {}), true),
                Point("from", {}),
                Point("to", {}),
            }),
            true),
    });
    entry.Definition.Result = DataField::Record({}, {}, {}, {
        DataField::Enum("outcome", {}, {},
                        { { "reached", "Reached", "verified under the assumptions" },
                          { "depends_on", "Depends on", "only if unknown kinds are possible" },
                          { "unreached", "Unreached", "verified under the assumptions" },
                          { "incomplete", "Incomplete", "a search stopped before an answer" } }),
        Names("depends_on", "unknown traversal kinds the cheapest open route crosses"),
        DataField::Field("sure", DataFieldKind::String, {}, "the walk with unknown kinds blocked"),
        DataField::Field("possible", DataFieldKind::String, {}, "the walk with unknown kinds open"),
        DataField::Optional("route", {}, {}, routeSchema, true),
        AssumptionsSchema(),
        DataField::Field("profile", DataFieldKind::String, {}, {}),
        Point("from", "the start snapped onto the walkable surface"),
        Point("to", "the end snapped onto the walkable surface"),
    });

    entry.Execute = [&level](const JsonValue& arguments) {
        EditorOperationOutcome refusal;
        const std::unique_ptr<LevelNavigation> built = BuildLevelNavigation(level.Document(), arguments, refusal);
        if (built == nullptr)
            return refusal;
        const ZoneNavigation& navigation = built->Navigation;
        const GameplayTagRegistry& tags = built->Tags;

        NavQueryContextConfig contextConfig;
        contextConfig.MaxNodes = 32768;
        contextConfig.MaxCorridor = 4096;
        NavQueryContext context(contextConfig);
        NavQueryRequest request = built->Request(built->Sure);

        NavLocation ends[2];
        const char* labels[2] = { "from", "to" };
        for (int i = 0; i < 2; ++i)
        {
            const NavProjectResult projected = NavProjectPoint(
                navigation, context, request, PointFromJson(*arguments.Find(labels[i])), Vec3d(1.0f, 2.0f, 1.0f));
            if (projected.Status != NavStatus::Success)
                return EditorOperationOutcome::Refused(
                    "nav.off_mesh", std::string("the '") + labels[i] + "' point is not near walkable surface",
                    JsonValue(JsonValue::Object{ { "endpoint", JsonValue(labels[i]) },
                                                 { "status", JsonValue(NavStatusName(projected.Status)) } }));
            ends[i] = projected.Location;
        }

        Walk sureWalk = Search(navigation, context, request, ends[0], ends[1], tags);
        Walk possibleWalk = sureWalk;
        if (sureWalk.Status != NavStatus::Success)
        {
            request.Capabilities = built->Possible;
            possibleWalk = Search(navigation, context, request, ends[0], ends[1], tags);
        }

        std::string outcome = "unreached";
        std::vector<std::string> dependsOn;
        JsonValue route;
        const auto stopped = [](NavStatus status) {
            return status == NavStatus::Partial || status == NavStatus::SearchLimitReached;
        };
        if (sureWalk.Status == NavStatus::Success)
        {
            outcome = "reached";
            route = std::move(sureWalk.Route);
        }
        else if (possibleWalk.Status == NavStatus::Success)
        {
            outcome = "depends_on";
            for (const std::string& crossed : possibleWalk.Crossed)
                if (std::ranges::find(built->Unknown, crossed) != built->Unknown.end()
                    && std::ranges::find(dependsOn, crossed) == dependsOn.end())
                    dependsOn.push_back(crossed);
            route = std::move(possibleWalk.Route);
        }
        else if (stopped(sureWalk.Status) || stopped(possibleWalk.Status))
        {
            outcome = "incomplete";
        }

        return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
            { "outcome", JsonValue(outcome) },
            { "depends_on", NamesJson(dependsOn) },
            { "sure", JsonValue(NavStatusName(sureWalk.Status)) },
            { "possible", JsonValue(NavStatusName(possibleWalk.Status)) },
            { "route", std::move(route) },
            { "assumptions", built->AssumptionsJson() },
            { "profile", JsonValue(built->Profile) },
            { "from", PointJson(ends[0].Position) },
            { "to", PointJson(ends[1].Position) },
        }));
    };
    return entry;
}
