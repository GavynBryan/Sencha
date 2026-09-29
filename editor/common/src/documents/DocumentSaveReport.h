#pragma once

#include "documents/DocumentRef.h"

#include <string>
#include <vector>

enum class DocumentSaveStatus
{
    Saved,
    // Written, but with problems a game would refuse to load.
    SavedWithProblems,
    // Changed on disk since read; left for the author to settle.
    Conflict,
    Failed,
};

struct DocumentSaveResult
{
    DocumentRef Document;
    DocumentSaveStatus Status = DocumentSaveStatus::Failed;
    std::string Error;
};

struct DocumentSaveReport
{
    std::vector<DocumentSaveResult> Results;

    // A later result for the same document replaces the earlier one.
    void Add(DocumentSaveResult result);
    void Merge(const DocumentSaveReport& later);
    void Remove(const DocumentRef& document);
    void RemoveSource(const DocumentSource& source);
    [[nodiscard]] std::vector<const DocumentSaveResult*> WithStatus(DocumentSaveStatus status) const;
    [[nodiscard]] const DocumentSaveResult* Find(const DocumentRef& document) const;
};
