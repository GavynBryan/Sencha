#include "WorldReachability.h"

#include "LevelJson.h"
#include "LevelNavigation.h"
#include "LevelTarget.h"
#include "WorldZones.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/WorldDocument.h"

#include <core/metadata/DataSchemaFields.h>
#include <navigation/NavQueryContext.h>
#include <navigation/NavigationQuery.h>
#include <math/geometry/3d/Aabb3d.h>
#include <navigation/tooling/NavMeshSnapshot.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>
#include <zone/WorldConnectionComponents.h>
#include <zone/WorldPartitionManifest.h>

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    // How far past a dock's rectangle, and in front of or behind its plane,
    // walkable surface still reaches its mouth.
    constexpr float kMouthReach = 0.75f;
    constexpr float kMouthDepth = 1.0f;
    // How far below a dock the floor it drops onto may lie.
    constexpr float kLandingDrop = 20.0f;
    constexpr float kEverywhere = 1.0e9f;

    struct ZoneWalk
    {
        ZoneId Zone;
        std::unique_ptr<LevelNavigation> Navigation;
        NavMeshSnapshot Snapshot;
        std::unordered_map<std::uint64_t, std::uint32_t> PolygonOf;
    };

    // A dock, as travel sees it: a rectangle in its plane, crossed from zone A
    // to zone B along its normal, the other way, or both, and maybe gated.
    struct Connection
    {
        DockId Id;
        std::string Name;
        ZoneId A;
        ZoneId B;
        Transform3f Frame;
        Vec2d Half;
        std::uint32_t Directions = DockDirectionBoth;
        bool Gated = false;

        [[nodiscard]] bool Leaves(ZoneId zone) const
        {
            return (zone == A && (Directions & DockDirectionAToB) != 0) || (zone == B && (Directions & DockDirectionBToA) != 0);
        }
        [[nodiscard]] ZoneId Beyond(ZoneId zone) const { return zone == A ? B : A; }
        [[nodiscard]] Vec3d Into(ZoneId zone) const
        {
            const Vec3d normal = Frame.Rotation.RotateVector(Vec3d{ 0, 0, 1 });
            return zone == B ? normal : normal * -1.0f;
        }
    };

    struct Gates
    {
        std::set<std::uint64_t> Open;
        std::set<std::uint64_t> Closed;
    };

    // Every zone opened and its navigation built under the scenario, every
    // dock read with whether some scene binds a gate to it.
    struct WorldGraph
    {
        std::vector<ZoneWalk> Zones;
        std::vector<Connection> Connections;
        std::vector<std::string> Unknown;
        std::vector<std::string> Unconsidered;
        std::string Profile;

        [[nodiscard]] const ZoneWalk* Walk(ZoneId zone) const
        {
            const auto found = std::ranges::find(Zones, zone, &ZoneWalk::Zone);
            return found != Zones.end() ? &*found : nullptr;
        }
    };

    std::optional<WorldGraph> BuildGraph(WorldDocument& world, const JsonValue& arguments, EditorOperationOutcome& refusal)
    {
        if (!world.IsWorld())
        {
            refusal = EditorOperationOutcome::Refused("world.none", "a single level is open, not a world");
            return std::nullopt;
        }
        WorldGraph graph;
        std::set<std::uint64_t> gated;
        const auto readGates = [&gated](const EditorDocument& document) {
            const World& components = document.GetScene().GetRegistry().Components;
            for (const EntityId entity : document.GetScene().GetAllEntities())
                if (const DockGateBinding* binding = components.TryGet<DockGateBinding>(entity))
                    gated.insert(binding->Id.Value);
        };
        for (const ZoneHeader& header : world.Manifest().Zones)
        {
            if (world.ZoneDocument(header.Id) == nullptr)
                (void)world.LoadZone(header.Id);
            const EditorDocument* document = world.ZoneDocument(header.Id);
            if (document == nullptr)
                continue;
            readGates(*document);
            EditorOperationOutcome unwalkable;
            ZoneWalk walk{ header.Id, BuildLevelNavigation(*document, arguments, unwalkable), {}, {} };
            if (walk.Navigation == nullptr)
            {
                if (unwalkable.Code != "nav.unavailable")
                {
                    refusal = unwalkable;
                    return std::nullopt;
                }
                continue;
            }
            walk.Snapshot = SnapshotNavMesh(walk.Navigation->Navigation, walk.Navigation->ProfileIndex);
            for (std::uint32_t index = 0; index < walk.Snapshot.Polygons.size(); ++index)
                walk.PolygonOf.emplace(walk.Snapshot.Polygons[index].Region.Ref, index);
            for (const std::string& kind : walk.Navigation->Unknown)
                if (std::ranges::find(graph.Unknown, kind) == graph.Unknown.end())
                    graph.Unknown.push_back(kind);
            graph.Profile = walk.Navigation->Profile;
            graph.Zones.push_back(std::move(walk));
        }

        const EditorDocument& scene = world.WorldSceneDocument();
        readGates(scene);
        const World& components = scene.GetScene().GetRegistry().Components;
        for (const EntityId entity : scene.GetScene().GetAllEntities())
        {
            if (const WorldLink* link = components.TryGet<WorldLink>(entity))
                graph.Unconsidered.push_back(LinkIdToString(link->Id));
            const WorldDock* dock = components.TryGet<WorldDock>(entity);
            const Transform3f* frame = scene.GetScene().TryGetWorldTransform(entity);
            if (dock == nullptr || frame == nullptr)
                continue;
            graph.Connections.push_back({ dock->Id, DockIdToString(dock->Id), dock->ZoneA, dock->ZoneB, *frame,
                                          dock->HalfExtents, dock->Directions, gated.contains(dock->Id.Value) });
        }
        return graph;
    }

    std::optional<DockId> DockNamed(const WorldGraph& graph, std::string_view text)
    {
        for (const Connection& connection : graph.Connections)
            if (connection.Name == text)
                return connection.Id;
        return std::nullopt;
    }

    struct Arrival
    {
        std::size_t Zone = 0;
        NavLocation At;
        // The arrival this one left from, and through which connection.
        std::optional<std::size_t> From;
        std::size_t Through = 0;
    };

    struct Walked
    {
        std::vector<Arrival> Arrivals;
        // The regions reached in each zone.
        std::vector<std::set<std::uint64_t>> Reached;
        std::optional<std::size_t> Goal;
        bool Complete = true;
    };

    std::span<const GameplayTagId> Capabilities(const ZoneWalk& zone, bool possible)
    {
        return possible ? std::span<const GameplayTagId>(zone.Navigation->Possible)
                        : std::span<const GameplayTagId>(zone.Navigation->Sure);
    }

    bool Passable(const Connection& connection, const Gates& gates, bool possible)
    {
        if (!connection.Gated)
            return true;
        if (gates.Open.contains(connection.Id.Value))
            return true;
        return possible && !gates.Closed.contains(connection.Id.Value);
    }

    // Where the reached polygon of `zone` nearest the dock's middle meets its
    // mouth: the middle of the part of that polygon inside the box the dock's
    // rectangle reaches, so a long edge across a narrow dock counts, and the
    // place to route to lies on the polygon.
    std::optional<NavLocation> Mouth(const ZoneWalk& zone, const std::set<std::uint64_t>& reached, const Connection& connection)
    {
        const Aabb3d mouth(Vec3d{ -(connection.Half.X + kMouthReach), -(connection.Half.Y + kMouthReach), -kMouthDepth },
                           Vec3d{ connection.Half.X + kMouthReach, connection.Half.Y + kMouthReach, kMouthDepth });
        std::optional<NavLocation> best;
        float bestDistance = 0.0f;
        std::vector<Vec3d> local;
        for (const std::uint64_t ref : reached)
        {
            const NavSnapshotPolygon& polygon = zone.Snapshot.Polygons[zone.PolygonOf.at(ref)];
            local.clear();
            for (std::uint32_t v = 0; v < polygon.VertexCount; ++v)
                local.push_back(connection.Frame.InverseTransformPoint(zone.Snapshot.Vertices[polygon.FirstVertex + v]));
            const std::vector<Vec3d> inside = ClipConvexPolygon(local, mouth);
            if (inside.empty())
                continue;
            Vec3d middle = Vec3d::Zero();
            for (const Vec3d& point : inside)
                middle = middle + point;
            middle = connection.Frame.TransformPoint(middle * (1.0f / static_cast<float>(inside.size())));
            const float distance = (middle - connection.Frame.Position).Magnitude();
            if (best && distance >= bestDistance)
                continue;
            best = NavLocation{ polygon.Region.Zone, polygon.Region.Generation, polygon.Region.Profile, 0, ref, middle };
            bestDistance = distance;
        }
        return best;
    }

    // Where crossing the dock into `zone` puts you: its middle carried a step
    // into the zone, dropped onto the floor below it.
    std::optional<NavLocation> Landing(const ZoneWalk& zone, NavQueryContext& context, const Connection& connection,
                                       bool possible)
    {
        const Vec3d into = connection.Into(zone.Zone);
        const float across = std::max(connection.Half.X, connection.Half.Y) + kMouthReach;
        const NavProjectResult landed = NavProjectPoint(zone.Navigation->Navigation, context,
            zone.Navigation->Request(Capabilities(zone, possible)), connection.Frame.Position + into * kMouthDepth,
            Vec3d{ across, kLandingDrop, across });
        return landed.Status == NavStatus::Success ? std::optional<NavLocation>(landed.Location) : std::nullopt;
    }

    // Whether a walk got to `zone`'s side of the dock: to its mouth there, or,
    // for a dock that drops into `zone`, to where it lands.
    bool SideReached(const ZoneWalk& zone, const std::set<std::uint64_t>& reached, const Connection& connection,
                     NavQueryContext& context)
    {
        if (Mouth(zone, reached, connection))
            return true;
        const std::optional<NavLocation> landed = Landing(zone, context, connection, false);
        return landed && reached.contains(landed->Ref);
    }

    Walked Walk(const WorldGraph& graph, NavQueryContext& context, std::size_t startZone, const NavLocation& start,
                std::optional<std::pair<std::size_t, std::uint64_t>> goal, const Gates& gates, bool possible)
    {
        Walked walked;
        walked.Reached.resize(graph.Zones.size());
        walked.Arrivals.push_back({ startZone, start, std::nullopt, 0 });
        std::set<std::pair<std::size_t, std::uint64_t>> visited{ { startZone, start.Ref } };
        for (std::size_t next = 0; next < walked.Arrivals.size(); ++next)
        {
            const Arrival arrival = walked.Arrivals[next];
            const ZoneWalk& zone = graph.Zones[arrival.Zone];
            NavReachableBuffer regions(zone.Snapshot.Polygons.size() + 1);
            if (NavCollectReachable(zone.Navigation->Navigation, context, zone.Navigation->Request(Capabilities(zone, possible)),
                                    arrival.At, kEverywhere, kEverywhere, regions)
                != NavStatus::Success)
            {
                walked.Complete = false;
                continue;
            }
            std::set<std::uint64_t> reached;
            for (const NavReachableRegion& region : regions.Regions())
                reached.insert(region.Region.Ref);
            walked.Reached[arrival.Zone].insert(reached.begin(), reached.end());
            if (goal && goal->first == arrival.Zone && reached.contains(goal->second))
            {
                walked.Goal = next;
                return walked;
            }
            for (std::size_t c = 0; c < graph.Connections.size(); ++c)
            {
                const Connection& connection = graph.Connections[c];
                if (!connection.Leaves(zone.Zone) || !Passable(connection, gates, possible) || !Mouth(zone, reached, connection))
                    continue;
                const auto beyond = std::ranges::find(graph.Zones, connection.Beyond(zone.Zone), &ZoneWalk::Zone);
                if (beyond == graph.Zones.end())
                    continue;
                const std::size_t other = static_cast<std::size_t>(beyond - graph.Zones.begin());
                const std::optional<NavLocation> landed = Landing(*beyond, context, connection, possible);
                if (landed && visited.insert({ other, landed->Ref }).second)
                    walked.Arrivals.push_back({ other, *landed, next, c });
            }
        }
        return walked;
    }

    std::optional<std::pair<std::size_t, NavLocation>> Locate(const WorldGraph& graph, const WorldDocument& world,
                                                               NavQueryContext& context, const Vec3d& point)
    {
        const ZoneHeader* header = InnermostZoneAt(world.Manifest(), point);
        if (header == nullptr)
            return std::nullopt;
        const auto zone = std::ranges::find(graph.Zones, header->Id, &ZoneWalk::Zone);
        if (zone == graph.Zones.end())
            return std::nullopt;
        const NavProjectResult projected = NavProjectPoint(zone->Navigation->Navigation, context,
            zone->Navigation->Request(zone->Navigation->Possible), point, Vec3d{ 1.0f, 2.0f, 1.0f });
        if (projected.Status != NavStatus::Success)
            return std::nullopt;
        return std::pair{ static_cast<std::size_t>(zone - graph.Zones.begin()), projected.Location };
    }

    std::optional<EditorOperationOutcome> ReadGates(const WorldGraph& graph, const JsonValue& arguments, Gates& gates)
    {
        for (const auto& [key, into] : { std::pair{ "open_gates", &gates.Open }, std::pair{ "closed_gates", &gates.Closed } })
            if (const JsonValue* names = arguments.Find(key); names != nullptr && names->IsArray())
                for (const JsonValue& name : names->AsArray())
                {
                    const std::optional<DockId> dock = DockNamed(graph, name.AsString());
                    if (!dock)
                        return EditorOperationOutcome::Refused("world.dock_unknown", "the world has no dock " + name.AsString());
                    const auto connection = std::ranges::find(graph.Connections, *dock, &Connection::Id);
                    if (!connection->Gated)
                        return EditorOperationOutcome::Refused("world.dock_not_gated",
                                                               "dock " + name.AsString() + " has no gate to assume open or shut");
                    into->insert(dock->Value);
                }
        return std::nullopt;
    }

    std::vector<DataFieldSchema> Parameters(std::vector<DataFieldSchema> first)
    {
        for (DataFieldSchema& field : ScenarioParameters())
            first.push_back(std::move(field));
        for (const char* key : { "open_gates", "closed_gates" })
            first.push_back(DataField::ArrayOf(key, {},
                std::string(key) == "open_gates" ? "gated docks assumed open, by dock id" : "gated docks assumed shut, by dock id",
                DataField::Field({}, DataFieldKind::String, {}, {})));
        return first;
    }

    JsonValue Names(const std::vector<std::string>& names)
    {
        JsonValue::Array array;
        for (const std::string& name : names)
            array.push_back(JsonValue(name));
        return JsonValue(std::move(array));
    }

    struct Leg
    {
        ZoneId Zone;
        std::vector<std::string> Crossed;
    };

    // The walk to the goal, leg by leg, with the traversal kinds each crosses
    // and the docks between.
    std::vector<Leg> Legs(const WorldGraph& graph, NavQueryContext& context, const Walked& walked, const NavLocation& goal,
                          bool possible, std::vector<std::size_t>& docks)
    {
        std::vector<std::size_t> chain;
        for (std::optional<std::size_t> at = walked.Goal; at; at = walked.Arrivals[*at].From)
            chain.push_back(*at);
        std::ranges::reverse(chain);
        std::vector<Leg> legs;
        for (std::size_t step = 0; step < chain.size(); ++step)
        {
            const Arrival& arrival = walked.Arrivals[chain[step]];
            const ZoneWalk& zone = graph.Zones[arrival.Zone];
            NavLocation end = goal;
            if (step + 1 < chain.size())
            {
                const std::size_t through = walked.Arrivals[chain[step + 1]].Through;
                docks.push_back(through);
                end = Mouth(zone, walked.Reached[arrival.Zone], graph.Connections[through]).value_or(arrival.At);
            }
            Leg leg{ zone.Zone, {} };
            NavRouteBuffer route(256, 4096, 256);
            if (NavFindRoute(zone.Navigation->Navigation, context, zone.Navigation->Request(Capabilities(zone, possible)),
                             arrival.At, end, route)
                == NavStatus::Success)
                for (const NavRouteStep& routeStep : route.Steps())
                    if (routeStep.Kind != NavRouteStepKind::Walk)
                        leg.Crossed.push_back(std::string(zone.Navigation->Tags.GetName(routeStep.Traversal)));
            legs.push_back(std::move(leg));
        }
        return legs;
    }

    std::unique_ptr<NavQueryContext> Context(const WorldGraph& graph)
    {
        std::size_t most = 0;
        for (const ZoneWalk& zone : graph.Zones)
            most = std::max(most, zone.Snapshot.Polygons.size());
        NavQueryContextConfig config;
        config.MaxNodes = static_cast<std::uint32_t>(std::clamp<std::size_t>(most * 2, 2048, 65535));
        config.MaxCorridor = 4096;
        return std::make_unique<NavQueryContext>(config);
    }

    EditorOperationEntry Reachable(LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "world.reachable";
        entry.Definition.Category = "World";
        entry.Definition.Description =
            "Whether one point of a world reaches another, across zones, asked twice like nav.reachable. A zone "
            "is left only through a dock: by walking to its mouth, crossing it the ways it allows, and landing "
            "where it puts you, so a dock in a floor is a drop. A gated dock counts as shut in the sure walk unless "
            "assumed open. World links have no positions yet and are listed as unconsidered. Opens every zone.";
        entry.Definition.Parameters = DataField::Record({}, {}, {}, Parameters({
            DataField::Vector("from", {}, "world position"), DataField::Vector("to", {}, "world position") }));
        entry.Definition.Result = DataField::Record({}, {}, {}, {
            DataField::Enum("outcome", {}, {},
                            { { "reached", "Reached", "verified under the assumptions" },
                              { "depends_on", "Depends on", "only if unknown kinds or gates are open" },
                              { "unreached", "Unreached", "verified under the assumptions" },
                              { "incomplete", "Incomplete", "a search stopped before an answer" } }),
            DataField::ArrayOf("depends_on", {}, "unknown traversal kinds the possible walk crosses", DataField::Field({}, DataFieldKind::String, {}, {})),
            DataField::ArrayOf("depends_on_gates", {}, "gated docks the possible walk goes through", DataField::Field({}, DataFieldKind::String, {}, {})),
            DataField::ArrayOf("legs", {}, "the walk zone by zone",
                DataField::Record({}, {}, {}, { ZoneSchema("zone"),
                    DataField::ArrayOf("crossed", {}, "traversal kinds crossed", DataField::Field({}, DataFieldKind::String, {}, {})) }),
                true),
            DataField::ArrayOf("docks", {}, "the docks crossed, in order", DataField::Field({}, DataFieldKind::String, {}, {})),
            DataField::ArrayOf("unconsidered", {}, "world links, which have no positions", DataField::Field({}, DataFieldKind::String, {}, {})),
            AssumptionsSchema(),
            DataField::Field("profile", DataFieldKind::String, {}, {}),
        });
        entry.Execute = [&level](const JsonValue& arguments) {
            WorldDocument& world = level.LevelWorld();
            EditorOperationOutcome refusal;
            std::optional<WorldGraph> graph = BuildGraph(world, arguments, refusal);
            if (!graph)
                return refusal;
            Gates gates;
            if (std::optional<EditorOperationOutcome> refused = ReadGates(*graph, arguments, gates))
                return *refused;
            const std::unique_ptr<NavQueryContext> context = Context(*graph);
            const auto from = Locate(*graph, world, *context, PointFromJson(*arguments.Find("from")));
            const auto to = Locate(*graph, world, *context, PointFromJson(*arguments.Find("to")));
            if (!from || !to)
                return EditorOperationOutcome::Refused("nav.off_mesh", "a point is not near the walkable surface of a zone");

            const std::pair goal{ to->first, to->second.Ref };
            const Walked sure = Walk(*graph, *context, from->first, from->second, goal, gates, false);
            const Walked possible = sure.Goal ? sure : Walk(*graph, *context, from->first, from->second, goal, gates, true);
            const bool reached = sure.Goal.has_value();
            const Walked& shown = reached ? sure : possible;

            std::vector<std::size_t> docks;
            const std::vector<Leg> legs =
                shown.Goal ? Legs(*graph, *context, shown, to->second, !reached, docks) : std::vector<Leg>{};
            std::vector<std::string> dependsOn;
            std::vector<std::string> dependsOnGates;
            JsonValue::Array legsJson;
            for (const Leg& leg : legs)
            {
                for (const std::string& kind : leg.Crossed)
                    if (!reached && std::ranges::find(graph->Unknown, kind) != graph->Unknown.end()
                        && std::ranges::find(dependsOn, kind) == dependsOn.end())
                        dependsOn.push_back(kind);
                legsJson.push_back(JsonValue(JsonValue::Object{ { "zone", ZoneJson(world.Manifest(), leg.Zone) },
                                                                { "crossed", Names(leg.Crossed) } }));
            }
            std::vector<std::string> crossedDocks;
            for (const std::size_t c : docks)
            {
                crossedDocks.push_back(graph->Connections[c].Name);
                if (!reached && graph->Connections[c].Gated && !gates.Open.contains(graph->Connections[c].Id.Value))
                    dependsOnGates.push_back(graph->Connections[c].Name);
            }
            std::string outcome = reached ? "reached" : possible.Goal ? "depends_on" : "unreached";
            if (!shown.Goal && (!sure.Complete || !possible.Complete))
                outcome = "incomplete";
            JsonValue scenario = graph->Zones.empty() ? JsonValue(JsonValue::Object{
                                                            { "held", JsonValue(JsonValue::Array{}) },
                                                            { "denied", JsonValue(JsonValue::Array{}) },
                                                            { "unknown", JsonValue(JsonValue::Array{}) } })
                                                      : graph->Zones.front().Navigation->AssumptionsJson();
            if (JsonValue* unknown = scenario.Find("unknown"))
                *unknown = Names(graph->Unknown);
            return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                { "outcome", JsonValue(outcome) },
                { "depends_on", Names(dependsOn) },
                { "depends_on_gates", Names(dependsOnGates) },
                { "legs", JsonValue(std::move(legsJson)) },
                { "docks", Names(crossedDocks) },
                { "unconsidered", Names(graph->Unconsidered) },
                { "assumptions", std::move(scenario) },
                { "profile", JsonValue(graph->Profile) },
            }));
        };
        return entry;
    }

    EditorOperationEntry Bypasses(LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "world.bypasses";
        entry.Definition.Category = "World";
        entry.Definition.Description =
            "The gated docks the sure walk from a point gets to both sides of with every gate shut that is not "
            "assumed open: what the gate was meant to keep back is reachable some other way. Opens every zone.";
        entry.Definition.Parameters = DataField::Record({}, {}, {}, Parameters({ DataField::Vector("from", {}, "world position") }));
        entry.Definition.Result = DataField::Record({}, {}, {}, {
            DataField::ArrayOf("bypasses", {}, {},
                DataField::Record({}, {}, {}, {
                    DataField::Field("dock", DataFieldKind::String, {}, "the gated dock's id"),
                    ZoneSchema("zone_a"),
                    ZoneSchema("zone_b"),
                }),
                true),
            DataField::Field("complete", DataFieldKind::Bool, {}, "false when a search ran out of room"),
            DataField::ArrayOf("unconsidered", {}, "world links, which have no positions", DataField::Field({}, DataFieldKind::String, {}, {})),
        });
        entry.Execute = [&level](const JsonValue& arguments) {
            WorldDocument& world = level.LevelWorld();
            EditorOperationOutcome refusal;
            std::optional<WorldGraph> graph = BuildGraph(world, arguments, refusal);
            if (!graph)
                return refusal;
            Gates gates;
            if (std::optional<EditorOperationOutcome> refused = ReadGates(*graph, arguments, gates))
                return *refused;
            const std::unique_ptr<NavQueryContext> context = Context(*graph);
            const auto from = Locate(*graph, world, *context, PointFromJson(*arguments.Find("from")));
            if (!from)
                return EditorOperationOutcome::Refused("nav.off_mesh", "the point is not near the walkable surface of a zone");

            const Walked sure = Walk(*graph, *context, from->first, from->second, std::nullopt, gates, false);
            JsonValue::Array bypasses;
            for (const Connection& connection : graph->Connections)
            {
                if (!connection.Gated || gates.Open.contains(connection.Id.Value))
                    continue;
                bool bothSides = true;
                for (const ZoneId side : { connection.A, connection.B })
                {
                    const auto zone = std::ranges::find(graph->Zones, side, &ZoneWalk::Zone);
                    bothSides = bothSides && zone != graph->Zones.end()
                        && SideReached(*zone, sure.Reached[static_cast<std::size_t>(zone - graph->Zones.begin())], connection,
                                       *context);
                }
                if (bothSides)
                    bypasses.push_back(JsonValue(JsonValue::Object{
                        { "dock", JsonValue(connection.Name) },
                        { "zone_a", ZoneJson(world.Manifest(), connection.A) },
                        { "zone_b", ZoneJson(world.Manifest(), connection.B) },
                    }));
            }
            return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                { "bypasses", JsonValue(std::move(bypasses)) },
                { "complete", JsonValue(sure.Complete) },
                { "unconsidered", Names(graph->Unconsidered) },
            }));
        };
        return entry;
    }
}

std::vector<EditorOperationEntry> WorldReachabilityOperations(LevelTarget& level)
{
    std::vector<EditorOperationEntry> entries;
    entries.push_back(Reachable(level));
    entries.push_back(Bypasses(level));
    return entries;
}
