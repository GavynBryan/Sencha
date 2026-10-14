#include "LevelChanges.h"

#include "EntityRecords.h"
#include "LevelSubjects.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/EntitySnapshot.h"
#include "operations/LevelTarget.h"

#include <core/identity/Id.h>
#include <core/metadata/DataSchemaFields.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>

namespace
{
    constexpr std::size_t kMarksHeld = 16;

    struct MarkedEntity
    {
        PersistentEntityId Id;
        std::uint64_t      Digest = 0;
        std::string        Name;
    };

    struct ChangeMark
    {
        std::string   Token;
        std::uint64_t Instance = 0;
        std::uint64_t Revision = 0;
        // By id, for lookup.
        std::vector<MarkedEntity> Entities;
    };

    class ChangeMarks
    {
    public:
        const ChangeMark& Take(const EditorDocument& level)
        {
            ChangeMark mark{ std::to_string(Next++), level.Instance(), level.Revision(), {} };
            const EditorScene& scene = level.GetScene();
            for (const EntityId entity : scene.GetAllEntities())
                if (const PersistentEntityId id = scene.PersistentIdOf(entity); id.IsValid())
                    mark.Entities.push_back({ id, DigestOf(level.CaptureEntity(entity)), EntityNameText(level, entity) });
            std::ranges::sort(mark.Entities, {}, [](const MarkedEntity& marked) { return marked.Id.Value; });
            if (Held.size() == kMarksHeld)
                Held.pop_front();
            Held.push_back(std::move(mark));
            return Held.back();
        }

        [[nodiscard]] const ChangeMark* Find(std::string_view token) const
        {
            const auto found = std::ranges::find(Held, token, &ChangeMark::Token);
            return found != Held.end() ? &*found : nullptr;
        }

    private:
        std::deque<ChangeMark> Held;
        std::uint64_t Next = 1;
    };

    const MarkedEntity* Marked(const ChangeMark& mark, PersistentEntityId id)
    {
        const auto found = std::ranges::lower_bound(mark.Entities, id.Value, {},
                                                    [](const MarkedEntity& marked) { return marked.Id.Value; });
        return found != mark.Entities.end() && found->Id == id ? &*found : nullptr;
    }

    DataFieldSchema Root(std::vector<DataFieldSchema> members)
    {
        DataFieldSchema root = AuthoredRecordRoot();
        root.Children = std::move(members);
        return root;
    }

    DataFieldSchema MarkField(std::string summary)
    {
        return DataField::Field("mark", DataFieldKind::String, {}, std::move(summary));
    }

    EditorOperationEntry Mark(const LevelTarget& level, std::shared_ptr<ChangeMarks> marks)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "changes.mark";
        entry.Definition.Category = "Level";
        entry.Definition.Description =
            "Remembers the level as it is now, for changes.since to compare against. The newest 16 marks are kept.";
        entry.Definition.Result = Root({ MarkField("names this moment to changes.since") });
        entry.Execute = [&level, marks](const JsonValue&) {
            return EditorOperationOutcome::Ok(
                JsonValue(JsonValue::Object{ { "mark", JsonValue(marks->Take(level.FocusLevel()).Token) } }));
        };
        return entry;
    }

    EditorOperationEntry Since(const LevelTarget& level, std::shared_ptr<ChangeMarks> marks)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "changes.since";
        entry.Definition.Category = "Level";
        entry.Definition.Description =
            "What was created, changed and deleted in the level since a mark, by anyone, and a mark for now.";
        entry.Definition.Parameters = Root({ MarkField("from changes.mark or an earlier changes.since") });
        entry.Definition.Result = Root({
            MarkField("the level as it is now; the same mark when nothing changed"),
            DataField::ArrayOf("created", {}, {}, EntityRecordSchema(), true),
            DataField::ArrayOf("modified", {}, {}, EntityRecordSchema(), true),
            DataField::ArrayOf("deleted", {}, {}, EntityNameSchema({}, "as it was named"), true),
        });
        entry.Execute = [&level, marks](const JsonValue& arguments) {
            const std::string token = arguments.Find("mark")->AsString();
            const ChangeMark* mark = marks->Find(token);
            if (mark == nullptr)
                return EditorOperationOutcome::Refused("changes.unknown_mark",
                                                       "no mark '" + token + "' is held; take a new one with changes.mark");
            const EditorDocument& document = level.FocusLevel();
            if (mark->Instance != document.Instance())
                return EditorOperationOutcome::Refused("changes.level_replaced",
                                                       "the level was closed or reloaded after the mark");

            JsonValue::Array created;
            JsonValue::Array modified;
            JsonValue::Array deleted;
            std::string now = mark->Token;
            if (mark->Revision != document.Revision())
            {
                const EditorScene& scene = document.GetScene();
                std::vector<std::uint64_t> present;
                for (const EntityId entity : scene.GetAllEntities())
                {
                    const PersistentEntityId id = scene.PersistentIdOf(entity);
                    if (!id.IsValid())
                        continue;
                    present.push_back(id.Value);
                    const MarkedEntity* before = Marked(*mark, id);
                    if (before == nullptr)
                        created.push_back(EntityRecordJson(document, entity));
                    else if (before->Digest != DigestOf(document.CaptureEntity(entity)))
                        modified.push_back(EntityRecordJson(document, entity));
                }
                std::ranges::sort(present);
                for (const MarkedEntity& before : mark->Entities)
                    if (!std::ranges::binary_search(present, before.Id.Value))
                        deleted.push_back(JsonValue(JsonValue::Object{
                            { "id", JsonValue(PersistentEntityIdToString(before.Id)) },
                            { "name", JsonValue(before.Name) },
                        }));
                now = marks->Take(document).Token;
            }
            return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                { "mark", JsonValue(now) },
                { "created", JsonValue(std::move(created)) },
                { "modified", JsonValue(std::move(modified)) },
                { "deleted", JsonValue(std::move(deleted)) },
            }));
        };
        return entry;
    }
}

std::vector<EditorOperationEntry> LevelChangeOperations(const LevelTarget& level)
{
    const auto marks = std::make_shared<ChangeMarks>();
    std::vector<EditorOperationEntry> entries;
    entries.push_back(Mark(level, marks));
    entries.push_back(Since(level, marks));
    return entries;
}
