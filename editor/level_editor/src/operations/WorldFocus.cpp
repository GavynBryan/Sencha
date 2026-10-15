#include "WorldFocus.h"

#include "document/WorldDocument.h"

#include <core/metadata/DataSchemaFields.h>
#include <zone/WorldPartitionManifest.h>

#include <string>

std::optional<ZoneId> ZoneNamed(const WorldPartitionManifest& manifest, std::string_view text)
{
    if (const std::optional<ZoneId> id = ZoneIdFromString(text))
        for (const ZoneHeader& header : manifest.Zones)
            if (header.Id == *id)
                return id;
    for (const ZoneHeader& header : manifest.Zones)
        if (header.Name == text)
            return header.Id;
    return std::nullopt;
}

namespace
{
    EditorOperationEntry Focus(WorldDocument& world)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "world.focus";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "World";
        entry.Definition.Description =
            "Chooses which document of the world the level operations read and write: a zone, by id or name, "
            "or 'world' for the world scene, which holds the docks. Opens the zone when it is closed. Live, it "
            "moves the designer's focus too.";
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            DataField::Field("target", DataFieldKind::String, {}, "a zone's id or name, or 'world'"),
        });
        entry.Definition.Result = DataField::Record({}, {}, {}, {
            DataField::Field("focus", DataFieldKind::String, {}, "the zone's id, or 'world'"),
        });
        entry.Execute = [&world](const JsonValue& arguments) {
            if (!world.IsWorld())
                return EditorOperationOutcome::Refused("world.none", "a single level is open, not a world");
            const std::string& target = arguments.Find("target")->AsString();
            if (target == "world")
            {
                if (!world.FocusWorldScene())
                    return EditorOperationOutcome::Refused("world.no_scene", "the world has no world scene");
                return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{ { "focus", JsonValue("world") } }));
            }
            const std::optional<ZoneId> zone = ZoneNamed(world.Manifest(), target);
            if (!zone)
                return EditorOperationOutcome::Refused("world.zone_unknown", "the world has no zone '" + target + "'");
            if (!world.SetFocusZone(*zone))
                return EditorOperationOutcome::Refused("world.zone_unopenable", "the zone '" + target + "' could not be opened");
            return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{ { "focus", JsonValue(ZoneIdToString(*zone)) } }));
        };
        return entry;
    }
}

std::vector<EditorOperationEntry> WorldFocusOperations(WorldDocument& world)
{
    std::vector<EditorOperationEntry> entries;
    entries.push_back(Focus(world));
    return entries;
}
