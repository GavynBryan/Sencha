#include "LevelDrafts.h"

#include "LevelTarget.h"

#include <core/json/JsonStringify.h>
#include <core/metadata/DataSchemaFields.h>

#include <random>
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
            "draft.begin", EditorOperationKind::Mutation,
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
            "draft.write", EditorOperationKind::Mutation,
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

    EditorOperationEntry List(LevelTarget& level)
    {
        return Entry(
            "draft.list", EditorOperationKind::Query, "Every open draft, and which one is being written.",
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
            DataField::Optional("failure", {}, "the refused step",
                                DataField::Field({}, DataFieldKind::String, {}, {}), true),
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
                data.emplace_back("failure",
                                  draft.State == LevelDraftState::Failed ? JsonValue(draft.Failure) : JsonValue{});
                data.emplace_back("steps", JsonValue(std::move(steps)));
                return EditorOperationOutcome::Ok(JsonValue(std::move(data)));
            }));
    }

    EditorOperationEntry Accept(LevelTarget& level)
    {
        return Entry(
            "draft.accept", EditorOperationKind::Mutation,
            "Applies a draft to the level it copies as one undoable step, and drops it. Refused when "
            "the level has changed under any entity the draft changes; rebase it then.",
            DraftOnly(), MutationResultSchema(),
            WithDraft(level, [&level](LevelDraft& draft) { return level.AcceptDraft(draft); }));
    }

    EditorOperationEntry Discard(LevelTarget& level)
    {
        return Entry(
            "draft.discard", EditorOperationKind::Mutation, "Drops a draft; the level never saw it.",
            DraftOnly(), DataField::Record({}, {}, {}, { Text("draft", "the dropped draft") }),
            WithDraft(level, [&level](LevelDraft& draft) {
                JsonValue data(JsonValue::Object{ { "draft", JsonValue(draft.Id) } });
                level.DiscardDraft(draft);
                return EditorOperationOutcome::Ok(std::move(data));
            }));
    }

    EditorOperationEntry Resume(LevelTarget& level)
    {
        return Entry(
            "draft.resume", EditorOperationKind::Mutation,
            "Lets a failed draft take steps again, from what the steps before the refused one made.",
            DraftOnly(), DataField::Record({}, {}, {}, { Text("draft", {}) }),
            WithDraft(level, [](LevelDraft& draft) {
                draft.State = LevelDraftState::Open;
                draft.Failure.clear();
                return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{ { "draft", JsonValue(draft.Id) } }));
            }));
    }

    EditorOperationEntry Rebase(LevelTarget& level, EditorOperations& operations)
    {
        return Entry(
            "draft.rebase", EditorOperationKind::Mutation,
            "Replays a draft's steps on a fresh copy of its level, taking in everything that changed "
            "there since. Refused, leaving the draft as it was, when a step no longer applies.",
            DraftOnly(), ChangeSummarySchema(),
            WithDraft(level, [&level, &operations](LevelDraft& draft) {
                return level.RebaseDraft(draft, [&operations](const LevelDraftStep& step) {
                    const JsonValue answer = operations.Invoke(step.Operation, step.Arguments);
                    const JsonValue* status = answer.Find("status");
                    if (status != nullptr && status->IsString() && status->AsString() == "ok")
                        return EditorOperationOutcome::Ok({});
                    const JsonValue* code = answer.Find("code");
                    const JsonValue* message = answer.Find("message");
                    return EditorOperationOutcome::Refused(
                        code != nullptr && code->IsString() ? code->AsString() : "operation.failed",
                        message != nullptr && message->IsString() ? message->AsString() : JsonStringify(answer));
                });
            }));
    }
}

std::vector<EditorOperationEntry> LevelDraftOperations(LevelTarget& level, EditorOperations& operations)
{
    std::vector<EditorOperationEntry> entries;
    entries.push_back(Begin(level));
    entries.push_back(Write(level));
    entries.push_back(List(level));
    entries.push_back(Diff(level));
    entries.push_back(Accept(level));
    entries.push_back(Discard(level));
    entries.push_back(Resume(level));
    entries.push_back(Rebase(level, operations));
    return entries;
}
