#include "data/DataDocumentSheet.h"

#include "data/DataDocument.h"
#include "project/ProjectFileSteps.h"
#include "prompts/DocumentPrompts.h"
#include "text/PathLeaf.h"

#include <format>
#include <utility>

DataDocumentSheet::DataDocumentSheet(DataDocumentSet& documents, PromptQueue& prompts,
                                     const DataSubtypeAuthoringSetup& authorings, ProjectFileSteps* files)
    : Set(documents)
    , Prompts(prompts.MakeAsker())
    , Properties(documents, [this] { return Set.Active(); }, &Authoring)
    , FilePath(
          [this] {
              const DataDocument* document = Set.Active();
              return document != nullptr ? document->VirtualPath() : std::string{};
          },
          [this](const std::string& to) {
              // The open document follows its file, changes and history and all.
              if (const DataDocument* document = Set.Active(); document != nullptr && Files != nullptr)
                  OperationError = Files->Move(document->VirtualPath(), to);
          })
    , Files(files)
{
    if (authorings)
        authorings(Authoring);
}

IDataSubtypeAuthoring* DataDocumentSheet::ActiveAuthoring(const DataDocument*& document)
{
    document = Set.Active();
    return document != nullptr ? Authoring.Find(document->Subtype()) : nullptr;
}

void DataDocumentSheet::Update()
{
    const DataDocument* document = nullptr;
    IDataSubtypeAuthoring* authoring = ActiveAuthoring(document);
    if (authoring == nullptr)
        return;
    authoring->Update(*document, Set);
    // A control pressed while listening, written as the author would.
    if (std::optional<std::pair<std::string, std::string>> write = authoring->TakeWrite(*document))
        if (Properties.BeginEdit(write->first))
            (void)Properties.CommitEdit(write->first, write->second);
}

bool DataDocumentSheet::ClaimPlatformEvent(const SDL_Event& event)
{
    // A control being listened for is claimed outright, or the same keystroke
    // also lands in whatever has focus.
    const DataDocument* document = nullptr;
    IDataSubtypeAuthoring* authoring = ActiveAuthoring(document);
    return authoring != nullptr && authoring->HandlePlatformEvent(event);
}

std::vector<PropertySection> DataDocumentSheet::Sections(IPropertySource* fields)
{
    const DataDocument* document = nullptr;
    IDataSubtypeAuthoring* authoring = ActiveAuthoring(document);
    if (document == nullptr)
        return {};
    std::vector<PropertySection> sections{ { .Heading = {}, .Source = &FilePath },
                                           { .Heading = {}, .Source = fields != nullptr ? fields : &Properties } };
    if (authoring != nullptr)
        for (const PropertySection& section : authoring->Sections(*document))
            sections.push_back(section);
    return sections;
}

void DataDocumentSheet::AppendOpen(WorkspaceDocumentFacet& facet) const
{
    for (const DataDocument* document : Set.Documents())
        facet.Open.push_back({ .Key = document->VirtualPath(),
                               .Label = std::string(PathLeaf(document->VirtualPath())),
                               .Changed = document->IsDirty() });
}

std::optional<WorkspaceOpenDocument> DataDocumentSheet::ActiveEntry() const
{
    const DataDocument* active = Set.Active();
    if (active == nullptr)
        return std::nullopt;
    return WorkspaceOpenDocument{ .Key = active->VirtualPath(),
                                  .Label = std::string(PathLeaf(active->VirtualPath())),
                                  .Changed = active->IsDirty() };
}

std::optional<DocumentRef> DataDocumentSheet::ActiveRef()
{
    const DataDocument* active = Set.Active();
    return active != nullptr ? std::optional(Set.RefOf(*active)) : std::nullopt;
}

bool DataDocumentSheet::Owns(const DocumentRef& document) const
{
    return document.Source == &Set.Store() && Set.IndexOf(document.Key).has_value();
}

bool DataDocumentSheet::Show(std::string_view key)
{
    const std::optional<std::size_t> index = Set.IndexOf(key);
    if (index)
        Set.SetActive(*index);
    return index.has_value();
}

bool DataDocumentSheet::Open(std::string_view path)
{
    OperationError.clear();
    return Set.OpenOrFocus(path, OperationError) != nullptr;
}

void DataDocumentSheet::CloseActive(std::function<void(const std::string& path)> closed)
{
    const DataDocument* document = Set.Active();
    if (document == nullptr)
        return;
    // By path: the documents may move while the question is open.
    const std::string path = document->VirtualPath();
    std::vector<DocumentRef> changed;
    if (document->IsDirty() || document->IsEditing())
        changed.push_back(Set.RefOf(*document));
    AskAboutDocuments(Prompts, "Close " + std::string(PathLeaf(path)), changed,
                      [this, path, closed = std::move(closed)](DirtyDisposition disposition) {
                          OperationError.clear();
                          const std::optional<std::size_t> index = Set.IndexOf(path);
                          if (index && !Set.Close(*index, disposition, OperationError))
                              return;
                          if (closed)
                              closed(path);
                      });
}

std::vector<Problem> DataDocumentSheet::Problems() const
{
    std::vector<Problem> problems;
    for (DataDocument* document : Set.Documents())
        for (const DataValidationError& error : document->ValidationErrors())
            problems.push_back({ .Severity = ProblemSeverity::Error,
                                 .Message = std::format("{}: {}", PathLeaf(document->VirtualPath()), error.Message),
                                 .Location = error.Path,
                                 .Document = Set.Store().RefOf(*document) });
    if (!OperationError.empty())
        problems.push_back({ .Severity = ProblemSeverity::Warning, .Message = OperationError });
    return problems;
}
