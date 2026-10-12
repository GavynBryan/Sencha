#include "LevelQueries.h"

#include "FloorPlan.h"
#include "LevelJson.h"
#include "LevelReachability.h"
#include "LevelSubjects.h"

#include "brush/BrushMeshStore.h"
#include "document/BrushComponents.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/WorldDocument.h"

#include <assets/texture/PngEncoding.h>
#include <core/io/FileBytes.h>
#include <core/json/JsonStringify.h>
#include <core/metadata/DataSchemaFields.h>
#include <math/geometry/3d/Aabb3d.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>

namespace
{
    // Schema -----------------------------------------------------------------

    DataFieldSchema Point(std::string key, std::string summary)
    {
        return DataField::Vector(std::move(key), {}, std::move(summary));
    }

    DataFieldSchema Bounds(std::string key)
    {
        return DataField::Optional(std::move(key), {}, {}, Point({}, {}), true);
    }

    DataFieldSchema Limit(std::int64_t fallback)
    {
        DataFieldSchema field = DataField::Field("limit", DataFieldKind::Int, {}, "most entities to list");
        field.Default = fallback;
        field.Numeric.Minimum = 0.0;
        return field;
    }

    DataFieldSchema KindField()
    {
        return DataField::Enum("kind", {}, "how the entity is represented",
                    { { "brush", "Brush", "level geometry" }, { "entity", "Entity", "anything else" } });
    }

    // One line per entity: what every listing answers with.
    DataFieldSchema EntityRecord(std::string key = {})
    {
        return DataField::Record(std::move(key), {}, {}, {
            DataField::Field("id", DataFieldKind::Entity, {}, "persistent id"),
            DataField::Field("name", DataFieldKind::String, {}, "authored name"),
            KindField(),
            Bounds("position"),
            Bounds("min"),
            Bounds("max"),
        });
    }

    DataFieldSchema Root(std::vector<DataFieldSchema> members)
    {
        DataFieldSchema root = AuthoredRecordRoot();
        root.Children = std::move(members);
        return root;
    }

    // Values -----------------------------------------------------------------

    std::size_t LimitFrom(const JsonValue& arguments)
    {
        return static_cast<std::size_t>(arguments.Find("limit")->AsNumber());
    }

    // Where an entity is for spatial questions: its bounds, or a point at its
    // position when it has a place but no extent (a spawn, a light, a camera).
    std::optional<Aabb3d> SpatialExtent(const EditorScene& scene, EntityId entity)
    {
        if (std::optional<Aabb3d> bounds = scene.EvaluatedWorldBounds(entity))
            return bounds;
        if (const Transform3f* world = scene.TryGetWorldTransform(entity))
            return Aabb3d(world->Position, world->Position);
        return std::nullopt;
    }

    JsonValue EntityJson(const EditorDocument& document, EntityId entity)
    {
        const std::optional<Aabb3d> bounds = document.GetScene().EvaluatedWorldBounds(entity);
        const Transform3f* world = document.GetScene().TryGetWorldTransform(entity);
        return JsonValue(JsonValue::Object{
            { "id", JsonValue(PersistentIdText(document, entity)) },
            { "name", JsonValue(EntityNameText(document, entity)) },
            { "kind", JsonValue(document.GetScene().TryGetBrush(entity) != nullptr ? "brush"
                                                                                  : "entity") },
            { "position", world != nullptr ? PointJson(world->Position) : JsonValue{} },
            { "min", bounds ? PointJson(bounds->Min) : JsonValue{} },
            { "max", bounds ? PointJson(bounds->Max) : JsonValue{} },
        });
    }

    float DistanceToBounds(const Vec3d& point, const Aabb3d& bounds)
    {
        const float dx = std::max({ bounds.Min.X - point.X, 0.0f, point.X - bounds.Max.X });
        const float dy = std::max({ bounds.Min.Y - point.Y, 0.0f, point.Y - bounds.Max.Y });
        const float dz = std::max({ bounds.Min.Z - point.Z, 0.0f, point.Z - bounds.Max.Z });
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // Eight-way compass on the ground plane; north is -Z and east is +X.
    std::string_view Bearing(const Vec3d& from, const Vec3d& to)
    {
        const float east = to.X - from.X;
        const float north = from.Z - to.Z;
        if (east * east + north * north < 1.0e-6f)
            return "none";
        static constexpr std::array<std::string_view, 8> kWords = { "n", "ne", "e", "se",
                                                                    "s", "sw", "w", "nw" };
        const double turns = std::atan2(east, north) / (2.0 * std::numbers::pi) + 1.0 / 16.0;
        const double wrapped = turns - std::floor(turns);
        return kWords[static_cast<std::size_t>(wrapped * 8.0) % 8];
    }

    DataFieldSchema BearingField()
    {
        std::vector<DataEnumChoice> words;
        for (std::string_view word : { "n", "ne", "e", "se", "s", "sw", "w", "nw", "none" })
            words.push_back({ std::string(word), std::string(word), {} });
        return DataField::Enum("bearing", {}, "compass direction from the point, north is -Z", std::move(words));
    }

    EditorOperationOutcome Refusal(const SubjectResolution& subject)
    {
        JsonValue detail;
        if (!subject.Candidates.empty())
        {
            JsonValue::Array candidates;
            for (const std::string& id : subject.Candidates)
                candidates.push_back(JsonValue(id));
            detail = JsonValue(JsonValue::Object{ { "candidates", JsonValue(std::move(candidates)) } });
        }
        return EditorOperationOutcome::Refused(subject.Code, subject.Message, std::move(detail));
    }

    // Operations ---------------------------------------------------------------

    EditorOperationEntry Summary(const WorldDocument& world)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "level.summary";
        entry.Definition.Category = "Level";
        entry.Definition.Description =
            "Counts, overall bounds and one line per entity of the focus level.";
        entry.Definition.Parameters = Root({ Limit(200) });
        entry.Definition.Result = Root({
            DataField::Field("document", DataFieldKind::String, {}, "the level's asset path"),
            DataField::Field("entity_count", DataFieldKind::Int, {}, {}),
            DataField::Field("brush_count", DataFieldKind::Int, {}, {}),
            Bounds("min"),
            Bounds("max"),
            DataField::ArrayOf("entities", {}, {}, EntityRecord(), true),
            DataField::Field("truncated", DataFieldKind::Bool, {}, "more entities exist than were listed"),
        });
        entry.Execute = [&world](const JsonValue& arguments) {
            const EditorDocument& document = world.FocusDocument();
            const EditorScene& scene = document.GetScene();
            const std::size_t limit = LimitFrom(arguments);
            std::optional<Aabb3d> overall;
            std::int64_t brushes = 0;
            JsonValue::Array entities;
            for (const EntityId entity : scene.GetAllEntities())
            {
                if (scene.TryGetBrush(entity) != nullptr)
                    ++brushes;
                if (const std::optional<Aabb3d> bounds = SpatialExtent(scene, entity))
                    overall = overall ? overall->ExpandedToInclude(*bounds) : *bounds;
                if (entities.size() < limit)
                    entities.push_back(EntityJson(document, entity));
            }
            const std::size_t count = scene.GetAllEntities().size();
            return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                { "document", JsonValue(document.SourceAssetPath()) },
                { "entity_count", JsonValue(static_cast<double>(count)) },
                { "brush_count", JsonValue(static_cast<double>(brushes)) },
                { "min", overall ? PointJson(overall->Min) : JsonValue{} },
                { "max", overall ? PointJson(overall->Max) : JsonValue{} },
                { "entities", JsonValue(std::move(entities)) },
                { "truncated", JsonValue(count > limit) },
            }));
        };
        return entry;
    }

    EditorOperationEntry Describe(const WorldDocument& world)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "entity.describe";
        entry.Definition.Category = "Level";
        entry.Definition.Description =
            "One entity in full: its components as JSON, its parent and children, and its "
            "brush's size. The subject is an entity id or '@<name>'.";
        entry.Definition.Parameters =
            Root({ DataField::Field("subject", DataFieldKind::String, {}, "an entity id or '@<name>'") });
        const DataFieldSchema id = DataField::Field({}, DataFieldKind::Entity, {}, {});
        entry.Definition.Result = Root({
            EntityRecord("entity"),
            DataField::Optional("parent", {}, {}, id, true),
            DataField::ArrayOf("children", {}, {}, id, true),
            DataField::ArrayOf("components", {}, "each component's value as JSON text",
                    DataField::Record({}, {}, {}, { DataField::Field("key", DataFieldKind::String, {}, {}),
                                         DataField::Field("json", DataFieldKind::String, {}, {}) }),
                    true),
            DataField::Optional("brush", {}, {},
                     DataField::Record({}, {}, {}, { DataField::Field("vertices", DataFieldKind::Int, {}, {}),
                                          DataField::Field("faces", DataFieldKind::Int, {}, {}) }),
                     true),
        });
        entry.Execute = [&world](const JsonValue& arguments) {
            const EditorDocument& document = world.FocusDocument();
            const SubjectResolution subject =
                ResolveEntitySubject(document, arguments.Find("subject")->AsString());
            if (!subject.Resolved())
                return Refusal(subject);
            const EntityId entity = subject.Entities.front();
            const ::World& components = document.GetRegistry().Components;

            JsonValue parent;
            if (const auto* link = components.TryGet<Parent>(entity))
                parent = JsonValue(PersistentIdText(document, link->Entity));
            JsonValue::Array children;
            for (const EntityId other : document.GetScene().GetAllEntities())
            {
                const auto* link = components.TryGet<Parent>(other);
                if (link != nullptr && link->Entity == entity)
                    children.push_back(JsonValue(PersistentIdText(document, other)));
            }

            JsonValue::Array serialized;
            const EntitySnapshot snapshot = document.CaptureEntity(entity);
            if (snapshot.Components.IsObject())
                for (const auto& [key, value] : snapshot.Components.AsObject())
                    serialized.push_back(JsonValue(JsonValue::Object{
                        { "key", JsonValue(key) }, { "json", JsonValue(JsonStringify(value)) } }));

            JsonValue brush;
            if (snapshot.Brush)
                brush = JsonValue(JsonValue::Object{
                    { "vertices", JsonValue(static_cast<double>(snapshot.Brush->Mesh.Vertices.size())) },
                    { "faces", JsonValue(static_cast<double>(snapshot.Brush->Mesh.Faces.size())) },
                });

            return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                { "entity", EntityJson(document, entity) },
                { "parent", std::move(parent) },
                { "children", JsonValue(std::move(children)) },
                { "components", JsonValue(std::move(serialized)) },
                { "brush", std::move(brush) },
            }));
        };
        return entry;
    }

    EditorOperationEntry Box(const WorldDocument& world)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "query.box";
        entry.Definition.Category = "Level";
        entry.Definition.Description = "Entities whose bounds touch an axis-aligned box.";
        entry.Definition.Parameters =
            Root({ Point("min", "one corner"), Point("max", "the opposite corner"), Limit(200) });
        entry.Definition.Result = Root({ DataField::ArrayOf("entities", {}, {}, EntityRecord(), true),
                                         DataField::Field("truncated", DataFieldKind::Bool, {}, {}) });
        entry.Execute = [&world](const JsonValue& arguments) {
            const EditorDocument& document = world.FocusDocument();
            const Vec3d low = PointFromJson(*arguments.Find("min"));
            const Vec3d high = PointFromJson(*arguments.Find("max"));
            const Aabb3d box{ Vec3d{ std::min(low.X, high.X), std::min(low.Y, high.Y),
                                     std::min(low.Z, high.Z) },
                              Vec3d{ std::max(low.X, high.X), std::max(low.Y, high.Y),
                                     std::max(low.Z, high.Z) } };
            const std::size_t limit = LimitFrom(arguments);
            JsonValue::Array entities;
            bool truncated = false;
            for (const EntityId entity : document.GetScene().GetAllEntities())
            {
                const std::optional<Aabb3d> bounds = SpatialExtent(document.GetScene(), entity);
                if (!bounds || !bounds->Intersects(box))
                    continue;
                if (entities.size() == limit)
                {
                    truncated = true;
                    break;
                }
                entities.push_back(EntityJson(document, entity));
            }
            return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                { "entities", JsonValue(std::move(entities)) },
                { "truncated", JsonValue(truncated) },
            }));
        };
        return entry;
    }

    EditorOperationEntry Near(const WorldDocument& world)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "query.near";
        entry.Definition.Category = "Level";
        entry.Definition.Description =
            "Entities within a radius of a point, nearest first, with distance to their bounds "
            "and compass bearing.";
        DataFieldSchema radius = DataField::Field("radius", DataFieldKind::Float, {}, "metres");
        radius.Default = 8.0;
        radius.Numeric.Minimum = 0.0;
        entry.Definition.Parameters = Root({ Point("point", "where to look from"), radius, Limit(50) });
        entry.Definition.Result = Root({
            DataField::ArrayOf("entities", {}, {},
                    DataField::Record({}, {}, {}, { EntityRecord("entity"),
                                         DataField::Field("distance", DataFieldKind::Float, {}, "metres to the bounds"),
                                         BearingField() }),
                    true),
            DataField::Field("truncated", DataFieldKind::Bool, {}, {}),
        });
        entry.Execute = [&world](const JsonValue& arguments) {
            const EditorDocument& document = world.FocusDocument();
            const Vec3d point = PointFromJson(*arguments.Find("point"));
            const float reach = static_cast<float>(arguments.Find("radius")->AsNumber());
            const std::size_t limit = LimitFrom(arguments);

            struct Hit
            {
                EntityId Entity;
                float Distance;
                Vec3d Center;
            };
            std::vector<Hit> hits;
            for (const EntityId entity : document.GetScene().GetAllEntities())
            {
                const std::optional<Aabb3d> bounds = SpatialExtent(document.GetScene(), entity);
                if (!bounds)
                    continue;
                const float distance = DistanceToBounds(point, *bounds);
                if (distance <= reach)
                    hits.push_back({ entity, distance, bounds->Center() });
            }
            // Ties broken by id so the answer is the same on every run.
            std::ranges::sort(hits, [&document](const Hit& a, const Hit& b) {
                if (a.Distance != b.Distance)
                    return a.Distance < b.Distance;
                return PersistentIdText(document, a.Entity) < PersistentIdText(document, b.Entity);
            });

            JsonValue::Array entities;
            for (std::size_t index = 0; index < hits.size() && index < limit; ++index)
                entities.push_back(JsonValue(JsonValue::Object{
                    { "entity", EntityJson(document, hits[index].Entity) },
                    { "distance", MetresJson(hits[index].Distance) },
                    { "bearing", JsonValue(std::string(Bearing(point, hits[index].Center))) },
                }));
            return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                { "entities", JsonValue(std::move(entities)) },
                { "truncated", JsonValue(hits.size() > limit) },
            }));
        };
        return entry;
    }

    EditorOperationEntry PlanSection(const WorldDocument& world)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "plan.section";
        entry.Definition.Category = "Level";
        entry.Definition.Description =
            "A floor plan: every brush cut by a horizontal plane, as outlines of [x, z] points "
            "(north is -z), plus the entities with a place but no extent. Optionally written as a "
            "north-up PNG with a metre grid. Cut off floor and ceiling planes, e.g. at eye height.";
        DataFieldSchema height = DataField::Field("height", DataFieldKind::Float, {},
                                                  "world height of the cut, metres");
        height.Default = 1.5;
        DataFieldSchema scale = DataField::Field("pixels_per_metre", DataFieldKind::Float, {}, {});
        scale.Default = 16.0;
        scale.Numeric.Minimum = 1.0;
        scale.Numeric.Maximum = 256.0;
        DataFieldSchema side = DataField::Field("max_side", DataFieldKind::Int, {},
                                                "longest image side in pixels");
        side.Default = std::int64_t{ 2048 };
        side.Numeric.Minimum = 64.0;
        side.Numeric.Maximum = 8192.0;
        entry.Definition.Parameters = Root({
            height,
            DataField::Optional("image", {}, "a path to write the plan to as PNG",
                                DataField::Field({}, DataFieldKind::String, {}, {})),
            scale,
            side,
        });

        const DataFieldSchema ground = DataField::Vector({}, {}, "[x, z]", 2);
        const DataFieldSchema identity = DataField::Record({}, {}, {}, {
            DataField::Field("id", DataFieldKind::Entity, {}, {}),
            DataField::Field("name", DataFieldKind::String, {}, {}),
        });
        DataFieldSchema brush = identity;
        brush.Children.push_back(DataField::ArrayOf(
            "contours", {}, {},
            DataField::Record({}, {}, {}, {
                DataField::Field("closed", DataFieldKind::Bool, {}, {}),
                DataField::ArrayOf("points", {}, {}, ground, true),
            }),
            true));
        DataFieldSchema point = identity;
        point.Children.push_back(DataField::Vector("position", {}, "[x, z]", 2));
        entry.Definition.Result = Root({
            DataField::Field("height", DataFieldKind::Float, {}, {}),
            DataField::ArrayOf("brushes", {}, {}, brush, true),
            DataField::ArrayOf("points", {}, {}, point, true),
            DataField::Optional("min", {}, {}, ground, true),
            DataField::Optional("max", {}, {}, ground, true),
            DataField::Optional("image", {}, {},
                DataField::Record({}, {}, {}, {
                    DataField::Field("path", DataFieldKind::String, {}, {}),
                    DataField::Field("width", DataFieldKind::Int, {}, {}),
                    DataField::Field("height", DataFieldKind::Int, {}, {}),
                    DataField::Vector("origin", {}, "[x, z] of the top-left corner", 2),
                    DataField::Field("pixels_per_metre", DataFieldKind::Float, {}, {}),
                }),
                true),
        });

        entry.Execute = [&world](const JsonValue& arguments) {
            const EditorDocument& document = world.FocusDocument();
            const float cut = static_cast<float>(arguments.Find("height")->AsNumber());
            const FloorPlan plan = SectionLevel(document, cut);

            JsonValue::Array brushes;
            for (const FloorPlan::Brush& cutBrush : plan.Brushes)
            {
                JsonValue::Array contours;
                for (const BrushSectionContour& contour : cutBrush.Contours)
                {
                    JsonValue::Array points;
                    for (const Vec2d& p : contour.Points)
                        points.push_back(GroundJson(p));
                    contours.push_back(JsonValue(JsonValue::Object{
                        { "closed", JsonValue(contour.Closed) },
                        { "points", JsonValue(std::move(points)) },
                    }));
                }
                brushes.push_back(JsonValue(JsonValue::Object{
                    { "id", JsonValue(PersistentIdText(document, cutBrush.Entity)) },
                    { "name", JsonValue(EntityNameText(document, cutBrush.Entity)) },
                    { "contours", JsonValue(std::move(contours)) },
                }));
            }
            JsonValue::Array points;
            for (const FloorPlan::Point& p : plan.Points)
                points.push_back(JsonValue(JsonValue::Object{
                    { "id", JsonValue(PersistentIdText(document, p.Entity)) },
                    { "name", JsonValue(EntityNameText(document, p.Entity)) },
                    { "position", GroundJson(p.Position) },
                }));

            JsonValue image;
            const JsonValue* imagePath = arguments.Find("image");
            if (imagePath != nullptr && imagePath->IsString())
            {
                const std::optional<FloorPlanImage> raster = RasterizeFloorPlan(
                    plan, static_cast<float>(arguments.Find("pixels_per_metre")->AsNumber()),
                    static_cast<std::uint32_t>(arguments.Find("max_side")->AsNumber()));
                if (raster)
                {
                    const std::vector<std::byte> png =
                        EncodePngRgba8(raster->Width, raster->Height, raster->Rgba);
                    if (png.empty() || !WriteFileBytes(imagePath->AsString(), png))
                        return EditorOperationOutcome::Refused(
                            "plan.image_not_written",
                            "the plan could not be written to " + imagePath->AsString());
                    image = JsonValue(JsonValue::Object{
                        { "path", JsonValue(imagePath->AsString()) },
                        { "width", JsonValue(static_cast<double>(raster->Width)) },
                        { "height", JsonValue(static_cast<double>(raster->Height)) },
                        { "origin", GroundJson(raster->Origin) },
                        { "pixels_per_metre", MetresJson(raster->Scale) },
                    });
                }
            }

            return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                { "height", MetresJson(cut) },
                { "brushes", JsonValue(std::move(brushes)) },
                { "points", JsonValue(std::move(points)) },
                { "min", plan.Empty ? JsonValue{} : GroundJson(plan.Min) },
                { "max", plan.Empty ? JsonValue{} : GroundJson(plan.Max) },
                { "image", std::move(image) },
            }));
        };
        return entry;
    }
}

std::vector<EditorOperationEntry> LevelQueryOperations(const WorldDocument& world)
{
    std::vector<EditorOperationEntry> entries;
    entries.push_back(Summary(world));
    entries.push_back(Describe(world));
    entries.push_back(Box(world));
    entries.push_back(Near(world));
    entries.push_back(PlanSection(world));
    entries.push_back(ReachabilityOperation(world));
    return entries;
}
