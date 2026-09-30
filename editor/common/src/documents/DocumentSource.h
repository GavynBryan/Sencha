#pragma once

#include "documents/DocumentRef.h"
#include "documents/DocumentSaveReport.h"

#include <string>
#include <string_view>
#include <vector>

enum class DocumentStep
{
    Undo,
    Redo,
};

// What an operation that would drop a document's changes does with them.
enum class DirtyDisposition
{
    Refuse,
    Save,
    Discard,
};

enum class ConflictChoice
{
    KeepMine,
    TakeFile,
};

// One kind of open document, as the cross-document journal and save-all see it.
// Implementations notify their own observers and bring a stepped document forward.
class DocumentSource
{
public:
    virtual ~DocumentSource() = default;

    // Documents whose committed version differs from the file, or that hold an open edit.
    virtual void AppendChangedDocuments(std::vector<DocumentRef>& out) = 0;
    [[nodiscard]] virtual DocumentSaveResult SaveDocument(std::string_view key) = 0;
    [[nodiscard]] virtual bool SettleDocument(std::string_view key, ConflictChoice choice, std::string& error) = 0;
    virtual void StepDocument(std::string_view key, DocumentStep step) = 0;
    virtual void CancelDocumentEdits() = 0;
    // Throws the document's changes away and closes it.
    virtual void DiscardDocument(std::string_view key) = 0;

    // What a person reading a prompt or a save report calls the document.
    [[nodiscard]] virtual std::string DocumentLabel(std::string_view key) const { return std::string(key); }
};
