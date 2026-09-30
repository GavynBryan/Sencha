#include "prompts/DocumentPrompts.h"

#include "problems/ProblemList.h"

#include <algorithm>
#include <format>
#include <utility>

namespace
{
std::string LabelOf(const DocumentRef& document)
{
    return document.Source != nullptr ? document.Source->DocumentLabel(document.Key) : document.Key;
}

std::vector<std::string> LabelsOf(const std::vector<DocumentRef>& documents)
{
    std::vector<std::string> labels;
    labels.reserve(documents.size());
    for (const DocumentRef& document : documents)
        labels.push_back(LabelOf(document));
    return labels;
}

constexpr std::string_view kSaveSource = "save";
} // namespace

Engine::ExitDecision DecideDocumentExit(const DocumentSourceSet& sources)
{
    return sources.ChangedDocuments().empty() ? Engine::ExitDecision::Allow : Engine::ExitDecision::Defer;
}

void AskAboutChanges(PromptQueue::Asker& asker, bool hasChanges, const std::string& documentName,
                     std::function<void(DirtyDisposition)> proceed)
{
    if (!hasChanges)
    {
        proceed(DirtyDisposition::Refuse);
        return;
    }
    asker.Ask({
        .Title = "Unsaved changes",
        .Message = std::format("{} has unsaved changes.", documentName),
        .Choices = { { "save", "Save" }, { "discard", "Discard", ActionTone::Destructive }, { "cancel", "Cancel" } },
        .Answer =
            [proceed = std::move(proceed)](std::string_view key) {
                if (key == "save")
                    proceed(DirtyDisposition::Save);
                else if (key == "discard")
                    proceed(DirtyDisposition::Discard);
            },
    });
}

DocumentExitHold::DocumentExitHold(Engine& engine, DocumentSourceSet& sources, PromptQueue& prompts)
    : EngineRef(engine)
    , Sources(sources)
    , Asker(prompts.MakeAsker())
{
    // Asked from the request itself: the engine coalesces further requests
    // into the pending one, so this runs once per question.
    EngineRef.OnExitRequested = [this](Engine::ExitSource) {
        const Engine::ExitDecision decision = DecideDocumentExit(Sources);
        if (decision == Engine::ExitDecision::Defer)
            Ask();
        return decision;
    };
}

DocumentExitHold::~DocumentExitHold()
{
    EngineRef.OnExitRequested = {};
    if (EngineRef.IsExitPending())
        EngineRef.CancelExit();
}

void DocumentExitHold::Ask()
{
    Asker.Ask({
        .Title = "Unsaved documents",
        .Message = "Save changes before closing?",
        .Items = LabelsOf(Sources.ChangedDocuments()),
        .Choices = { { "save", "Save all and close" },
                     { "discard", "Discard and close", ActionTone::Destructive },
                     { "cancel", "Keep editing" } },
        .Answer =
            [this](std::string_view key) {
                if (key == "save")
                    (void)Sources.SaveAll();
                else if (key == "discard")
                    // Thrown away here, deliberately, so nothing is destroyed
                    // later still holding changes.
                    Sources.DiscardAll();
                // A save that left something unsaved keeps the editor open; the
                // save's follow-up says what and why.
                if (key != "cancel" && DecideDocumentExit(Sources) == Engine::ExitDecision::Allow)
                    EngineRef.ConfirmExit();
                else
                    EngineRef.CancelExit();
            },
    });
}

DocumentSaveFollowUp::DocumentSaveFollowUp(DocumentSourceSet& sources, ProblemList& problems, PromptQueue& prompts)
    : Sources(sources)
    , Problems(problems)
    , Asker(prompts.MakeAsker())
{
    Subscription = Sources.SubscribeReport([this](const DocumentSaveResult* saved) { ReportChanged(saved); });
}

void DocumentSaveFollowUp::ReportChanged(const DocumentSaveResult* saved)
{
    // A failed settle stands until its conflict is gone or saved over.
    std::erase_if(SettleFailures, [&](const SettleFailure& failure) {
        const DocumentSaveResult* result = Sources.LastSave().Find(failure.Document);
        return result == nullptr || result->Status != DocumentSaveStatus::Conflict
            || (saved != nullptr && saved->Document == failure.Document);
    });

    std::vector<Problem> problems;
    for (const SettleFailure& failure : SettleFailures)
        problems.push_back({ ProblemSeverity::Error, failure.Error, {}, failure.Document });
    for (const DocumentSaveResult& result : Sources.LastSave().Results)
    {
        const std::string label = LabelOf(result.Document);
        switch (result.Status)
        {
        case DocumentSaveStatus::Saved:
            break;
        case DocumentSaveStatus::SavedWithProblems:
            problems.push_back({ ProblemSeverity::Warning,
                                 std::format("{} was saved with problems a game will refuse to load.", label), {},
                                 result.Document });
            break;
        case DocumentSaveStatus::Conflict:
            problems.push_back({ ProblemSeverity::Warning,
                                 std::format("{} changed on disk since it was read; save it again to decide.", label),
                                 {}, result.Document });
            break;
        case DocumentSaveStatus::Failed:
            problems.push_back({ ProblemSeverity::Error, std::format("{} was not saved: {}", label, result.Error), {},
                                 result.Document });
            break;
        }
    }
    Problems.Publish(kSaveSource, std::move(problems));

    if (saved != nullptr && saved->Status == DocumentSaveStatus::Conflict
        && std::ranges::find(Asking, saved->Document) == Asking.end())
        AskToSettle(saved->Document);
}

void DocumentSaveFollowUp::AskToSettle(const DocumentRef& document)
{
    Asking.push_back(document);
    Asker.Ask({
        .Title = "Changed on disk",
        .Message = std::format("{} changed on disk since it was read.", LabelOf(document)),
        .Choices = { { "keep", "Keep mine" },
                     { "take", "Take the file's", ActionTone::Destructive },
                     { "cancel", "Decide later" } },
        .Answer =
            [this, document](std::string_view key) {
                std::erase(Asking, document);
                if (key == "cancel")
                    return;
                std::string error;
                if (!Sources.Settle(document, key == "keep" ? ConflictChoice::KeepMine : ConflictChoice::TakeFile,
                                    error))
                {
                    SettleFailures.push_back({ document, std::move(error) });
                    ReportChanged(nullptr);
                }
            },
    });
}
