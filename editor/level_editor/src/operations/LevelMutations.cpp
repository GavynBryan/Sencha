#include "LevelMutations.h"

#include "LevelEditTarget.h"
#include "LevelSubjects.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/EntityNameComponent.h"
#include "document/commands/RenameEntityCommand.h"

#include <core/metadata/DataSchemaFields.h>

#include <string>

namespace
{
    constexpr std::size_t kLongestName = sizeof(EntityNameComponent::Value) - 1;

    // Every mutation's answer: what the change set did, and the revisions
    // either side of it.
    DataFieldSchema MutationResult(std::vector<DataFieldSchema> extra = {})
    {
        const DataFieldSchema touched = DataField::Record({}, {}, {}, {
            DataField::Field("id", DataFieldKind::Entity, {}, {}),
            DataField::Field("name", DataFieldKind::String, {}, {}),
        });
        std::vector<DataFieldSchema> members{
            DataField::ArrayOf("created", {}, {}, touched, true),
            DataField::ArrayOf("modified", {}, {}, touched, true),
            DataField::ArrayOf("deleted", {}, {}, touched, true),
            DataField::Record("revision", {}, {}, {
                DataField::Field("before", DataFieldKind::Int, {}, {}),
                DataField::Field("after", DataFieldKind::Int, {}, {}),
            }),
        };
        for (DataFieldSchema& field : extra)
            members.push_back(std::move(field));
        return DataField::Record({}, {}, {}, std::move(members));
    }

    std::string RecordedName(const EntitySnapshot& snapshot)
    {
        const JsonValue* name = snapshot.Components.IsObject() ? snapshot.Components.Find(TypeSchema<EntityNameComponent>::Name) : nullptr;
        const JsonValue* value = name != nullptr ? name->Find("value") : nullptr;
        return value != nullptr && value->IsString() ? value->AsString() : std::string{};
    }

    JsonValue::Object Summary(const EntityChangeSet& changes, std::uint64_t before, std::uint64_t after)
    {
        JsonValue::Array created, modified, deleted;
        for (const EntityChange& change : changes.Changes)
        {
            const EntitySnapshot& shown = change.After ? *change.After : *change.Before;
            JsonValue entry(JsonValue::Object{
                { "id", JsonValue(PersistentEntityIdToString(change.Id)) },
                { "name", JsonValue(RecordedName(shown)) },
            });
            if (!change.Before)
                created.push_back(std::move(entry));
            else if (!change.After)
                deleted.push_back(std::move(entry));
            else
                modified.push_back(std::move(entry));
        }
        return JsonValue::Object{
            { "created", JsonValue(std::move(created)) },
            { "modified", JsonValue(std::move(modified)) },
            { "deleted", JsonValue(std::move(deleted)) },
            { "revision", JsonValue(JsonValue::Object{
                { "before", JsonValue(static_cast<double>(before)) },
                { "after", JsonValue(static_cast<double>(after)) },
            }) },
        };
    }

    EditorOperationEntry Rename(LevelEditTarget& target)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "entity.rename";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Level";
        entry.Definition.Description = "Gives an entity a new authored name, as one undoable step. Surrounding spaces are "
            "trimmed and an empty name clears it.";
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            DataField::Field("subject", DataFieldKind::String, {}, "an entity id or '@<name>'"),
            DataField::Field("name", DataFieldKind::String, {}, "the new name; empty clears it"),
        });
        entry.Definition.Result = MutationResult();
        entry.Execute = [&target](const JsonValue& arguments) {
            EditorDocument& document = target.Document();
            const SubjectResolution subject =
                ResolveEntitySubject(document, arguments.Find("subject")->AsString());
            if (!subject.Resolved())
                return EditorOperationOutcome::Refused(subject.Code, subject.Message);
            const std::string& name = arguments.Find("name")->AsString();
            if (name.size() > kLongestName)
                return EditorOperationOutcome::Refused("entity.name_invalid",
                                                       "a name is at most " + std::to_string(kLongestName) + " characters");

            const std::uint64_t before = document.Revision();
            EntityChangeRecorder recorder(document);
            const EntityId entity = subject.Entities.front();
            recorder.Touch(entity);
            if (auto rename = MakeRenameEntityCommand(entity, name, document.GetScene(), document))
                rename->Execute();
            EntityChangeSet changes = recorder.Finish();
            const JsonValue::Object summary = Summary(changes, before, document.Revision());
            target.Commit(std::move(changes));
            return EditorOperationOutcome::Ok(JsonValue(summary));
        };
        return entry;
    }
}

std::vector<EditorOperationEntry> LevelMutationOperations(LevelEditTarget& target)
{
    std::vector<EditorOperationEntry> entries;
    entries.push_back(Rename(target));
    return entries;
}
