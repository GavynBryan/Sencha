#include "SpaceGraph.h"

#include "EntityRecords.h"
#include "LevelNavigation.h"
#include "LevelTarget.h"
#include "SpaceRelations.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/SpaceComponents.h"

#include <core/metadata/DataSchemaFields.h>
#include <navigation/NavQueryContext.h>
#include <navigation/NavigationQuery.h>
#include <navigation/tooling/NavMeshSnapshot.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <vector>

namespace
{
    // Farther and costlier than any level: a flood bounded only by the mesh.
    constexpr float kEverywhere = 1.0e9f;
    // How far either side of an opening's wall the crossing is measured from.
    constexpr float kCrossingReach = 1.0f;
    // A polygon lies on its floor; a point this far above it is in the air the floor belongs to.
    constexpr float kAboveFloor = 0.1f;

    struct Graph
    {
        std::vector<EntityId> Spaces;
        // Each region's space, by polygon reference.
        std::unordered_map<std::uint64_t, std::size_t> SpaceOf;
        // A polygon in each space to start from, if any.
        std::vector<std::optional<NavLocation>> Seeds;
    };

    Graph Classify(const EditorScene& scene, const LevelNavigation& level, const NavMeshSnapshot& snapshot)
    {
        Graph graph;
        const World& components = scene.GetRegistry().Components;
        for (const EntityId entity : scene.GetAllEntities())
            if (components.HasComponent<Space>(entity) && scene.RoleOf(entity) == BrushRole::Volume)
                graph.Spaces.push_back(entity);
        graph.Seeds.resize(graph.Spaces.size());
        for (const NavSnapshotPolygon& polygon : snapshot.Polygons)
        {
            Vec3d centre = Vec3d::Zero();
            for (std::uint32_t v = 0; v < polygon.VertexCount; ++v)
                centre = centre + snapshot.Vertices[polygon.FirstVertex + v];
            centre = centre * (1.0f / static_cast<float>(polygon.VertexCount));
            const std::optional<EntityId> space = SpaceContaining(scene, centre + Vec3d{ 0, kAboveFloor, 0 });
            if (!space)
                continue;
            const std::size_t index =
                static_cast<std::size_t>(std::ranges::find(graph.Spaces, *space) - graph.Spaces.begin());
            graph.SpaceOf.emplace(polygon.Region.Ref, index);
            if (!graph.Seeds[index])
                graph.Seeds[index] = NavLocation{ level.Zone, polygon.Region.Generation, polygon.Region.Profile, 0,
                                                  polygon.Region.Ref, centre };
        }
        return graph;
    }

    // The spaces a flood from `seed` reaches; nullopt when the flood stopped short.
    std::optional<std::vector<bool>> Reached(const LevelNavigation& level, const Graph& graph, NavQueryContext& context,
                              std::span<const GameplayTagId> capabilities, const NavLocation& seed,
                              std::size_t polygons)
    {
        std::vector<bool> reached(graph.Spaces.size(), false);
        NavReachableBuffer regions(polygons);
        if (NavCollectReachable(level.Navigation, context, level.Request(capabilities), seed, kEverywhere, kEverywhere,
                                regions)
            != NavStatus::Success)
            return std::nullopt;
        for (const NavReachableRegion& region : regions.Regions())
            if (const auto space = graph.SpaceOf.find(region.Region.Ref); space != graph.SpaceOf.end())
                reached[space->second] = true;
        return reached;
    }

    // Whether walkable surface runs through the opening from one side to the other.
    bool Crosses(const LevelNavigation& level, NavQueryContext& context, const Transform3f& frame)
    {
        const Vec3d through = frame.Rotation.RotateVector(Vec3d{ 0, 0, 1 });
        const NavQueryRequest request = level.Request(level.Possible);
        const NavProjectResult start =
            NavProjectPoint(level.Navigation, context, request, frame.Position - through * kCrossingReach, Vec3d{ 0.5f, 1.0f, 0.5f });
        if (start.Status != NavStatus::Success)
            return false;
        const NavRaycastResult ray =
            NavRaycast(level.Navigation, context, request, start.Location, frame.Position + through * kCrossingReach);
        return ray.Status == NavStatus::Success && !ray.Hit;
    }
}

EditorOperationEntry SpaceGraphOperation(const LevelTarget& level)
{
    EditorOperationEntry entry;
    entry.Definition.Name = "space.graph";
    entry.Definition.Category = "Space";
    entry.Definition.Description =
        "Which spaces the walkable surface joins, measured on the level's navigation under a scenario: a "
        "connection is reached when the sure walk makes it and depends_on when only the possible walk does. "
        "Also whether walkable surface runs through each opening, and which spaces have none.";
    entry.Definition.Parameters = DataField::Record({}, {}, {}, ScenarioParameters());
    const DataFieldSchema named = EntityNameSchema({}, {});
    entry.Definition.Result = DataField::Record({}, {}, {}, {
        DataField::ArrayOf("spaces", {}, {},
            DataField::Record({}, {}, {}, { EntityNameSchema("space", {}),
                DataField::Field("walkable", DataFieldKind::Bool, {}, "any walkable surface lies in it") }),
            true),
        DataField::ArrayOf("connections", {}, "from one space to another, either way when both are listed",
            DataField::Record({}, {}, {}, {
                EntityNameSchema("from", {}),
                EntityNameSchema("to", {}),
                DataField::Enum("outcome", {}, {},
                    { { "reached", "Reached", "the sure walk makes it" },
                      { "depends_on", "Depends on", "only with unknown traversal kinds open" } }),
            }),
            true),
        DataField::ArrayOf("openings", {}, {},
            DataField::Record({}, {}, {}, { EntityNameSchema("opening", {}),
                DataField::Field("crossed", DataFieldKind::Bool, {}, "walkable surface runs through it") }),
            true),
        DataField::Field("complete", DataFieldKind::Bool, {}, "false when a flood ran out of search room; some connections may be missing"),
        AssumptionsSchema(),
        DataField::Field("profile", DataFieldKind::String, {}, {}),
    });
    entry.Execute = [&level](const JsonValue& arguments) {
        EditorOperationOutcome refusal;
        const std::unique_ptr<LevelNavigation> navigation = BuildLevelNavigation(level.Document(), arguments, refusal);
        if (navigation == nullptr)
            return refusal;
        const EditorDocument& document = level.Document();
        const EditorScene& scene = document.GetScene();
        const NavMeshSnapshot snapshot = SnapshotNavMesh(navigation->Navigation, navigation->ProfileIndex);
        const Graph graph = Classify(scene, *navigation, snapshot);

        NavQueryContextConfig config;
        config.MaxNodes = static_cast<std::uint32_t>(std::clamp<std::size_t>(snapshot.Polygons.size() * 2, 1024, 65535));
        NavQueryContext context(config);

        JsonValue::Array spaces;
        JsonValue::Array connections;
        bool complete = true;
        for (std::size_t from = 0; from < graph.Spaces.size(); ++from)
        {
            spaces.push_back(JsonValue(JsonValue::Object{
                { "space", EntityNameJson(document, graph.Spaces[from]) },
                { "walkable", JsonValue(graph.Seeds[from].has_value()) },
            }));
            if (!graph.Seeds[from])
                continue;
            const std::optional<std::vector<bool>> sure =
                Reached(*navigation, graph, context, navigation->Sure, *graph.Seeds[from], snapshot.Polygons.size());
            const std::optional<std::vector<bool>> possible =
                Reached(*navigation, graph, context, navigation->Possible, *graph.Seeds[from], snapshot.Polygons.size());
            if (!sure || !possible)
            {
                complete = false;
                continue;
            }
            for (std::size_t to = 0; to < graph.Spaces.size(); ++to)
                if (to != from && (*possible)[to])
                    connections.push_back(JsonValue(JsonValue::Object{
                        { "from", EntityNameJson(document, graph.Spaces[from]) },
                        { "to", EntityNameJson(document, graph.Spaces[to]) },
                        { "outcome", JsonValue((*sure)[to] ? "reached" : "depends_on") },
                    }));
        }

        JsonValue::Array openings;
        const World& components = scene.GetRegistry().Components;
        for (const EntityId entity : scene.GetAllEntities())
            if (components.HasComponent<Opening>(entity))
                if (const Transform3f* frame = scene.TryGetWorldTransform(entity))
                    openings.push_back(JsonValue(JsonValue::Object{
                        { "opening", EntityNameJson(document, entity) },
                        { "crossed", JsonValue(Crosses(*navigation, context, *frame)) },
                    }));

        return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
            { "spaces", JsonValue(std::move(spaces)) },
            { "connections", JsonValue(std::move(connections)) },
            { "openings", JsonValue(std::move(openings)) },
            { "complete", JsonValue(complete) },
            { "assumptions", navigation->AssumptionsJson() },
            { "profile", JsonValue(navigation->Profile) },
        }));
    };
    return entry;
}
