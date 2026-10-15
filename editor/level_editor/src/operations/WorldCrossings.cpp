#include "WorldCrossings.h"

#include "EntityRecords.h"
#include "LevelJson.h"
#include "LevelNavigation.h"
#include "LevelSubjects.h"
#include "LevelTarget.h"
#include "WorldFocus.h"

#include "document/EditorDocument.h"
#include "document/EditorEntityRecipe.h"
#include "document/EditorScene.h"
#include "document/EntityChangeSet.h"
#include "document/SpaceComponents.h"
#include "document/WorldDocument.h"
#include "document/WorldDockAuthoring.h"
#include "document/commands/RenameEntityCommand.h"

#include <core/metadata/DataSchemaFields.h>
#include <navigation/tooling/NavMeshSnapshot.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>
#include <zone/WorldConnectionComponents.h>
#include <zone/WorldPartitionManifest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <string>

namespace
{
    // A crossing narrower than this many agent radii cannot be walked through.
    constexpr float kNarrowestCrossing = 2.0f;
    // How far past a bound a point is taken to stand in the zone beyond.
    constexpr float kBeyond = 1.0f;
    constexpr float kDefaultDockHeight = 3.0f;

    // One vertical side of a zone's bounds.
    struct Side
    {
        std::string_view Name;
        Vec3d Normal;
        int Across;  // the axis the side's plane is constant on
        int Along;   // the horizontal axis that runs along it
        float Plane;
    };

    std::array<Side, 4> SidesOf(const Aabb3d& bounds)
    {
        return { Side{ "east", { 1, 0, 0 }, 0, 2, bounds.Max.X }, Side{ "west", { -1, 0, 0 }, 0, 2, bounds.Min.X },
                 Side{ "south", { 0, 0, 1 }, 2, 0, bounds.Max.Z }, Side{ "north", { 0, 0, -1 }, 2, 0, bounds.Min.Z } };
    }

    struct Interval
    {
        float From = 0.0f;
        float To = 0.0f;
        float Floor = 0.0f;
    };

    // Joins intervals that touch or come within `gap` of each other.
    std::vector<Interval> Merged(std::vector<Interval> intervals, float gap)
    {
        std::ranges::sort(intervals, {}, &Interval::From);
        std::vector<Interval> merged;
        for (const Interval& next : intervals)
            if (!merged.empty() && next.From <= merged.back().To + gap)
            {
                merged.back().To = std::max(merged.back().To, next.To);
                merged.back().Floor = std::min(merged.back().Floor, next.Floor);
            }
            else
                merged.push_back(next);
        return merged;
    }

    // What of `span` no interval of `covered` reaches.
    std::vector<Interval> Uncovered(const Interval& span, std::vector<Interval> covered)
    {
        std::ranges::sort(covered, {}, &Interval::From);
        std::vector<Interval> left;
        float at = span.From;
        for (const Interval& cover : covered)
        {
            if (cover.To <= at || cover.From >= span.To)
                continue;
            if (cover.From > at)
                left.push_back({ at, cover.From, span.Floor });
            at = std::max(at, cover.To);
        }
        if (at < span.To)
            left.push_back({ at, span.To, span.Floor });
        return left;
    }

    // The stretches of `side` the walkable border of `snapshot` lies along.
    std::vector<Interval> BorderAlong(const NavMeshSnapshot& snapshot, const Side& side, float tolerance)
    {
        std::vector<Interval> along;
        for (const NavSnapshotBorder& border : snapshot.Borders)
            if (std::abs(border.A[side.Across] - side.Plane) <= tolerance
                && std::abs(border.B[side.Across] - side.Plane) <= tolerance)
                along.push_back({ std::min(border.A[side.Along], border.B[side.Along]),
                                  std::max(border.A[side.Along], border.B[side.Along]), std::min(border.A.Y, border.B.Y) });
        return Merged(std::move(along), tolerance);
    }

    struct Walkable
    {
        std::unique_ptr<LevelNavigation> Navigation;
        NavMeshSnapshot Snapshot;
        float Tolerance = 0.0f;
        float Radius = 0.0f;
    };

    std::optional<Walkable> WalkableOf(const EditorDocument& zone, EditorOperationOutcome& refusal)
    {
        Walkable walkable;
        walkable.Navigation = BuildLevelNavigation(zone, JsonValue(JsonValue::Object{}), refusal);
        if (walkable.Navigation == nullptr)
            return std::nullopt;
        walkable.Snapshot = SnapshotNavMesh(walkable.Navigation->Navigation, walkable.Navigation->ProfileIndex);
        const NavBuildProfile& profile = walkable.Navigation->File.Profiles[walkable.Navigation->ProfileIndex].Build;
        walkable.Radius = profile.Radius;
        walkable.Tolerance = profile.Radius + 2.0f * profile.CellSize;
        return walkable;
    }

    // The along-side stretches docks between the two zones cover on `side`.
    std::vector<Interval> Docked(const EditorScene& worldScene, const Side& side, ZoneId zone, ZoneId neighbour,
                                 float tolerance)
    {
        std::vector<Interval> covered;
        const World& components = worldScene.GetRegistry().Components;
        for (const EntityId entity : worldScene.GetAllEntities())
        {
            const WorldDock* dock = components.TryGet<WorldDock>(entity);
            const Transform3f* frame = worldScene.TryGetWorldTransform(entity);
            if (dock == nullptr || frame == nullptr)
                continue;
            const bool joins = (dock->ZoneA == zone && dock->ZoneB == neighbour) || (dock->ZoneA == neighbour && dock->ZoneB == zone);
            const Vec3d normal = frame->Rotation.RotateVector(Vec3d{ 0, 0, 1 });
            if (!joins || std::abs(normal.Dot(side.Normal)) < 0.9f
                || std::abs(frame->Position[side.Across] - side.Plane) > tolerance + kBeyond)
                continue;
            const float halfWidth = dock->HalfExtents.X * std::abs(frame->Right()[side.Along]);
            covered.push_back({ frame->Position[side.Along] - halfWidth, frame->Position[side.Along] + halfWidth, 0.0f });
        }
        return covered;
    }

    std::optional<ZoneId> Beyond(const WorldPartitionManifest& manifest, const Vec3d& point, ZoneId zone)
    {
        const std::vector<ZoneId> candidates = WorldDockZoneCandidates(manifest, point, zone);
        return candidates.size() == 1 ? std::optional<ZoneId>(candidates.front()) : std::nullopt;
    }

    JsonValue ZoneJson(const WorldPartitionManifest& manifest, ZoneId zone)
    {
        std::string name;
        for (const ZoneHeader& header : manifest.Zones)
            if (header.Id == zone)
                name = header.Name;
        return JsonValue(JsonValue::Object{ { "id", JsonValue(ZoneIdToString(zone)) }, { "name", JsonValue(name) } });
    }

    DataFieldSchema ZoneSchema(std::string key)
    {
        return DataField::Record(std::move(key), {}, {}, {
            DataField::Field("id", DataFieldKind::String, {}, {}), DataField::Field("name", DataFieldKind::String, {}, {}) });
    }

    EditorOperationEntry Crossings(LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "world.crossings";
        entry.Definition.Category = "World";
        entry.Definition.Description =
            "Where a zone's walkable surface runs to its bounds with another zone beyond and no dock between the "
            "two covers it (spatial.crossing_without_dock). Each crossing gives the point, width and facing "
            "dock.place_at takes. A closed zone named is opened; 'confirmed' says whether the zone beyond, when "
            "open, walks on from there too.";
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            DataField::Field("zone", DataFieldKind::String, {}, "a zone's id or name; the focused zone when absent", false),
        });
        entry.Definition.Result = DataField::Record({}, {}, {}, {
            DataField::ArrayOf("crossings", {}, {},
                DataField::Record({}, {}, {}, {
                    DataField::Field("code", DataFieldKind::String, {}, "spatial.crossing_without_dock"),
                    ZoneSchema("zone"),
                    ZoneSchema("beyond"),
                    AxisDirectionField("facing", "the side of the zone it crosses, the way a dock there faces"),
                    DataField::Vector("at", {}, "the middle of the crossing, on the floor at the bound"),
                    DataField::Field("width", DataFieldKind::Float, {}, "metres along the bound"),
                    DataField::Optional("confirmed", {}, "null when the zone beyond is closed",
                                        DataField::Field({}, DataFieldKind::Bool, {}, {}), true),
                }),
                true),
        });
        entry.Execute = [&level](const JsonValue& arguments) {
            WorldDocument& world = level.LevelWorld();
            if (!world.IsWorld())
                return EditorOperationOutcome::Refused("world.none", "a single level is open, not a world");
            const WorldPartitionManifest& manifest = world.Manifest();
            ZoneId zone = world.IsWorldSceneFocused() ? ZoneId{} : world.FocusZone();
            if (const JsonValue* named = arguments.Find("zone"))
            {
                const std::optional<ZoneId> found = ZoneNamed(manifest, named->AsString());
                if (!found)
                    return EditorOperationOutcome::Refused("world.zone_unknown", "the world has no zone '" + named->AsString() + "'");
                zone = *found;
            }
            if (!zone.IsValid())
                return EditorOperationOutcome::Refused("world.zone_needed", "the world scene is focused; name a zone");
            if (world.ZoneDocument(zone) == nullptr)
                (void)world.LoadZone(zone);
            EditorDocument* document = world.ZoneDocument(zone);
            const ZoneHeader* header = nullptr;
            for (const ZoneHeader& candidate : manifest.Zones)
                if (candidate.Id == zone)
                    header = &candidate;
            if (document == nullptr || header == nullptr || !header->Bounds.IsValid())
                return EditorOperationOutcome::Refused("world.zone_closed", "the zone is not open, or has no bounds");

            EditorOperationOutcome refusal;
            std::optional<Walkable> walkable = WalkableOf(*document, refusal);
            if (!walkable)
                return refusal;
            std::map<std::uint64_t, std::optional<Walkable>> beyondWalkable;

            JsonValue::Array crossings;
            for (const Side& side : SidesOf(header->Bounds))
                for (const Interval& span : BorderAlong(walkable->Snapshot, side, walkable->Tolerance))
                {
                    Vec3d middle = side.Normal * side.Plane;
                    middle[side.Along] = 0.5f * (span.From + span.To);
                    middle.Y = span.Floor;
                    const std::optional<ZoneId> beyond =
                        Beyond(manifest, middle + side.Normal * kBeyond + Vec3d{ 0, 0.5f, 0 }, zone);
                    if (!beyond)
                        continue;
                    for (const Interval& open :
                         Uncovered(span, Docked(world.WorldSceneDocument().GetScene(), side, zone, *beyond, walkable->Tolerance)))
                    {
                        if (open.To - open.From < kNarrowestCrossing * walkable->Radius)
                            continue;
                        Vec3d at = middle;
                        at[side.Along] = 0.5f * (open.From + open.To);
                        JsonValue confirmed;
                        if (EditorDocument* other = world.ZoneDocument(*beyond))
                        {
                            auto& known = beyondWalkable[beyond->Value];
                            if (!known)
                            {
                                EditorOperationOutcome unused;
                                known = WalkableOf(*other, unused);
                            }
                            bool meets = false;
                            if (known)
                            {
                                const Side facing{ side.Name, side.Normal * -1.0f, side.Across, side.Along, side.Plane };
                                for (const Interval& theirs : BorderAlong(known->Snapshot, facing, known->Tolerance))
                                    meets = meets || (theirs.From < open.To && theirs.To > open.From);
                            }
                            confirmed = JsonValue(meets);
                        }
                        crossings.push_back(JsonValue(JsonValue::Object{
                            { "code", JsonValue("spatial.crossing_without_dock") },
                            { "zone", ZoneJson(manifest, zone) },
                            { "beyond", ZoneJson(manifest, *beyond) },
                            { "facing", JsonValue(std::string(side.Name)) },
                            { "at", PointJson(at) },
                            { "width", MetresJson(open.To - open.From) },
                            { "confirmed", std::move(confirmed) },
                        }));
                    }
                }
            return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{ { "crossings", JsonValue(std::move(crossings)) } }));
        };
        return entry;
    }

    struct DockFrame
    {
        Vec3d At;      // on the floor, in the middle
        Vec3d Facing;  // from the zone it leaves into the one it enters
        float Width = 0.0f;
        float Height = kDefaultDockHeight;
    };

    // An opening's frame, from whichever open zone holds it.
    std::optional<DockFrame> OpeningFrame(WorldDocument& world, std::string_view subject, EditorOperationOutcome& refusal)
    {
        std::optional<DockFrame> found;
        refusal = EditorOperationOutcome::Refused("subject.not_found", "no open zone holds that opening");
        world.VisitOpenZones([&](ZoneId, EditorDocument& document, auto&) {
            const SubjectResolution resolved = ResolveEntitySubject(document, subject);
            if (found || !resolved.Resolved())
                return;
            const EditorScene& scene = document.GetScene();
            const Opening* cut = scene.GetRegistry().Components.TryGet<Opening>(resolved.Entities.front());
            const Transform3f* frame = scene.TryGetWorldTransform(resolved.Entities.front());
            if (cut == nullptr || frame == nullptr)
            {
                refusal = EditorOperationOutcome::Refused("opening.not_opening", "it is not an opening");
                return;
            }
            found = DockFrame{ frame->Position, frame->Rotation.RotateVector(Vec3d{ 0, 0, 1 }), cut->Width, cut->Height };
        });
        return found;
    }

    EditorOperationEntry PlaceDock(LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "dock.place_at";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "World";
        entry.Definition.Description =
            "Adds a dock to the world scene fitted to an opening, or to a crossing world.crossings reported, as "
            "one undoable step. The dock keeps no tie to what it was fitted to. The world scene must be focused "
            "(world.focus target=world); a draft of it proposes the dock instead.";
        DataFieldSchema height = DataField::Field("height", DataFieldKind::Float, {}, "metres; the opening's, else 3", false);
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            DataField::Field("opening", DataFieldKind::String, {}, "an opening's id or '@<name>', in any open zone", false),
            DataField::Vector("at", {}, "the middle of the crossing on the floor, without an opening", 3, false),
            DataField::Field("width", DataFieldKind::Float, {}, "metres, without an opening", false),
            AxisDirectionField("facing", "the way out of the zone it leaves, without an opening"),
            std::move(height),
        });
        entry.Definition.Parameters.Children[3].Required = false;
        entry.Definition.Result = MutationResultSchema({ EntityNameSchema("dock", "the dock made") });
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            WorldDocument& world = level.LevelWorld();
            if (!world.IsWorld() || &level.FocusLevel() != &world.WorldSceneDocument())
                return EditorOperationOutcome::Refused("dock.needs_world_scene",
                                                       "docks live in the world scene; world.focus target=world first");
            std::optional<DockFrame> frame;
            EditorOperationOutcome refusal;
            if (const JsonValue* opening = arguments.Find("opening"))
            {
                frame = OpeningFrame(world, opening->AsString(), refusal);
                if (!frame)
                    return refusal;
            }
            else
            {
                const JsonValue* at = arguments.Find("at");
                const JsonValue* width = arguments.Find("width");
                const JsonValue* facing = arguments.Find("facing");
                if (at == nullptr || width == nullptr || facing == nullptr)
                    return EditorOperationOutcome::Refused("dock.frame_missing", "name an opening, or give at, width and facing");
                frame = DockFrame{ PointFromJson(*at), *AxisDirection(facing->AsString()),
                                   static_cast<float>(width->AsNumber()) };
            }
            if (const JsonValue* tall = arguments.Find("height"))
                frame->Height = static_cast<float>(tall->AsNumber());

            return level.Mutate(name, arguments, [&world, frame](EditorDocument& document, EntityChangeRecorder& recorder) {
                const Vec3d facing = frame->Facing.Normalized();
                const std::optional<ZoneId> leaving =
                    Beyond(world.Manifest(), frame->At - facing * kBeyond + Vec3d{ 0, 0.5f, 0 }, ZoneId{});
                if (!leaving)
                    return EditorOperationOutcome::Refused("dock.no_zone", "no one zone lies behind that point");
                EditorCreateContext context;
                context.World = &world;
                context.ActiveZone = *leaving;
                context.PlacementPoint = frame->At + Vec3d{ 0, 0.5f * frame->Height, 0 };
                context.PlacementNormal = facing;
                EditorScene& scene = document.GetScene();
                const EntityId dock = document.RestoreEntity(WorldDockRecipe{}.Build(context));
                recorder.Created(dock);
                WorldDock* placed = scene.GetRegistry().Components.TryGet<WorldDock>(dock);
                if (placed == nullptr || !placed->ZoneB.IsValid())
                    return EditorOperationOutcome::Refused("dock.no_zone", "no one zone lies beyond that point");
                placed->HalfExtents = Vec2d{ 0.5f * frame->Width, 0.5f * frame->Height };
                if (auto rename = MakeRenameEntityCommand(dock, scene.NextEntityName("Dock"), scene, document))
                    rename->Execute();
                recorder.Created(dock);
                document.MarkDirty();
                return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{ { "dock", EntityNameJson(document, dock) } }));
            });
        };
        return entry;
    }
}

std::vector<EditorOperationEntry> WorldCrossingOperations(LevelTarget& level)
{
    std::vector<EditorOperationEntry> entries;
    entries.push_back(Crossings(level));
    entries.push_back(PlaceDock(level));
    return entries;
}
