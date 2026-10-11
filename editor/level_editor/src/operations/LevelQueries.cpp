#include "LevelQueries.h"

#include "LevelSubjects.h"

#include "brush/BrushMeshStore.h"
#include "document/BrushComponents.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/WorldDocument.h"

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

    JsonValue VectorJson(const Vec3d& v)
    {
        return JsonValue(JsonValue::Array{ JsonValue(static_cast<double>(v.X)),
                                           JsonValue(static_cast<double>(v.Y)),
                                           JsonValue(static_cast<double>(v.Z)) });
    }

    Vec3d VectorFrom(const JsonValue& value)
    {
        const JsonValue::Array& a = value.AsArray();
        return Vec3d{ static_cast<float>(a[0].AsNumber()), static_cast<float>(a[1].AsNumber()),
                      static_cast<float>(a[2].AsNumber()) };
    }

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
            { "position", world != nullptr ? VectorJson(world->Position) : JsonValue{} },
            { "min", bounds ? VectorJson(bounds->Min) : JsonValue{} },
            { "max", bounds ? VectorJson(bounds->Max) : JsonValue{} },
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
                { "min", overall ? VectorJson(overall->Min) : JsonValue{} },
                { "max", overall ? VectorJson(overall->Max) : JsonValue{} },
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
            const Vec3d low = VectorFrom(*arguments.Find("min"));
            const Vec3d high = VectorFrom(*arguments.Find("max"));
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
            const Vec3d point = VectorFrom(*arguments.Find("point"));
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
                    { "distance", JsonValue(static_cast<double>(hits[index].Distance)) },
                    { "bearing", JsonValue(std::string(Bearing(point, hits[index].Center))) },
                }));
            return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                { "entities", JsonValue(std::move(entities)) },
                { "truncated", JsonValue(hits.size() > limit) },
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
    return entries;
}
