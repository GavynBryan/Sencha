#include "InstanceOperations.h"

#include "EntityRecords.h"
#include "LevelJson.h"
#include "LevelSubjects.h"
#include "LevelTarget.h"

#include "document/EditorDocument.h"
#include "document/EntityChangeSet.h"

#include <core/identity/Id.h>
#include <core/metadata/DataSchemaFields.h>
#include <math/Angle.h>
#include <math/Quat.h>

#include <string>

namespace
{
    EditorOperationEntry Place(LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "instance.place";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Level";
        entry.Definition.Description =
            "Places a scene (a prefab: a turret, a lamp, a door frame) at a point, turned about up, as one "
            "undoable step. Its entities follow the source; edits to them are kept as the placement's overrides.";
        DataFieldSchema turn = DataField::Field("turn", DataFieldKind::Float, {}, "degrees counter-clockwise seen from above", false);
        turn.Default = 0.0;
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            DataField::Field("source", DataFieldKind::String, {}, "the scene's asset path, as assets.list finds it with 't:scene'"),
            DataField::Vector("at", {}, "where its origin goes"),
            std::move(turn),
        });
        entry.Definition.Result = MutationResultSchema({ EntityNameSchema("placement", "the placement's root") });
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            return level.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                const std::string& source = arguments.Find("source")->AsString();
                Transform3f placement = Transform3f::Identity();
                placement.Position = PointFromJson(*arguments.Find("at"));
                const float degrees = static_cast<float>(arguments.Find("turn")->AsNumber());
                placement.Rotation = Quatf::FromAxisAngle(Vec3d::Up(), ToRadians(degrees));
                std::string error;
                const SceneInstanceId placed = document.PlaceSceneInstance(source, placement, {}, &error);
                if (!placed.IsValid())
                    return EditorOperationOutcome::Refused("instance.refused", error);
                recorder.Placed(placed);
                const SubjectResolution root =
                    ResolveEntitySubject(document, PersistentEntityIdToString(PersistentEntityId{ placed.Value }));
                JsonValue named = root.Resolved() ? EntityNameJson(document, root.Entities.front()) : JsonValue{};
                return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{ { "placement", std::move(named) } }));
            });
        };
        return entry;
    }
}

std::vector<EditorOperationEntry> InstanceOperations(LevelTarget& level)
{
    std::vector<EditorOperationEntry> entries;
    entries.push_back(Place(level));
    return entries;
}
