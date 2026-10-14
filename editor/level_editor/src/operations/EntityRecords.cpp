#include "EntityRecords.h"

#include "LevelJson.h"
#include "LevelSubjects.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/SpaceComponents.h"
#include "selection/SelectableRef.h"

#include <core/metadata/DataSchemaFields.h>
#include <math/geometry/3d/Aabb3d.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>

#include <optional>

namespace
{
    DataFieldSchema OptionalPoint(std::string key)
    {
        return DataField::Optional(std::move(key), {}, {}, DataField::Vector({}, {}, {}), true);
    }

    DataFieldSchema KindField()
    {
        return DataField::Enum("kind", {}, "how the entity is represented",
                    { { "brush", "Brush", "solid level geometry" },
                      { "space", "Space", "a named region of air: a volume brush tagged space" },
                      { "volume", "Volume", "a brush enclosing air, not geometry" },
                      { "opening", "Opening", "a cut through generated walls" },
                      { "entity", "Entity", "anything else" } });
    }

    std::string_view ElementName(const SelectableRef& ref)
    {
        if (ref.IsFace())
            return "face";
        if (ref.IsEdge())
            return "edge";
        if (ref.IsVertex())
            return "vertex";
        return "entity";
    }
}

std::string_view EntityKind(const EditorScene& scene, EntityId entity)
{
    const World& components = scene.GetRegistry().Components;
    if (components.HasComponent<Space>(entity))
        return "space";
    if (components.HasComponent<Opening>(entity))
        return "opening";
    if (scene.TryGetBrush(entity) == nullptr)
        return "entity";
    return scene.RoleOf(entity) == BrushRole::Volume ? "volume" : "brush";
}

JsonValue EntityNameJson(const EditorDocument& document, EntityId entity)
{
    return JsonValue(JsonValue::Object{
        { "id", JsonValue(PersistentIdText(document, entity)) },
        { "name", JsonValue(EntityNameText(document, entity)) },
    });
}

DataFieldSchema EntityNameSchema(std::string key, std::string summary)
{
    return DataField::Record(std::move(key), {}, std::move(summary), {
        DataField::Field("id", DataFieldKind::Entity, {}, {}),
        DataField::Field("name", DataFieldKind::String, {}, {}),
    });
}

JsonValue EntityRecordJson(const EditorDocument& document, EntityId entity)
{
    const std::optional<Aabb3d> bounds = document.GetScene().EvaluatedWorldBounds(entity);
    const Transform3f* world = document.GetScene().TryGetWorldTransform(entity);
    return JsonValue(JsonValue::Object{
        { "id", JsonValue(PersistentIdText(document, entity)) },
        { "name", JsonValue(EntityNameText(document, entity)) },
        { "kind", JsonValue(std::string(EntityKind(document.GetScene(), entity))) },
        { "position", world != nullptr ? PointJson(world->Position) : JsonValue{} },
        { "min", bounds ? PointJson(bounds->Min) : JsonValue{} },
        { "max", bounds ? PointJson(bounds->Max) : JsonValue{} },
    });
}

DataFieldSchema EntityRecordSchema(std::string key)
{
    return DataField::Record(std::move(key), {}, {}, {
        DataField::Field("id", DataFieldKind::Entity, {}, "persistent id"),
        DataField::Field("name", DataFieldKind::String, {}, "authored name"),
        KindField(),
        OptionalPoint("position"),
        OptionalPoint("min"),
        OptionalPoint("max"),
    });
}

JsonValue SelectionJson(const EditorDocument& document, std::span<const SelectableRef> selected)
{
    JsonValue::Array items;
    for (const SelectableRef& ref : selected)
    {
        if (!ref.IsValid())
            continue;
        JsonValue::Object item{
            { "id", JsonValue(PersistentIdText(document, ref.Entity)) },
            { "name", JsonValue(EntityNameText(document, ref.Entity)) },
            { "element", JsonValue(std::string(ElementName(ref))) },
        };
        if (ref.IsMeshElement())
            item.emplace_back("index", JsonValue(static_cast<double>(ref.ElementId)));
        items.push_back(JsonValue(std::move(item)));
    }
    return JsonValue(std::move(items));
}

DataFieldSchema SelectionSchema(std::string key, std::string summary)
{
    return DataField::ArrayOf(std::move(key), {}, std::move(summary),
        DataField::Record({}, {}, {}, {
            DataField::Field("id", DataFieldKind::Entity, {}, {}),
            DataField::Field("name", DataFieldKind::String, {}, {}),
            DataField::Enum("element", {}, "what part of the entity is selected",
                { { "entity", "Entity", {} }, { "face", "Face", {} }, { "edge", "Edge", {} }, { "vertex", "Vertex", {} } }),
            DataField::Optional("index", {}, "the face, edge or vertex index",
                DataField::Field({}, DataFieldKind::Int, {}, {}), false),
        }),
        true);
}
