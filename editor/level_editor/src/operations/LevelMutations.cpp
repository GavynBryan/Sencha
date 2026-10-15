#include "LevelMutations.h"

#include "EntityRecords.h"
#include "LevelJson.h"
#include "LevelSubjects.h"
#include "LevelTarget.h"

#include "document/EditorDocument.h"
#include "document/EditorEntityRecipe.h"
#include "document/EditorScene.h"
#include "document/EntityBranches.h"
#include "document/EntityChangeSet.h"
#include "document/EntityNameComponent.h"
#include "document/SpaceComponents.h"
#include "document/commands/RenameEntityCommand.h"
#include "document/commands/ReparentEntitiesCommand.h"

#include <core/metadata/DataSchemaFields.h>

#include <cstdint>
#include <ranges>
#include <string>
#include <vector>

namespace
{
    EditorOperationEntry Rename(LevelTarget& target)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "entity.rename";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Level";
        entry.Definition.Description = "Gives an entity a new authored name, as one undoable step. "
                                       "Surrounding spaces are trimmed and an empty name clears it.";
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            DataField::Field("subject", DataFieldKind::String, {}, "an entity id or '@<name>'"),
            DataField::Field("name", DataFieldKind::String, {}, "the new name; empty clears it"),
        });
        entry.Definition.Result = MutationResultSchema();
        entry.Execute = [&target, name = entry.Definition.Name](const JsonValue& arguments) {
            return target.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                const SubjectResolution subject =
                    ResolveEntitySubject(document, arguments.Find("subject")->AsString());
                if (!subject.Resolved())
                    return SubjectRefusal(subject);
                const EntityId entity = subject.Entities.front();
                const std::string newName = NormalizedEntityName(arguments.Find("name")->AsString());
                const bool space = document.GetScene().GetRegistry().Components.HasComponent<Space>(entity);
                if (space && newName.empty())
                    return EditorOperationOutcome::Refused("space.name_invalid", "a space needs a name");
                if (std::optional<EditorOperationOutcome> refused = RefuseEntityName(document, entity, space, newName))
                    return std::move(*refused);
                recorder.Touch(entity);
                if (auto rename = MakeRenameEntityCommand(entity, newName, document.GetScene(), document))
                    rename->Execute();
                return EditorOperationOutcome::Ok({});
            });
        };
        return entry;
    }

    DataFieldSchema Subjects(std::string summary)
    {
        return DataField::ArrayOf("subjects", {}, std::move(summary),
                                  DataField::Field({}, DataFieldKind::String, {}, "an entity id or '@<name>'"), true);
    }

    EditorOperationEntry Delete(LevelTarget& target)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "entity.delete";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Level";
        entry.Definition.Description = "Deletes entities and everything under them, as one undoable step. "
                                       "A space's doors go with it; walls it shared are rebuilt for its neighbours.";
        entry.Definition.Parameters = DataField::Record({}, {}, {}, { Subjects("what to delete") });
        entry.Definition.Result = MutationResultSchema();
        entry.Execute = [&target, name = entry.Definition.Name](const JsonValue& arguments) {
            return target.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                const SubjectResolution subjects = ResolveEntitySubjects(document, *arguments.Find("subjects"));
                if (!subjects.Resolved())
                    return SubjectRefusal(subjects);
                const std::vector<EntityId> doomed = CollectBranches(document.GetScene(), subjects.Entities);
                for (const EntityId entity : doomed)
                    recorder.Touch(entity);
                for (const EntityId entity : std::views::reverse(doomed))
                    document.GetScene().DestroyEntity(entity);
                return EditorOperationOutcome::Ok({});
            });
        };
        return entry;
    }

    EditorOperationEntry Duplicate(LevelTarget& target)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "entity.duplicate";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Level";
        entry.Definition.Description = "Copies entities and everything under them, offset by a vector, as one "
                                       "undoable step. Copies take fresh ids and the next free names.";
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            Subjects("what to copy"),
            DataField::Vector("offset", {}, "metres from each source to its copy; none when absent", 3, false) });
        entry.Definition.Result = MutationResultSchema({
            DataField::ArrayOf("copies", {}, "the copy of each subject, in order", EntityNameSchema({}, {}), true) });
        entry.Execute = [&target, name = entry.Definition.Name](const JsonValue& arguments) {
            WorldDocument& world = target.LevelWorld();
            return target.Mutate(name, arguments, [&arguments, &world](EditorDocument& document, EntityChangeRecorder& recorder) {
                const SubjectResolution subjects = ResolveEntitySubjects(document, *arguments.Find("subjects"));
                if (!subjects.Resolved())
                    return SubjectRefusal(subjects);
                const JsonValue* given = arguments.Find("offset");
                const Vec3d offset = given != nullptr ? PointFromJson(*given) : Vec3d{ 0, 0, 0 };
                std::vector<Transform3f> placements;
                for (const EntityId source : subjects.Entities)
                {
                    const Transform3f* placed = document.GetScene().TryGetWorldTransform(source);
                    Transform3f moved = placed != nullptr ? *placed : Transform3f::Identity();
                    moved.Position = moved.Position + offset;
                    placements.push_back(moved);
                }
                for (const EntityId source : subjects.Entities)
                    recorder.Read(source);
                const BranchCopy copy = CaptureBranchCopy(document, subjects.Entities, DuplicateBranchPolicy::Subtree,
                    [&world](std::vector<EntitySnapshot>& snapshots) { RemapWorldConnectionDuplicateSnapshots(world, snapshots); });
                const std::vector<EntityId> created = RestoreBranchCopy(document, copy, placements, false);
                JsonValue::Array copies(subjects.Entities.size());
                for (std::size_t index = 0; index < created.size(); ++index)
                {
                    recorder.Created(created[index]);
                    if (copy.RootOf[index] != SIZE_MAX)
                        copies[copy.RootOf[index]] = EntityNameJson(document, created[index]);
                }
                return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{ { "copies", JsonValue(std::move(copies)) } }));
            });
        };
        return entry;
    }

    EditorOperationEntry Reparent(LevelTarget& target)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "entity.reparent";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Level";
        entry.Definition.Description = "Moves entities under a new parent, or to the top of the level, as one "
                                       "undoable step. By default they stay where they are in the world.";
        DataFieldSchema parentField = DataField::Field("parent", DataFieldKind::String, {},
                                                  "an entity id or '@<name>'; empty for the top of the level", false);
        parentField.Default = std::string{};
        DataFieldSchema keep = DataField::Enum("keep", {}, "what the move preserves",
            { { "world", "World", "where it stands in the world" },
              { "local", "Local", "its placement relative to its parent, so it moves with the new one" } },
            false);
        keep.Default = std::string("world");
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            Subjects("what to move"), std::move(parentField), std::move(keep) });
        entry.Definition.Result = MutationResultSchema();
        entry.Execute = [&target, name = entry.Definition.Name](const JsonValue& arguments) {
            return target.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                const SubjectResolution subjects = ResolveEntitySubjects(document, *arguments.Find("subjects"));
                if (!subjects.Resolved())
                    return SubjectRefusal(subjects);
                EntityId parent;
                if (const std::string& named = arguments.Find("parent")->AsString(); !named.empty())
                {
                    const SubjectResolution found = ResolveEntitySubject(document, named);
                    if (!found.Resolved())
                        return SubjectRefusal(found);
                    parent = found.Entities.front();
                }
                EditorScene& scene = document.GetScene();
                const ReparentTargets targets = ResolveReparentTargets(subjects.Entities, parent, scene, document);
                if (targets.Refusal == ReparentRefusal::Cycle)
                    return EditorOperationOutcome::Refused("reparent.cycle", "the parent is inside what would move under it");
                if (targets.Refusal == ReparentRefusal::PlacementMember)
                    return EditorOperationOutcome::Refused("reparent.placement_member",
                                                           "a placed scene's members keep the source's structure");
                if (targets.Entities.empty())
                    return EditorOperationOutcome::Ok({});
                for (const EntityId entity : targets.Entities)
                    recorder.Touch(entity);
                if (parent.IsValid())
                    recorder.Read(parent);
                const ReparentTransformRule rule = arguments.Find("keep")->AsString() == "local"
                    ? ReparentTransformRule::KeepLocal : ReparentTransformRule::KeepWorld;
                ReparentEntitiesCommand(targets.Entities, parent, rule, scene, document).Execute();
                return EditorOperationOutcome::Ok({});
            });
        };
        return entry;
    }
}

std::vector<EditorOperationEntry> LevelMutationOperations(LevelTarget& target)
{
    std::vector<EditorOperationEntry> entries;
    entries.push_back(Rename(target));
    entries.push_back(Delete(target));
    entries.push_back(Duplicate(target));
    entries.push_back(Reparent(target));
    return entries;
}
