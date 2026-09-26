#pragma once

#include "documents/DocumentRef.h"
#include "documents/DocumentSaveReport.h"
#include "documents/DocumentSource.h"

#include <cstddef>
#include <string>
#include <vector>

// One undo journal and one save-all across every kind of open document. A source
// registers itself and removes itself before it is destroyed.
class DocumentSourceSet
{
public:
    DocumentSourceSet() = default;
    DocumentSourceSet(const DocumentSourceSet&) = delete;
    DocumentSourceSet& operator=(const DocumentSourceSet&) = delete;
    DocumentSourceSet(DocumentSourceSet&&) = delete;
    DocumentSourceSet& operator=(DocumentSourceSet&&) = delete;

    void AddSource(DocumentSource& source);
    void RemoveSource(DocumentSource& source);

    // A document took a new step on its own history.
    void Record(DocumentRef document);
    // The document's history is gone: it closed, reloaded or took the file's version.
    void ForgetDocument(const DocumentRef& document);

    // Open edits are cancelled before the newest step is retaken.
    void Undo();
    void Redo();
    [[nodiscard]] bool CanUndo() const { return Cursor > 0; }
    [[nodiscard]] bool CanRedo() const { return Cursor < Steps.size(); }
    void CancelEdits();

    const DocumentSaveReport& SaveAll();
    DocumentSaveResult Save(const DocumentRef& document);
    bool Settle(const DocumentRef& document, ConflictChoice choice, std::string& error);
    [[nodiscard]] const DocumentSaveReport& LastSave() const { return Report; }
    [[nodiscard]] std::vector<DocumentRef> ChangedDocuments() const;

private:
    void Step(DocumentStep step);
    template <typename Predicate>
    void EraseSteps(Predicate&& matches);

    std::vector<DocumentSource*> Sources;
    std::vector<DocumentRef> Steps;
    // Steps before the cursor are taken; those after it are redoable.
    std::size_t Cursor = 0;
    bool Stepping = false;
    DocumentSaveReport Report;
};
