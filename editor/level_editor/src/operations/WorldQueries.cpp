#include "WorldQueries.h"

#include "DiagnosticRecords.h"
#include "LevelJson.h"
#include "LevelSubjects.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/WorldDocument.h"

#include <core/metadata/DataSchemaFields.h>
#include <world/registry/Registry.h>
#include <zone/WorldConnectionComponents.h>
#include <zone/WorldPartitionIds.h>

#include <format>
#include <string>

namespace
{
    DataFieldSchema Text(std::string key, std::string summary = {})
    {
        return DataField::Field(std::move(key), DataFieldKind::String, {}, std::move(summary));
    }

    DataFieldSchema Point(std::string key, std::string summary = {})
    {
        return DataField::Vector(std::move(key), {}, std::move(summary));
    }

    std::string ZoneName(const WorldDocument& world, ZoneId zone)
    {
        for (const ZoneHeader& header : world.Manifest().Zones)
            if (header.Id == zone)
                return header.Name;
        return {};
    }

    EditorOperationEntry Summary(const WorldDocument& world)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "world.summary";
        entry.Definition.Category = "World";
        entry.Definition.Description =
            "The world's zones, which one the level operations read, and the docks between "
            "them: where each stands, which way it faces and how big it is.";
        const DataFieldSchema zoneRecord = DataField::Record({}, {}, {}, {
            Text("id"), Text("name"), Text("scene", "the zone's level document"),
            Point("min"), Point("max"),
            DataField::Field("focus", DataFieldKind::Bool, {}, "the zone the level operations read"),
        });
        const DataFieldSchema dockRecord = DataField::Record({}, {}, {}, {
            Text("id"), DataField::Field("entity", DataFieldKind::Entity, {}, {}),
            Text("zone_a"), Text("zone_a_name"), Text("zone_b"), Text("zone_b_name"),
            Point("position"), Point("normal", "the side zone_a lies on"),
            DataField::Vector("half_extents", {}, "[across, up]", 2),
            DataField::Enum("directions", {}, {}, { { "a_to_b", "A to B", {} },
                                                     { "b_to_a", "B to A", {} },
                                                     { "both", "Both", {} } }),
        });
        entry.Definition.Result = DataField::Record({}, {}, {}, {
            Text("name"), Text("start_zone"),
            DataField::ArrayOf("zones", {}, {}, zoneRecord, true),
            DataField::ArrayOf("docks", {}, {}, dockRecord, true),
        });
        entry.Execute = [&world](const JsonValue&) {
            if (!world.IsWorld())
                return EditorOperationOutcome::Refused("world.none", "a single level is open, not a world");
            const WorldPartitionManifest& manifest = world.Manifest();
            JsonValue::Array zones;
            for (const ZoneHeader& header : manifest.Zones)
                zones.push_back(JsonValue(JsonValue::Object{
                    { "id", JsonValue(ZoneIdToString(header.Id)) },
                    { "name", JsonValue(header.Name) },
                    { "scene", JsonValue(header.SceneRef) },
                    { "min", PointJson(header.Bounds.Min) },
                    { "max", PointJson(header.Bounds.Max) },
                    { "focus", JsonValue(header.Id == world.FocusZone()) },
                }));

            const EditorDocument& scene = world.WorldSceneDocument();
            JsonValue::Array docks;
            for (const EntityId entity : scene.GetScene().GetAllEntities())
            {
                const WorldDock* dock = scene.GetRegistry().Components.TryGet<WorldDock>(entity);
                if (dock == nullptr)
                    continue;
                const Transform3f placed = scene.GetScene().ComposeWorldTransform(entity);
                const std::string_view directions =
                    dock->Directions == DockDirectionBoth  ? "both"
                    : dock->Directions == DockDirectionAToB ? "a_to_b"
                                                            : "b_to_a";
                docks.push_back(JsonValue(JsonValue::Object{
                    { "id", JsonValue(DockIdToString(dock->Id)) },
                    { "entity", JsonValue(PersistentIdText(scene, entity)) },
                    { "zone_a", JsonValue(ZoneIdToString(dock->ZoneA)) },
                    { "zone_a_name", JsonValue(ZoneName(world, dock->ZoneA)) },
                    { "zone_b", JsonValue(ZoneIdToString(dock->ZoneB)) },
                    { "zone_b_name", JsonValue(ZoneName(world, dock->ZoneB)) },
                    { "position", PointJson(placed.Position) },
                    { "normal", PointJson(placed.Forward() * -1.0f) },
                    { "half_extents", JsonValue(JsonValue::Array{ MetresJson(dock->HalfExtents.X),
                                                                  MetresJson(dock->HalfExtents.Y) }) },
                    { "directions", JsonValue(std::string(directions)) },
                }));
            }
            return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                { "name", JsonValue(manifest.Name) },
                { "start_zone", JsonValue(ZoneIdToString(manifest.StartZone)) },
                { "zones", JsonValue(std::move(zones)) },
                { "docks", JsonValue(std::move(docks)) },
            }));
        };
        return entry;
    }

    EditorOperationEntry Diagnostics(const WorldDocument& world)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "diagnostics";
        entry.Definition.Category = "World";
        entry.Definition.Description =
            "What the world's validation reports: each record's severity, stable rule id, the "
            "kind and id of what it is about, and a message.";
        entry.Definition.Result = DataField::Record({}, {}, {}, { ContentRiskRecordsSchema("records") });
        entry.Execute = [&world](const JsonValue&) {
            return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                { "records", ContentRiskRecordsJson(world.ValidationRecords()) },
            }));
        };
        return entry;
    }
}

std::vector<EditorOperationEntry> WorldQueryOperations(const WorldDocument& world)
{
    std::vector<EditorOperationEntry> entries;
    entries.push_back(Summary(world));
    entries.push_back(Diagnostics(world));
    return entries;
}
