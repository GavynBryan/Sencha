#include "LevelTarget.h"

#include "LevelSubjects.h"

#include "commands/CommandStack.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/EntityNameComponent.h"
#include "document/SpaceComponents.h"
#include "document/SpaceWalls.h"
#include "document/WorldDocument.h"
#include "document/commands/ApplyChangeSetCommand.h"

#include <core/metadata/DataSchemaFields.h>

#include <algorithm>
#include <array>
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

    void AddSummary(JsonValue& data, JsonValue::Object summary, std::uint64_t before, std::uint64_t after)
    {
        if (!data.IsObject())
            data = JsonValue(JsonValue::Object{});
        JsonValue::Object& object = data.AsObject();
        for (auto& field : summary)
            object.push_back(std::move(field));
        object.emplace_back("revision", JsonValue(JsonValue::Object{
            { "before", JsonValue(static_cast<double>(before)) },
            { "after", JsonValue(static_cast<double>(after)) },
        }));
    }

    EditorOperationOutcome RefuseBaseGone(const LevelDraft& draft)
    {
        return EditorOperationOutcome::Refused("draft.base_gone",
                                               "the level draft " + draft.Id + " copies is no longer open");
    }

    std::string FailureText(const LevelDraftFailure& failure)
    {
        return failure.Operation + " at step " + std::to_string(failure.Step) + ": " + failure.Message;
    }

    EditorOperationOutcome RefuseFailed(const LevelDraft& draft)
    {
        return EditorOperationOutcome::Refused(
            "draft.failed", "draft " + draft.Id + " failed (" + FailureText(draft.Failure) + "); "
                + (draft.Unreplayed.empty() ? "resume" : "rebase") + " or discard it");
    }

    void Fail(LevelDraft& draft, std::size_t step, std::string_view operation, const EditorOperationOutcome& outcome)
    {
        draft.State = LevelDraftState::Failed;
        draft.Failure = { step, std::string(operation), outcome.Code, outcome.Message };
    }
}

LevelTarget::LevelTarget(WorldDocument& world, CommandStack& commands)
    : World(world)
    , Commands(commands)
{
}

LevelTarget::~LevelTarget() = default;

EditorDocument& LevelTarget::Document()
{
    return Writing != nullptr ? *Writing->Document : World.FocusDocument();
}

const EditorDocument& LevelTarget::Document() const
{
    return Writing != nullptr ? *Writing->Document : World.FocusDocument();
}

const EditorDocument& LevelTarget::FocusLevel() const
{
    return World.FocusDocument();
}

EditorOperationOutcome LevelTarget::Mutate(std::string_view operation, const JsonValue& arguments,
                                           const Edit& edit)
{
    if (Writing != nullptr && Writing->State == LevelDraftState::Failed)
        return RefuseFailed(*Writing);
    return Run(Writing, operation, arguments, edit);
}

EditorOperationOutcome LevelTarget::MutateLevel(const Edit& edit)
{
    return Run(nullptr, {}, JsonValue{}, edit);
}

EditorOperationOutcome LevelTarget::Run(LevelDraft* draft, std::string_view operation, const JsonValue& arguments,
                                        const Edit& edit)
{
    EditorDocument& document = draft != nullptr ? *draft->Document : World.FocusDocument();
    // An operation is where a mutation finishes, so world transforms are made
    // current on both sides of it: the edit reads them, and so does whatever
    // the client asks next.
    document.GetScene().RefreshDerivedTransforms();
    const std::uint64_t before = document.Revision();
    EntityChangeRecorder recorder(document);
    EditorOperationOutcome outcome = edit(document, recorder);
    if (outcome.Status == EditorOperationStatus::Ok)
    {
        document.GetScene().RefreshDerivedTransforms();
        RebuildStaleSpaceWalls(document, recorder);
    }
    EntityChangeSet changes = recorder.Finish();
    if (outcome.Status != EditorOperationStatus::Ok)
    {
        ApplyEntityChangeSet(document, changes, ChangeSetSide::Before);
        document.GetScene().RefreshDerivedTransforms();
        if (draft != nullptr)
            Fail(*draft, draft->Steps.size() + 1, operation, outcome);
        return outcome;
    }

    document.GetScene().RefreshDerivedTransforms();
    AddSummary(outcome.Data, ChangeSummary(changes), before, document.Revision());
    if (draft != nullptr)
    {
        AppendChangeSet(draft->Changes, std::move(changes));
        draft->Steps.push_back({ std::string(operation), arguments });
    }
    else if (!changes.Empty())
    {
        Commands.Execute(std::make_unique<ApplyChangeSetCommand>(document, std::move(changes),
                                                                 ChangeSetSide::After));
    }
    return outcome;
}

std::unique_ptr<LevelDraft> LevelTarget::CopyLevel(EditorDocument& level, std::string label,
                                                   std::string rationale, std::uint32_t idSeed, std::string& error)
{
    auto draft = std::make_unique<LevelDraft>();
    draft->Label = std::move(label);
    draft->Rationale = std::move(rationale);
    draft->BasePath = level.SourceAssetPath();
    draft->BaseInstance = level.Instance();
    draft->IdSeed = idSeed;
    draft->Document = std::make_unique<EditorDocument>(World.Logging());
    draft->Document->SetContentRoots(level.ContentRoots());
    if (!draft->Document->LoadFromSceneText(level.ToSceneText(), &error))
        return nullptr;
    draft->Document->GetScene().SeedPersistentIds(idSeed);
    return draft;
}

LevelDraft* LevelTarget::BeginDraft(std::string label, std::string rationale, std::uint32_t idSeed,
                                    std::string& error)
{
    std::unique_ptr<LevelDraft> draft =
        CopyLevel(World.FocusDocument(), std::move(label), std::move(rationale), idSeed, error);
    if (draft == nullptr)
        return nullptr;
    draft->Id = "d" + std::to_string(++DraftsBegun);
    Writing = Drafts_.emplace_back(std::move(draft)).get();
    Previewed = Writing;
    return Writing;
}

LevelDraft* LevelTarget::FindDraft(std::string_view id)
{
    const auto found = std::ranges::find(Drafts_, id, [](const auto& draft) { return std::string_view(draft->Id); });
    return found != Drafts_.end() ? found->get() : nullptr;
}

EditorDocument* LevelTarget::FindBase(const LevelDraft& draft)
{
    EditorDocument& focus = World.FocusDocument();
    if (focus.Instance() == draft.BaseInstance)
        return &focus;
    if (!World.IsWorld())
        return nullptr;
    if (World.WorldSceneDocument().Instance() == draft.BaseInstance)
        return &World.WorldSceneDocument();
    EditorDocument* found = nullptr;
    World.VisitOpenZones([&](ZoneId, EditorDocument& zone, const auto&) {
        if (found == nullptr && zone.Instance() == draft.BaseInstance)
            found = &zone;
    });
    return found;
}

EditorOperationOutcome LevelTarget::AcceptDraft(LevelDraft& draft)
{
    if (draft.State == LevelDraftState::Failed)
        return RefuseFailed(draft);
    EditorDocument* level = FindBase(draft);
    if (level == nullptr)
        return RefuseBaseGone(draft);
    const std::vector<PersistentEntityId> conflicts = ChangeSetConflicts(*level, draft.Changes);
    if (!conflicts.empty())
    {
        JsonValue::Array ids;
        for (const PersistentEntityId id : conflicts)
            ids.push_back(JsonValue(PersistentEntityIdToString(id)));
        return EditorOperationOutcome::Refused(
            "draft.conflict", "the level changed under entities draft " + draft.Id + " changes; rebase it",
            JsonValue(JsonValue::Object{ { "entities", JsonValue(std::move(ids)) } }));
    }

    const std::uint64_t before = level->Revision();
    JsonValue::Object summary = ChangeSummary(draft.Changes);
    if (!draft.Changes.Empty())
        Commands.Execute(std::make_unique<ApplyChangeSetCommand>(*level, std::move(draft.Changes),
                                                                 ChangeSetSide::Before));
    JsonValue data;
    AddSummary(data, std::move(summary), before, level->Revision());
    DiscardDraft(draft);
    return EditorOperationOutcome::Ok(std::move(data));
}

void LevelTarget::DiscardDraft(LevelDraft& draft)
{
    if (Writing == &draft)
        Writing = nullptr;
    if (Previewed == &draft)
        Previewed = nullptr;
    std::erase_if(Drafts_, [&draft](const auto& held) { return held.get() == &draft; });
}

std::vector<std::string> LevelTarget::DropOrphanedDrafts()
{
    std::vector<std::string> dropped;
    for (std::size_t index = Drafts_.size(); index-- > 0;)
    {
        LevelDraft& draft = *Drafts_[index];
        if (FindBase(draft) != nullptr)
            continue;
        dropped.push_back(draft.Label);
        DiscardDraft(draft);
    }
    return dropped;
}

bool LevelTarget::ReplayInto(LevelDraft& draft, std::span<const LevelDraftStep> steps, const Replay& replay)
{
    LevelDraft* const writing = Writing;
    Writing = &draft;
    for (std::size_t step = 0; step < steps.size(); ++step)
    {
        const std::size_t recorded = draft.Steps.size();
        EditorOperationOutcome replayed = replay(steps[step]);
        if (replayed.Status == EditorOperationStatus::Ok && draft.Steps.size() == recorded)
            replayed = EditorOperationOutcome::Refused("proposal.not_level_edit",
                                                       "the step answered without editing the draft");
        if (replayed.Status != EditorOperationStatus::Ok)
        {
            Fail(draft, step + 1, steps[step].Operation, replayed);
            draft.Unreplayed.assign(steps.begin() + static_cast<std::ptrdiff_t>(step), steps.end());
            Writing = writing;
            return false;
        }
    }
    Writing = writing;
    return true;
}

EditorOperationOutcome LevelTarget::ResumeDraft(LevelDraft& draft)
{
    if (!draft.Unreplayed.empty())
        return EditorOperationOutcome::Refused(
            "draft.incomplete", "draft " + draft.Id + " never replayed " + std::to_string(draft.Unreplayed.size())
                + " of its proposal's steps (" + FailureText(draft.Failure) + "); rebase or discard it");
    draft.State = LevelDraftState::Open;
    draft.Failure = {};
    return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{ { "draft", JsonValue(draft.Id) } }));
}

EditorOperationOutcome LevelTarget::RebaseDraft(LevelDraft& draft, const Replay& replay)
{
    EditorDocument* level = FindBase(draft);
    if (level == nullptr)
        return RefuseBaseGone(draft);
    std::string error;
    std::unique_ptr<LevelDraft> fresh = CopyLevel(*level, draft.Label, draft.Rationale, draft.IdSeed, error);
    if (fresh == nullptr)
        return EditorOperationOutcome::Refused("draft.copy_failed", error);
    fresh->Id = draft.Id;
    fresh->OpenedFrom = draft.OpenedFrom;

    std::vector<LevelDraftStep> steps = draft.Steps;
    steps.insert(steps.end(), draft.Unreplayed.begin(), draft.Unreplayed.end());
    if (!ReplayInto(*fresh, steps, replay))
    {
        const LevelDraftFailure& failure = fresh->Failure;
        return EditorOperationOutcome::Refused(
            "draft.rebase_failed",
            "step " + std::to_string(failure.Step) + " (" + failure.Operation + ") no longer applies: "
                + failure.Message,
            JsonValue(JsonValue::Object{ { "step", JsonValue(static_cast<double>(failure.Step)) },
                                         { "operation", JsonValue(failure.Operation) },
                                         { "code", JsonValue(failure.Code) } }));
    }
    draft = std::move(*fresh);
    return EditorOperationOutcome::Ok(JsonValue(ChangeSummary(draft.Changes)));
}

bool ChangesGeneratedWall(const EntityChange& change)
{
    const EntitySnapshot& shown = change.After ? *change.After : *change.Before;
    return shown.Components.IsObject() && shown.Components.Find(TypeSchema<GeneratedWall>::Name) != nullptr;
}

JsonValue::Object ChangeSummary(const EntityChangeSet& changes)
{
    JsonValue::Array created, modified, deleted;
    std::array<double, 3> walls{};
    for (const EntityChange& change : changes.Changes)
    {
        if (ChangesGeneratedWall(change))
        {
            walls[!change.Before ? 0 : !change.After ? 2 : 1] += 1;
            continue;
        }
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
    // A placement is listed as its root, which goes by the placement's id.
    for (const InstanceChange& change : changes.Instances)
    {
        const SceneInstanceRecord& shown = change.After ? *change.After : *change.Before;
        JsonValue entry(JsonValue::Object{
            { "id", JsonValue(PersistentEntityIdToString(PersistentEntityId{ change.Id.Value })) },
            { "name", JsonValue(shown.Name.empty() ? shown.Source : shown.Name) },
        });
        (!change.Before ? created : !change.After ? deleted : modified).push_back(std::move(entry));
    }
    return JsonValue::Object{
        { "created", JsonValue(std::move(created)) },
        { "modified", JsonValue(std::move(modified)) },
        { "deleted", JsonValue(std::move(deleted)) },
        { "walls", JsonValue(JsonValue::Object{
                       { "created", JsonValue(walls[0]) },
                       { "modified", JsonValue(walls[1]) },
                       { "deleted", JsonValue(walls[2]) },
                   }) },
    };
}

DataFieldSchema ChangeSummarySchema(std::vector<DataFieldSchema> extra)
{
    const DataFieldSchema touched = DataField::Record({}, {}, {}, {
        DataField::Field("id", DataFieldKind::Entity, {}, {}),
        DataField::Field("name", DataFieldKind::String, {}, {}),
    });
    std::vector<DataFieldSchema> members{
        DataField::ArrayOf("created", {}, {}, touched, true),
        DataField::ArrayOf("modified", {}, {}, touched, true),
        DataField::ArrayOf("deleted", {}, {}, touched, true),
        DataField::Record("walls", {}, "wall brushes the spaces built, counted rather than listed", {
            DataField::Field("created", DataFieldKind::Int, {}, {}),
            DataField::Field("modified", DataFieldKind::Int, {}, {}),
            DataField::Field("deleted", DataFieldKind::Int, {}, {}),
        }),
    };
    for (DataFieldSchema& field : extra)
        members.push_back(std::move(field));
    return DataField::Record({}, {}, {}, std::move(members));
}

DataFieldSchema MutationResultSchema(std::vector<DataFieldSchema> extra)
{
    extra.insert(extra.begin(), DataField::Record("revision", {}, {}, {
        DataField::Field("before", DataFieldKind::Int, {}, {}),
        DataField::Field("after", DataFieldKind::Int, {}, {}),
    }));
    return ChangeSummarySchema(std::move(extra));
}
