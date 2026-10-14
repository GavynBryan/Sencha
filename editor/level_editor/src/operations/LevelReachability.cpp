#include "LevelReachability.h"

#include "document/DocumentNavigation.h"
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

    std::vector<std::string> NamesFrom(const JsonValue* value)
    {
        std::vector<std::string> names;
        if (value != nullptr && value->IsArray())
            for (const JsonValue& name : value->AsArray())
                names.push_back(name.AsString());
        return names;
    }

    JsonValue NamesJson(const std::vector<std::string>& names)
    {
        JsonValue::Array array;
        for (const std::string& name : names)
            array.push_back(JsonValue(name));
        return JsonValue(std::move(array));
    }

    // What the scenario says about each traversal kind the level's links use.
    struct Assumptions
    {
        std::vector<std::string> Held;
        std::vector<std::string> Denied;
        std::vector<std::string> Unknown;
    };

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
    entry.Definition.Parameters = DataField::Record({}, {}, {}, {
        Point("from", "world position"),
        Point("to", "world position"),
        Names("held", "traversal kinds the player can perform, e.g. navigation.traversal.jump"),
        Names("denied", "traversal kinds the player cannot perform"),
        DataField::Optional("profile", {}, "navigation profile; the first one when absent",
                            DataField::Field({}, DataFieldKind::String, {}, {})),
    });

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
        DataField::Record("assumptions", {}, {}, {
            Names("held", {}), Names("denied", {}), Names("unknown", {}),
        }),
        DataField::Field("profile", DataFieldKind::String, {}, {}),
        Point("from", "the start snapped onto the walkable surface"),
        Point("to", "the end snapped onto the walkable surface"),
    });

    entry.Execute = [&level](const JsonValue& arguments) {
        if (level.Document().ContentRoots().empty())
            return EditorOperationOutcome::Refused(
                "nav.unavailable", "navigation needs a project: no content root holds navigation.settings");
        const EditorDocument& document = level.Document();
        const DocumentNavigation built = BuildDocumentNavigation(document, level.Document().ContentRoots().front());
        if (!built.File)
        {
            std::string why = "the project has no navigation.settings asset or the level has no walkable geometry";
            if (!built.Diagnostics.empty())
                why = built.Diagnostics.front().Rule + ": " + built.Diagnostics.front().Message;
            return EditorOperationOutcome::Refused("nav.unavailable", why);
        }
        const NavigationFile& file = *built.File;

        // The analysis names its own vocabulary: every name the built file and
        // the scenario use, registered with their parents.
        GameplayTagRegistry tags;
        std::set<std::string> kinds;
        for (const NavLinkRecord& link : file.Links)
            kinds.insert(link.Traversal);
        const std::vector<std::string> held = NamesFrom(arguments.Find("held"));
        const std::vector<std::string> denied = NamesFrom(arguments.Find("denied"));
        for (const NavProfileRecord& profile : file.Profiles)
            (void)tags.RegisterTag(profile.Name);
        for (const std::string& area : file.Areas)
            (void)tags.RegisterTag(area);
        for (const std::vector<std::string>* names : { &held, &denied })
            for (const std::string& name : *names)
                if (!tags.RegisterTag(name))
                    return EditorOperationOutcome::Refused("scenario.malformed",
                                                           "'" + name + "' is not a tag name");
        for (const std::string& kind : kinds)
            (void)tags.RegisterTag(kind);

        const auto covers = [&tags](const std::vector<std::string>& by, const std::string& kind) {
            return std::ranges::any_of(by, [&](const std::string& name) {
                return tags.IsDescendantOf(tags.FindTag(kind), tags.FindTag(name));
            });
        };
        Assumptions assumed{ held, denied, {} };
        std::vector<GameplayTagId> sure;
        for (const std::string& name : held)
            sure.push_back(tags.FindTag(name));
        std::vector<GameplayTagId> possible = sure;
        for (const std::string& kind : kinds)
        {
            const bool isHeld = covers(held, kind);
            const bool isDenied = covers(denied, kind);
            if (isHeld && isDenied)
                return EditorOperationOutcome::Refused(
                    "scenario.contradictory", "'" + kind + "' is both held and denied");
            if (!isHeld && !isDenied)
            {
                assumed.Unknown.push_back(kind);
                possible.push_back(tags.FindTag(kind));
            }
        }

        const JsonValue* profileArgument = arguments.Find("profile");
        const std::string profile = profileArgument != nullptr && profileArgument->IsString()
            ? profileArgument->AsString()
            : (file.Profiles.empty() ? std::string{} : file.Profiles.front().Name);
        if (!tags.FindTag(profile).IsValid())
            return EditorOperationOutcome::Refused("nav.profile_unknown",
                                                   "the navigation has no profile '" + profile + "'");

        const ZoneId zone{ 1 };
        ZoneNavigation navigation;
        if (!navigation.Load(zone, file, &tags))
            return EditorOperationOutcome::Refused("nav.unavailable", "the built navigation did not load");

        NavQueryContextConfig contextConfig;
        contextConfig.MaxNodes = 32768;
        contextConfig.MaxCorridor = 4096;
        NavQueryContext context(contextConfig);
        NavQueryRequest request;
        request.Zone = zone;
        request.Profile = tags.FindTag(profile);

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

        request.Capabilities = sure;
        Walk sureWalk = Search(navigation, context, request, ends[0], ends[1], tags);
        Walk possibleWalk = sureWalk;
        if (sureWalk.Status != NavStatus::Success)
        {
            request.Capabilities = possible;
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
                if (std::ranges::find(assumed.Unknown, crossed) != assumed.Unknown.end()
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
            { "assumptions", JsonValue(JsonValue::Object{
                { "held", NamesJson(assumed.Held) },
                { "denied", NamesJson(assumed.Denied) },
                { "unknown", NamesJson(assumed.Unknown) },
            }) },
            { "profile", JsonValue(profile) },
            { "from", PointJson(ends[0].Position) },
            { "to", PointJson(ends[1].Position) },
        }));
    };
    return entry;
}
