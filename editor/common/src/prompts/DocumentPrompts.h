#pragma once

#include "documents/DocumentRef.h"
#include "documents/DocumentSource.h"
#include "documents/DocumentSourceSet.h"
#include "prompts/PromptQueue.h"

#include <app/Engine.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class ProblemList;

// Exit waits while any open document has changes, an open edit included.
[[nodiscard]] Engine::ExitDecision DecideDocumentExit(const DocumentSourceSet& sources);

// Asks Save, Discard or Cancel before an operation drops a document's changes.
// Proceeds at once, refusing nothing, when the document has none.
void AskAboutChanges(PromptQueue::Asker& asker, bool hasChanges, const std::string& documentName,
                     std::function<void(DirtyDisposition)> proceed);

//=============================================================================
// DocumentExitHold
//
// Holds the editor's exit behind a question while any document has changes:
// save them all, throw them away, or keep editing. Installed on the engine for
// as long as it lives.
//=============================================================================
class DocumentExitHold
{
public:
    DocumentExitHold(Engine& engine, DocumentSourceSet& sources, PromptQueue& prompts);
    ~DocumentExitHold();

    DocumentExitHold(const DocumentExitHold&) = delete;
    DocumentExitHold& operator=(const DocumentExitHold&) = delete;

private:
    void Ask();

    Engine& EngineRef;
    DocumentSourceSet& Sources;
    PromptQueue::Asker Asker;
};

//=============================================================================
// DocumentSaveFollowUp
//
// What follows a save, whoever made it: a file that could not be written, or
// was written with problems, stays on the problem list until a later save or
// its closing clears it, and a file that changed on disk under its document is
// a question -- keep mine, or take the file's.
//=============================================================================
class DocumentSaveFollowUp
{
public:
    DocumentSaveFollowUp(DocumentSourceSet& sources, ProblemList& problems, PromptQueue& prompts);

    DocumentSaveFollowUp(const DocumentSaveFollowUp&) = delete;
    DocumentSaveFollowUp& operator=(const DocumentSaveFollowUp&) = delete;

private:
    void ReportChanged(const DocumentSaveResult* saved);
    void AskToSettle(const DocumentRef& document);

    DocumentSourceSet& Sources;
    ProblemList& Problems;
    struct SettleFailure
    {
        DocumentRef Document;
        std::string Error;
    };

    PromptQueue::Asker Asker;
    std::vector<DocumentRef> Asking;
    std::vector<SettleFailure> SettleFailures;
    std::shared_ptr<DocumentSourceSet::ReportObserver::Fn> Subscription;
};
