#pragma once

#include "documents/DocumentRef.h"
#include "documents/DocumentSaveReport.h"

#include <filesystem>
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

// What an open document did about its file changing on disk: took the new
// version (a clean document, or its own write), or held its own changes, which
// its next save reports as a conflict.
enum class ExternalChange
{
    NotOpen,
    Adopted,
    Held,
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

    // `file` changed on disk; a source whose documents do not live in files
    // that are watched never has one open.
    [[nodiscard]] virtual ExternalChange FileChangedOnDisk(const std::filesystem::path&)
    {
        return ExternalChange::NotOpen;
    }

    // What a person reading a prompt or a save report calls the document.
    [[nodiscard]] virtual std::string DocumentLabel(std::string_view key) const { return std::string(key); }
};
