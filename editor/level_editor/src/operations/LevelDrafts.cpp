#include "LevelDrafts.h"

#include "LevelProposal.h"
#include "LevelTarget.h"

#include "document/EditorDocument.h"
#include "document/EntityChangeSet.h"

#include <core/io/FileBytes.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/metadata/DataSchemaFields.h>

#include <format>
#include <random>
#include <span>
#include <string>
#include <variant>

namespace
{
    DataFieldSchema Text(std::string key, std::string summary)
    {
        return DataField::Field(std::move(key), DataFieldKind::String, {}, std::move(summary));
    }

    DataFieldSchema OptionalText(std::string key, std::string summary)
    {
        return DataField::Optional(std::move(key), {}, std::move(summary),
                                   DataField::Field({}, DataFieldKind::String, {}, {}));
    }

    DataFieldSchema WhichDraft()
    {
        return OptionalText("draft", "the draft's id; the one being written when absent");
    }

    DataFieldSchema DraftOnly()
    {
        return DataField::Record({}, {}, {}, { WhichDraft() });
    }

    std::string StateName(LevelDraftState state)
    {
        return state == LevelDraftState::Open ? "open" : "failed";
    }

    DataFieldSchema FailureField()
    {
        return DataField::Optional("failure", {}, "the refused step, while failed",
            DataField::Record({}, {}, {}, {
                DataField::Field("step", DataFieldKind::Int, {}, "counted from 1"),
                Text("operation", {}),
                Text("code", "why it was refused"),
                Text("message", {}),
                DataField::Field("unreplayed", DataFieldKind::Int, {},
                                 "proposal steps never replayed, the refused one included; a rebase replays them"),
            }),
            true);
    }

    JsonValue FailureJson(const LevelDraft& draft)
    {
        if (draft.State != LevelDraftState::Failed)
            return JsonValue{};
        return JsonValue(JsonValue::Object{
            { "step", JsonValue(static_cast<double>(draft.Failure.Step)) },
            { "operation", JsonValue(draft.Failure.Operation) },
            { "code", JsonValue(draft.Failure.Code) },
            { "message", JsonValue(draft.Failure.Message) },
            { "unreplayed", JsonValue(static_cast<double>(draft.Unreplayed.size())) },
        });
    }

    DataFieldSchema StateField()
    {
        return DataField::Enum("state", {}, {},
                               { { "open", "Open", "takes further steps and can be accepted" },
                                 { "failed", "Failed", "a step was refused; resume or discard it" } });
    }

    // The draft an operation names, or the one being written.
    std::variant<LevelDraft*, EditorOperationOutcome> Pick(LevelTarget& level, const JsonValue& arguments)
    {
        const JsonValue* named = arguments.Find("draft");
        if (named == nullptr || !named->IsString())
        {
            if (LevelDraft* writing = level.WritingDraft())
                return writing;
            return EditorOperationOutcome::Refused("draft.none", "no draft is being written; name one");
        }
        if (LevelDraft* draft = level.FindDraft(named->AsString()))
            return draft;
        return EditorOperationOutcome::Refused("draft.unknown", "there is no draft '" + named->AsString() + "'");
    }

    // A proposal's steps are mutations, checked before any runs: a query or a
    // session operation would act on the editor, not the draft.
    EditorOperationOutcome ReplayStep(EditorOperations& operations, const LevelDraftStep& step)
    {
        const EditorOperationCatalog& catalog = operations.Catalog();
        const EditorOperationDefinition* definition = catalog.Get(catalog.Find(step.Operation));
        if (definition == nullptr)
            return EditorOperationOutcome::Refused("operation.unknown", "there is no operation '" + step.Operation + "'");
        if (definition->Kind != EditorOperationKind::Mutation)
            return EditorOperationOutcome::Refused("proposal.not_level_edit",
                                                   step.Operation + " is not a step a proposal can take");
        const JsonValue answer = operations.Invoke(step.Operation, step.Arguments);
        const auto text = [&answer](std::string_view key) {
            const JsonValue* value = answer.Find(key);
            return value != nullptr && value->IsString() ? value->AsString() : std::string{};
        };
        if (text("status") == "ok")
            return EditorOperationOutcome::Ok({});
        return EditorOperationOutcome::Refused(text("code"), text("message"));
    }

    template <typename Body>
    EditorOperationExecutor WithDraft(LevelTarget& level, Body body)
    {
        return [&level, body](const JsonValue& arguments) {
            auto picked = Pick(level, arguments);
            if (auto* refused = std::get_if<EditorOperationOutcome>(&picked))
                return std::move(*refused);
            return body(*std::get<LevelDraft*>(picked));
        };
    }

    EditorOperationEntry Entry(std::string name, EditorOperationKind kind, std::string description,
                               DataFieldSchema parameters, DataFieldSchema result, EditorOperationExecutor execute)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = std::move(name);
        entry.Definition.Kind = kind;
        entry.Definition.Category = "Draft";
        entry.Definition.Description = std::move(description);
        entry.Definition.Parameters = std::move(parameters);
        entry.Definition.Result = std::move(result);
        entry.Execute = std::move(execute);
        return entry;
    }

    EditorOperationEntry Begin(LevelTarget& level)
    {
        return Entry(
            "draft.begin", EditorOperationKind::Session,
            "Copies the focus level into a new draft. Mutations edit the draft until it is accepted, "
            "discarded, or another draft is written; queries read it too.",
            DataField::Record({}, {}, {}, {
                Text("label", "what the draft proposes, in a few words"),
                OptionalText("rationale", "why"),
                DataField::Optional("id_seed", {}, "what the ids it mints follow from; random when absent",
                                    DataField::Field({}, DataFieldKind::Int, {}, {})),
            }),
            DataField::Record({}, {}, {}, {
                Text("draft", "its id"),
                Text("base", "the copied level's asset path"),
                DataField::Field("id_seed", DataFieldKind::Int, {}, {}),
            }),
            [&level](const JsonValue& arguments) {
                const JsonValue* rationale = arguments.Find("rationale");
                const JsonValue* seed = arguments.Find("id_seed");
                const std::uint32_t idSeed = seed != nullptr && seed->IsNumber()
                    ? static_cast<std::uint32_t>(seed->AsNumber())
                    : std::random_device{}();
                std::string error;
                LevelDraft* draft = level.BeginDraft(arguments.Find("label")->AsString(),
                                                     rationale != nullptr && rationale->IsString()
                                                         ? rationale->AsString()
                                                         : std::string{},
                                                     idSeed, error);
                if (draft == nullptr)
                    return EditorOperationOutcome::Refused("draft.copy_failed", error);
                return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                    { "draft", JsonValue(draft->Id) },
                    { "base", JsonValue(draft->BasePath) },
                    { "id_seed", JsonValue(static_cast<double>(idSeed)) },
                }));
            });
    }

    EditorOperationEntry Write(LevelTarget& level)
    {
        return Entry(
            "draft.write", EditorOperationKind::Session,
            "Makes mutations edit the named draft, or the level itself when no draft is named.",
            DataField::Record({}, {}, {}, { OptionalText("draft", "the draft's id; absent for the level") }),
            DataField::Record({}, {}, {}, {
                DataField::Optional("writing", {}, "the draft now written; null for the level",
                                    DataField::Field({}, DataFieldKind::String, {}, {}), true),
            }),
            [&level](const JsonValue& arguments) {
                const JsonValue* named = arguments.Find("draft");
                if (named == nullptr || !named->IsString())
                {
                    level.WriteTo(nullptr);
                    return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{ { "writing", JsonValue{} } }));
                }
                LevelDraft* draft = level.FindDraft(named->AsString());
                if (draft == nullptr)
                    return EditorOperationOutcome::Refused("draft.unknown",
                                                           "there is no draft '" + named->AsString() + "'");
                level.WriteTo(draft);
                return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{ { "writing", JsonValue(draft->Id) } }));
            });
    }

    EditorOperationEntry PreviewDraft(LevelTarget& level)
    {
        return Entry(
            "draft.preview", EditorOperationKind::Session,
            "Shows the named draft over its level in Kyusu, ghosted, or none when no draft is named.",
            DataField::Record({}, {}, {}, { OptionalText("draft", "the draft's id; absent shows none") }),
            DataField::Record({}, {}, {}, {
                DataField::Optional("previewed", {}, "the draft now shown; null for none",
                                    DataField::Field({}, DataFieldKind::String, {}, {}), true),
            }),
            [&level](const JsonValue& arguments) {
                const JsonValue* named = arguments.Find("draft");
                if (named == nullptr || !named->IsString())
                {
                    level.Preview(nullptr);
                    return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{ { "previewed", JsonValue{} } }));
                }
                LevelDraft* draft = level.FindDraft(named->AsString());
                if (draft == nullptr)
                    return EditorOperationOutcome::Refused("draft.unknown",
                                                           "there is no draft '" + named->AsString() + "'");
                level.Preview(draft);
                return EditorOperationOutcome::Ok(
                    JsonValue(JsonValue::Object{ { "previewed", JsonValue(draft->Id) } }));
            });
    }

    EditorOperationEntry List(LevelTarget& level)
    {
        return Entry(
            "draft.list", EditorOperationKind::Query, "Every open draft, which one is being written and which is shown.",
            DataField::Record({}, {}, {}, {}),
            DataField::Record({}, {}, {}, {
                DataField::ArrayOf("drafts", {}, {},
                    DataField::Record({}, {}, {}, {
                        Text("draft", {}),
                        Text("label", {}),
                        StateField(),
                        DataField::Field("steps", DataFieldKind::Int, {}, {}),
                        DataField::Field("changed", DataFieldKind::Int, {}, "entities created, modified or deleted"),
                        DataField::Field("writing", DataFieldKind::Bool, {}, {}),
                        DataField::Field("previewed", DataFieldKind::Bool, {}, "shown over its level in Kyusu"),
                    }),
                    true),
            }),
            [&level](const JsonValue&) {
                JsonValue::Array drafts;
                for (const auto& draft : level.Drafts())
                    drafts.push_back(JsonValue(JsonValue::Object{
                        { "draft", JsonValue(draft->Id) },
                        { "label", JsonValue(draft->Label) },
                        { "state", JsonValue(StateName(draft->State)) },
                        { "steps", JsonValue(static_cast<double>(draft->Steps.size())) },
                        { "changed", JsonValue(static_cast<double>(draft->Changes.Changes.size())) },
                        { "writing", JsonValue(draft.get() == level.WritingDraft()) },
                        { "previewed", JsonValue(draft.get() == level.PreviewedDraft()) },
                    }));
                return EditorOperationOutcome::Ok(
                    JsonValue(JsonValue::Object{ { "drafts", JsonValue(std::move(drafts)) } }));
            });
    }

    EditorOperationEntry Diff(LevelTarget& level)
    {
        DataFieldSchema result = ChangeSummarySchema({
            Text("draft", {}),
            Text("label", {}),
            Text("rationale", {}),
            Text("base", "the copied level's asset path"),
            StateField(),
            FailureField(),
            DataField::ArrayOf("steps", {}, {},
                DataField::Record({}, {}, {}, {
                    Text("operation", {}),
                    Text("arguments", "as JSON"),
                }),
                true),
        });
        return Entry(
            "draft.diff", EditorOperationKind::Query,
            "What a draft changes against the level it copies, and the steps that changed it.",
            DraftOnly(), std::move(result),
            WithDraft(level, [](const LevelDraft& draft) {
                JsonValue::Array steps;
                for (const LevelDraftStep& step : draft.Steps)
                    steps.push_back(JsonValue(JsonValue::Object{
                        { "operation", JsonValue(step.Operation) },
                        { "arguments", JsonValue(JsonStringify(step.Arguments)) },
                    }));
                JsonValue::Object data = ChangeSummary(draft.Changes);
                data.emplace_back("draft", JsonValue(draft.Id));
                data.emplace_back("label", JsonValue(draft.Label));
                data.emplace_back("rationale", JsonValue(draft.Rationale));
                data.emplace_back("base", JsonValue(draft.BasePath));
                data.emplace_back("state", JsonValue(StateName(draft.State)));
                data.emplace_back("failure", FailureJson(draft));
                data.emplace_back("steps", JsonValue(std::move(steps)));
                return EditorOperationOutcome::Ok(JsonValue(std::move(data)));
            }));
    }

    JsonValue DraftEvent(std::string_view event, const LevelDraft& draft)
    {
        return JsonValue(JsonValue::Object{
            { "event", JsonValue(std::string(event)) },
            { "draft", JsonValue(draft.Id) },
            { "label", JsonValue(draft.Label) },
        });
    }

    EditorOperationEntry Accept(LevelTarget& level, EditorOperations& operations)
    {
        return Entry(
            "draft.accept", EditorOperationKind::Session,
            "Applies a draft to the level it copies as one undoable step, and drops it. Refused when "
            "the level has changed under any entity the draft changes; rebase it then.",
            DraftOnly(), MutationResultSchema(),
            WithDraft(level, [&level, &operations](LevelDraft& draft) {
                const JsonValue event = DraftEvent("draft.accepted", draft);
                EditorOperationOutcome outcome = level.AcceptDraft(draft);
                if (outcome.Status == EditorOperationStatus::Ok)
                    operations.Emit(event);
                return outcome;
            }));
    }

    EditorOperationEntry Discard(LevelTarget& level, EditorOperations& operations)
    {
        return Entry(
            "draft.discard", EditorOperationKind::Session, "Drops a draft; the level never saw it.",
            DraftOnly(), DataField::Record({}, {}, {}, { Text("draft", "the dropped draft") }),
            WithDraft(level, [&level, &operations](LevelDraft& draft) {
                JsonValue data(JsonValue::Object{ { "draft", JsonValue(draft.Id) } });
                const JsonValue event = DraftEvent("draft.discarded", draft);
                level.DiscardDraft(draft);
                operations.Emit(event);
                return EditorOperationOutcome::Ok(std::move(data));
            }));
    }

    EditorOperationEntry Resume(LevelTarget& level)
    {
        return Entry(
            "draft.resume", EditorOperationKind::Session,
            "Lets a failed draft take steps again, from what the steps before the refused one made. Refused "
            "for a proposal whose replay stopped short; rebase or discard that.",
            DraftOnly(), DataField::Record({}, {}, {}, { Text("draft", {}) }),
            WithDraft(level, [&level](LevelDraft& draft) { return level.ResumeDraft(draft); }));
    }

    EditorOperationEntry Rebase(LevelTarget& level, EditorOperations& operations)
    {
        return Entry(
            "draft.rebase", EditorOperationKind::Session,
            "Replays a draft's steps on a fresh copy of its level, taking in everything that changed "
            "there since, and the steps of a proposal whose replay stopped short. Refused, leaving the "
            "draft as it was, when a step does not apply.",
            DraftOnly(), ChangeSummarySchema(),
            WithDraft(level, [&level, &operations](LevelDraft& draft) {
                return level.RebaseDraft(draft, [&operations](const LevelDraftStep& step) {
                    return ReplayStep(operations, step);
                });
            }));
    }

    EditorOperationEntry Save(LevelTarget& level, const EditorOperations& operations)
    {
        return Entry(
            "draft.save", EditorOperationKind::Session,
            "Writes a draft as a proposal file: its steps, the level they apply to, what the entities "
            "they change or rely on looked like, and the operations' contracts, for a designer to open "
            "and review.",
            DataField::Record({}, {}, {}, {
                WhichDraft(),
                Text("path", "the file to write, by convention <project>/.proposals/<name>.sproposal"),
                OptionalText("author", "who proposes it"),
            }),
            DataField::Record({}, {}, {}, {
                Text("draft", {}),
                Text("path", {}),
                DataField::Field("steps", DataFieldKind::Int, {}, {}),
            }),
            [&level, &operations](const JsonValue& arguments) {
                auto picked = Pick(level, arguments);
                if (auto* refused = std::get_if<EditorOperationOutcome>(&picked))
                    return std::move(*refused);
                const LevelDraft& draft = *std::get<LevelDraft*>(picked);
                if (!draft.Unreplayed.empty())
                    return EditorOperationOutcome::Refused(
                        "draft.incomplete", "draft " + draft.Id + " holds only part of the proposal it replayed");
                const JsonValue* author = arguments.Find("author");
                const std::string text = JsonStringify(
                    LevelProposalToJson(ProposalOf(draft,
                                                   author != nullptr && author->IsString() ? author->AsString() : std::string{},
                                                   operations.Catalog())),
                    true);
                const std::string& path = arguments.Find("path")->AsString();
                if (!WriteFileBytes(path, std::as_bytes(std::span(text))))
                    return EditorOperationOutcome::Refused("proposal.not_written", "could not write " + path);
                return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                    { "draft", JsonValue(draft.Id) },
                    { "path", JsonValue(path) },
                    { "steps", JsonValue(static_cast<double>(draft.Steps.size())) },
                }));
            });
    }

    // The operations whose contract is no longer the one the proposal was written against.
    JsonValue::Array ChangedOperations(const EditorOperationCatalog& catalog, const LevelProposal& proposal)
    {
        JsonValue::Array changed;
        for (const auto& [operation, digest] : proposal.Contracts)
            if (ContractDigest(catalog, operation) != digest)
                changed.push_back(JsonValue(operation));
        return changed;
    }

    // The entities whose record on `level` is no longer the one the proposal saw.
    JsonValue::Array MovedSince(const EditorDocument& level, const LevelProposal& proposal)
    {
        JsonValue::Array moved;
        for (const auto& [id, digest] : proposal.BaseDigests)
            if (CurrentDigest(level, id) != digest)
                moved.push_back(JsonValue(PersistentEntityIdToString(id)));
        return moved;
    }

    EditorOperationEntry Open(LevelTarget& level, EditorOperations& operations)
    {
        return Entry(
            "draft.open", EditorOperationKind::Session,
            "Replays a proposal file into a new draft of the focus level, which must be the level it "
            "was written for, and names the entities that level has changed since.",
            DataField::Record({}, {}, {}, { Text("path", "the proposal file") }),
            ChangeSummarySchema({
                Text("draft", {}),
                Text("label", {}),
                Text("rationale", {}),
                Text("author", {}),
                StateField(),
                FailureField(),
                DataField::ArrayOf("moved_since", {}, "entities the level changed since it was written",
                                   DataField::Field({}, DataFieldKind::Entity, {}, {}), true),
                DataField::ArrayOf("changed_operations", {},
                                   "operations whose contract changed since it was written; their steps may replay differently",
                                   DataField::Field({}, DataFieldKind::String, {}, {}), true),
            }),
            [&level, &operations](const JsonValue& arguments) {
                const std::string& path = arguments.Find("path")->AsString();
                const std::optional<JsonValue> json = JsonParseFile(path);
                if (!json)
                    return EditorOperationOutcome::Refused("proposal.unreadable", "could not read " + path);
                std::string error;
                const std::optional<LevelProposal> proposal = LevelProposalFromJson(*json, error);
                if (!proposal)
                    return EditorOperationOutcome::Refused("proposal.malformed", path + ": " + error);
                const std::string focus = level.FocusLevel().SourceAssetPath();
                if (proposal->BasePath != focus)
                    return EditorOperationOutcome::Refused(
                        "proposal.wrong_level", "the proposal is for '" + proposal->BasePath + "', not '" + focus + "'");

                const JsonValue::Array moved = MovedSince(level.FocusLevel(), *proposal);
                LevelDraft* draft = level.BeginDraft(proposal->Label, proposal->Rationale, proposal->IdSeed, error);
                if (draft == nullptr)
                    return EditorOperationOutcome::Refused("draft.copy_failed", error);
                draft->OpenedFrom = path;
                (void)level.ReplayInto(*draft, proposal->Steps, [&operations](const LevelDraftStep& step) {
                    return ReplayStep(operations, step);
                });

                JsonValue::Object data = ChangeSummary(draft->Changes);
                data.emplace_back("draft", JsonValue(draft->Id));
                data.emplace_back("label", JsonValue(draft->Label));
                data.emplace_back("rationale", JsonValue(draft->Rationale));
                data.emplace_back("author", JsonValue(proposal->Author));
                data.emplace_back("state", JsonValue(StateName(draft->State)));
                data.emplace_back("failure", FailureJson(*draft));
                data.emplace_back("moved_since", JsonValue(moved));
                data.emplace_back("changed_operations", JsonValue(ChangedOperations(operations.Catalog(), *proposal)));
                return EditorOperationOutcome::Ok(JsonValue(std::move(data)));
            });
    }
}

std::vector<EditorOperationEntry> LevelDraftOperations(LevelTarget& level, EditorOperations& operations)
{
    std::vector<EditorOperationEntry> entries;
    entries.push_back(Begin(level));
    entries.push_back(Write(level));
    entries.push_back(PreviewDraft(level));
    entries.push_back(List(level));
    entries.push_back(Diff(level));
    entries.push_back(Accept(level, operations));
    entries.push_back(Discard(level, operations));
    entries.push_back(Resume(level));
    entries.push_back(Rebase(level, operations));
    entries.push_back(Save(level, operations));
    entries.push_back(Open(level, operations));
    return entries;
}
