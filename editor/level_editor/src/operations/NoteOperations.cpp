#include "NoteOperations.h"

#include "EntityRecords.h"
#include "LevelJson.h"
#include "LevelSubjects.h"
#include "LevelTarget.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/EntityChangeSet.h"
#include "document/NoteComponent.h"
#include "document/commands/RenameEntityCommand.h"

#include <core/metadata/DataSchemaFields.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>

#include <string>

namespace
{
    constexpr std::size_t kLongestText = sizeof(Note::Text) - 1;
    constexpr std::size_t kLongestAuthor = sizeof(Note::Author) - 1;

    std::string_view StateName(NoteState state)
    {
        return state == NoteState::Open ? "open" : "resolved";
    }

    EditorOperationEntry Add(LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "note.add";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Note";
        entry.Definition.Description =
            "Leaves a note in the level, as one undoable step: about an entity, parented to it so it follows, "
            "or at a point. Notes are authoring only; the cook strips them.";
        DataFieldSchema authorField = DataField::Field("author", DataFieldKind::String, {}, "who leaves it", false);
        authorField.Default = std::string("claude");
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            DataField::Field("text", DataFieldKind::String, {}, "what to say, at most 255 characters"),
            DataField::Field("subject", DataFieldKind::String, {}, "the entity it is about; absent for a point", false),
            DataField::Vector("at", {}, "where it stands; the subject's position when absent", 3, false),
            std::move(authorField),
        });
        entry.Definition.Result = MutationResultSchema({ EntityNameSchema("note", "the note left") });
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            return level.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                const std::string& text = arguments.Find("text")->AsString();
                const std::string& author = arguments.Find("author")->AsString();
                if (text.empty() || text.size() > kLongestText || author.size() > kLongestAuthor)
                    return EditorOperationOutcome::Refused("note.text_invalid",
                                                           "a note says something, in at most 255 characters by an author of at most 31");
                EditorScene& scene = document.GetScene();
                EntityId about;
                if (const JsonValue* subject = arguments.Find("subject"))
                {
                    const SubjectResolution found = ResolveEntitySubject(document, subject->AsString());
                    if (!found.Resolved())
                        return SubjectRefusal(found);
                    about = found.Entities.front();
                }
                const JsonValue* at = arguments.Find("at");
                const Transform3f* aboutFrame = about.IsValid() ? scene.TryGetWorldTransform(about) : nullptr;
                if (at == nullptr && aboutFrame == nullptr)
                    return EditorOperationOutcome::Refused("note.nowhere", "a note needs a subject or a point");

                const EntityId note = scene.CreateEntity(at != nullptr ? PointFromJson(*at) : aboutFrame->Position);
                scene.GetRegistry().Components.AddComponent(note, Note{ InlineString<256>(text), InlineString<32>(author), NoteState::Open });
                if (about.IsValid())
                {
                    const Transform3f placed = *scene.TryGetWorldTransform(note);
                    (void)scene.SetParent(note, about);
                    scene.SetWorldTransform(note, placed);
                }
                if (auto rename = MakeRenameEntityCommand(note, scene.NextEntityName("Note"), scene, document))
                    rename->Execute();
                recorder.Created(note);
                document.MarkDirty();
                return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{ { "note", EntityNameJson(document, note) } }));
            });
        };
        return entry;
    }

    EditorOperationEntry Resolve(LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "note.resolve";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Note";
        entry.Definition.Description = "Marks a note resolved, or open again, as one undoable step.";
        DataFieldSchema state = DataField::Enum("state", {}, {}, { { "resolved", "Resolved", {} }, { "open", "Open", {} } }, false);
        state.Default = std::string("resolved");
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            DataField::Field("subject", DataFieldKind::String, {}, "a note's id or '@<name>'"),
            std::move(state),
        });
        entry.Definition.Result = MutationResultSchema();
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            return level.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                const SubjectResolution found = ResolveEntitySubject(document, arguments.Find("subject")->AsString());
                if (!found.Resolved())
                    return SubjectRefusal(found);
                Note* note = document.GetScene().GetRegistry().Components.TryGet<Note>(found.Entities.front());
                if (note == nullptr)
                    return EditorOperationOutcome::Refused("note.not_note", "it is not a note");
                recorder.Touch(found.Entities.front());
                note->State = arguments.Find("state")->AsString() == "open" ? NoteState::Open : NoteState::Resolved;
                document.MarkDirty();
                return EditorOperationOutcome::Ok({});
            });
        };
        return entry;
    }

    EditorOperationEntry List(const LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "notes.list";
        entry.Definition.Category = "Note";
        entry.Definition.Description = "The level's notes, the open ones unless asked otherwise, with what each is about.";
        DataFieldSchema state = DataField::Enum("state", {}, {},
            { { "open", "Open", {} }, { "resolved", "Resolved", {} }, { "all", "All", {} } }, false);
        state.Default = std::string("open");
        entry.Definition.Parameters = DataField::Record({}, {}, {}, { std::move(state) });
        entry.Definition.Result = DataField::Record({}, {}, {}, {
            DataField::ArrayOf("notes", {}, {},
                DataField::Record({}, {}, {}, {
                    EntityNameSchema("note", {}),
                    DataField::Field("text", DataFieldKind::String, {}, {}),
                    DataField::Field("author", DataFieldKind::String, {}, {}),
                    DataField::Enum("state", {}, {}, { { "open", "Open", {} }, { "resolved", "Resolved", {} } }),
                    DataField::Optional("about", {}, "the entity it is parented to", EntityNameSchema({}, {}), true),
                    DataField::Vector("at", {}, {}),
                }),
                true),
        });
        entry.Execute = [&level](const JsonValue& arguments) {
            const EditorDocument& document = level.Document();
            const EditorScene& scene = document.GetScene();
            const World& components = scene.GetRegistry().Components;
            const std::string& wanted = arguments.Find("state")->AsString();
            JsonValue::Array notes;
            for (const EntityId entity : scene.GetAllEntities())
            {
                const Note* note = components.TryGet<Note>(entity);
                if (note == nullptr || (wanted != "all" && wanted != StateName(note->State)))
                    continue;
                const EntityId about = scene.GetParent(entity);
                const Transform3f* frame = scene.TryGetWorldTransform(entity);
                notes.push_back(JsonValue(JsonValue::Object{
                    { "note", EntityNameJson(document, entity) },
                    { "text", JsonValue(std::string(note->Text.View())) },
                    { "author", JsonValue(std::string(note->Author.View())) },
                    { "state", JsonValue(std::string(StateName(note->State))) },
                    { "about", about.IsValid() ? EntityNameJson(document, about) : JsonValue{} },
                    { "at", PointJson(frame != nullptr ? frame->Position : Vec3d::Zero()) },
                }));
            }
            return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{ { "notes", JsonValue(std::move(notes)) } }));
        };
        return entry;
    }
}

std::vector<EditorOperationEntry> NoteOperations(LevelTarget& level)
{
    std::vector<EditorOperationEntry> entries;
    entries.push_back(Add(level));
    entries.push_back(Resolve(level));
    entries.push_back(List(level));
    return entries;
}
