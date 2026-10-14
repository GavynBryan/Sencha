#include "LevelTarget.h"

#include "LevelSubjects.h"

#include "commands/CommandStack.h"
#include "document/EditorDocument.h"
#include "document/EntityChangeSet.h"
#include "document/EntityNameComponent.h"
#include "document/WorldDocument.h"
#include "document/commands/ApplyChangeSetCommand.h"

#include <core/metadata/DataSchemaFields.h>

#include <memory>
#include <string>

namespace
{
    std::string RecordedName(const EntitySnapshot& snapshot)
    {
        const JsonValue* name = snapshot.Components.IsObject()
            ? snapshot.Components.Find(TypeSchema<EntityNameComponent>::Name)
            : nullptr;
        const JsonValue* value = name != nullptr ? name->Find("value") : nullptr;
        return value != nullptr && value->IsString() ? value->AsString() : std::string{};
    }

    void AddSummary(JsonValue& data, const EntityChangeSet& changes, std::uint64_t before, std::uint64_t after)
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
        if (!data.IsObject())
            data = JsonValue(JsonValue::Object{});
        JsonValue::Object& object = data.AsObject();
        object.emplace_back("created", JsonValue(std::move(created)));
        object.emplace_back("modified", JsonValue(std::move(modified)));
        object.emplace_back("deleted", JsonValue(std::move(deleted)));
        object.emplace_back("revision", JsonValue(JsonValue::Object{
            { "before", JsonValue(static_cast<double>(before)) },
            { "after", JsonValue(static_cast<double>(after)) },
        }));
    }
}

EditorDocument& LevelTarget::Document()
{
    return World.FocusDocument();
}

const EditorDocument& LevelTarget::Document() const
{
    return World.FocusDocument();
}

EditorOperationOutcome LevelTarget::Mutate(const Edit& edit)
{
    EditorDocument& document = Document();
    const std::uint64_t before = document.Revision();
    EntityChangeRecorder recorder(document);
    EditorOperationOutcome outcome = edit(document, recorder);
    EntityChangeSet changes = recorder.Finish();
    if (outcome.Status != EditorOperationStatus::Ok)
    {
        ApplyEntityChangeSet(document, changes, ChangeSetSide::Before);
        return outcome;
    }
    AddSummary(outcome.Data, changes, before, document.Revision());
    if (!changes.Empty())
        Commands.Execute(std::make_unique<ApplyChangeSetCommand>(document, std::move(changes),
                                                                 ChangeSetSide::After));
    return outcome;
}

DataFieldSchema MutationResultSchema(std::vector<DataFieldSchema> extra)
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
