#pragma once

#include "documents/DocumentRef.h"
#include "documents/DocumentSource.h"

#include <app/Engine.h>

#include <functional>
#include <optional>
#include <string>

class DocumentSourceSet;
class EditorUiFeature;

// Exit waits while any open document has changes, an open edit included.
[[nodiscard]] Engine::ExitDecision DecideDocumentExit(const DocumentSourceSet& sources);

// Undo, redo and save all on the shell, and a prompt that holds exit until
// every change is saved or deliberately discarded. The host clears
// Engine::OnExitRequested before `sources` is destroyed.
void InstallDocumentShellActions(EditorUiFeature& ui, Engine& engine, DocumentSourceSet& sources);

// Asks Save, Discard or Cancel before an operation would drop a document's changes.
class UnsavedDocumentPrompt
{
public:
    // Proceeds at once, refusing nothing, when the document has no changes.
    void Ask(bool hasChanges, std::string documentName, std::function<void(DirtyDisposition)> proceed);
    void Draw();

private:
    std::string DocumentName;
    std::function<void(DirtyDisposition)> Proceed;
    bool Asking = false;
};
