#include "documents/DocumentSaveReport.h"

#include <algorithm>

void DocumentSaveReport::Add(DocumentSaveResult result)
{
    Remove(result.Document);
    Results.push_back(std::move(result));
}

void DocumentSaveReport::Merge(const DocumentSaveReport& later)
{
    for (const DocumentSaveResult& result : later.Results)
        Add(result);
}

void DocumentSaveReport::Remove(const DocumentRef& document)
{
    std::erase_if(Results, [&](const DocumentSaveResult& result) { return result.Document == document; });
}

void DocumentSaveReport::RemoveSource(const DocumentSource& source)
{
    std::erase_if(Results, [&](const DocumentSaveResult& result) { return result.Document.Source == &source; });
}

std::vector<const DocumentSaveResult*> DocumentSaveReport::WithStatus(DocumentSaveStatus status) const
{
    std::vector<const DocumentSaveResult*> matching;
    for (const DocumentSaveResult& result : Results)
        if (result.Status == status)
            matching.push_back(&result);
    return matching;
}

const DocumentSaveResult* DocumentSaveReport::Find(const DocumentRef& document) const
{
    const auto found = std::ranges::find(Results, document, &DocumentSaveResult::Document);
    return found == Results.end() ? nullptr : &*found;
}
