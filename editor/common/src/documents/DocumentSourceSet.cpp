#include "documents/DocumentSourceSet.h"

#include <algorithm>
#include <cassert>

void DocumentSourceSet::AddSource(DocumentSource& source)
{
    if (std::ranges::find(Sources, &source) == Sources.end())
        Sources.push_back(&source);
}

void DocumentSourceSet::RemoveSource(DocumentSource& source)
{
    EraseSteps([&](const DocumentRef& step) { return step.Source == &source; });
    Report.RemoveSource(source);
    std::erase(Sources, &source);
}

void DocumentSourceSet::Record(DocumentRef document)
{
    assert(!Stepping && "a document recorded a new step while the journal was retaking one");
    Steps.resize(Cursor);
    Steps.push_back(std::move(document));
    ++Cursor;
}

void DocumentSourceSet::ForgetDocument(const DocumentRef& document)
{
    EraseSteps([&](const DocumentRef& step) { return step == document; });
    Report.Remove(document);
}

void DocumentSourceSet::Undo()
{
    CancelEdits();
    if (CanUndo())
    {
        --Cursor;
        Step(DocumentStep::Undo);
    }
}

void DocumentSourceSet::Redo()
{
    CancelEdits();
    if (CanRedo())
    {
        Step(DocumentStep::Redo);
        ++Cursor;
    }
}

void DocumentSourceSet::CancelEdits()
{
    for (DocumentSource* source : Sources)
        source->CancelDocumentEdits();
}

const DocumentSaveReport& DocumentSourceSet::SaveAll()
{
    for (const DocumentRef& document : ChangedDocuments())
        (void)Save(document);
    return Report;
}

DocumentSaveResult DocumentSourceSet::Save(const DocumentRef& document)
{
    DocumentSaveResult result = document.Source->SaveDocument(document.Key);
    result.Document = document;
    Report.Add(result);
    return result;
}

bool DocumentSourceSet::Settle(const DocumentRef& document, ConflictChoice choice, std::string& error)
{
    if (!document.Source->SettleDocument(document.Key, choice, error))
        return false;
    Report.Remove(document);
    return true;
}

std::vector<DocumentRef> DocumentSourceSet::ChangedDocuments() const
{
    std::vector<DocumentRef> changed;
    for (DocumentSource* source : Sources)
        source->AppendChangedDocuments(changed);
    return changed;
}

void DocumentSourceSet::Step(DocumentStep step)
{
    const DocumentRef& document = Steps[Cursor];
    Stepping = true;
    document.Source->StepDocument(document.Key, step);
    Stepping = false;
}

template <typename Predicate>
void DocumentSourceSet::EraseSteps(Predicate&& matches)
{
    const auto taken = static_cast<std::ptrdiff_t>(Cursor);
    Cursor -= static_cast<std::size_t>(std::count_if(Steps.begin(), Steps.begin() + taken, matches));
    std::erase_if(Steps, matches);
}
