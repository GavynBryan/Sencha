#include "LevelMutations.h"

#include "LevelSubjects.h"
#include "LevelTarget.h"

#include "document/EditorDocument.h"
#include "document/EntityChangeSet.h"
#include "document/EntityNameComponent.h"
#include "document/commands/RenameEntityCommand.h"

#include <core/metadata/DataSchemaFields.h>

#include <string>

namespace
{
    constexpr std::size_t kLongestName = sizeof(EntityNameComponent::Value) - 1;

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
        entry.Execute = [&target](const JsonValue& arguments) {
            return target.Mutate([&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                const SubjectResolution subject =
                    ResolveEntitySubject(document, arguments.Find("subject")->AsString());
                if (!subject.Resolved())
                    return EditorOperationOutcome::Refused(subject.Code, subject.Message);
                const std::string& name = arguments.Find("name")->AsString();
                if (name.size() > kLongestName)
                    return EditorOperationOutcome::Refused(
                        "entity.name_invalid", "a name is at most " + std::to_string(kLongestName) + " characters");

                const EntityId entity = subject.Entities.front();
                recorder.Touch(entity);
                if (auto rename = MakeRenameEntityCommand(entity, name, document.GetScene(), document))
                    rename->Execute();
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
    return entries;
}
