#include "DataWorkspace.h"

#include "text/PathLeaf.h"


#include "actions/ActionOffer.h"
#include "data/DataAssetFiles.h"
#include "problems/ProblemList.h"
#include "prompts/DocumentPrompts.h"
#include "text/TextFilterMatch.h"

#include <app/Engine.h>
#include <app/GameContexts.h>
#include <app/RuntimeContent.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/console/ConsoleTypes.h>

#include <SDL3/SDL_keycode.h>

#include <format>
#include <memory>
#include <span>
#include <utility>

namespace
{
constexpr std::string_view kCommandOwner = "editor.data";
constexpr std::string_view kExplorerId = "data";
constexpr std::string_view kProblemSource = "data";
constexpr std::string_view kScheme = "asset://";

std::string RootRelative(std::string_view path)
{
    return std::string(path.starts_with(kScheme) ? path.substr(kScheme.size()) : path);
}

} // namespace

DataWorkspace::DataWorkspace(Engine& engine, const ProjectDescriptor& project, DocumentSourceSet& sources,
                             DataDocumentStore& store, PromptQueue& prompts, ActionCatalog& catalog,
                             ProblemList& problems, const DataSubtypeAuthoringSetup& authorings)
    : EngineRef(engine)
    , Model(engine.Content().Assets(), project, sources, store)
    , Prompts(prompts.MakeAsker())
    , Problems(problems)
    , Properties(Model.Documents, [this] { return Model.Documents.Active(); }, &Authoring)
    , Source(Model.Documents, [this] { return Model.Documents.Active(); })
    , Creation([this] { return Model.Documents.CreatableSubtypes(); },
               [this](const std::string& subtype, const std::string& path, std::string& error) {
                   if (Model.Documents.Create(subtype, path, error) == nullptr)
                       return false;
                   Subject = CardSubject::Document;
                   return true;
               })
    , FilePath([this] {
                   const DataDocument* document = Model.Documents.Active();
                   return document != nullptr ? document->VirtualPath() : std::string{};
               },
               [this](const std::string& to) {
                   AfterSettling([this, to](const std::string& from, DirtyDisposition disposition) {
                       OperationError.clear();
                       (void)Model.Rename(from, to, disposition, OperationError);
                   });
               })
{
    authorings(Authoring);
    BuildUi();
    RegisterCommands();
    DefineActions(catalog);
    OfferAction(catalog, ActionHandlers, SaveAction(), { .Invoke = [this] { SaveActive(); } });
}

DataWorkspace::~DataWorkspace()
{
    Problems.Clear(kProblemSource);
    EngineRef.Console().Registry().UnregisterOwner(kCommandOwner);
}

void DataWorkspace::BuildUi()
{
}

void DataWorkspace::RegisterCommands()
{
    EngineRef.Console().Registry().RegisterCommand({
        .Name = "data.open",
        .Owner = std::string(kCommandOwner),
        .Usage = "data.open <asset>",
        .Help = "Open a data asset in the data workspace.",
        .Callback = [this](ConsoleExecutionContext&, std::span<const std::string> args) {
            ConsoleResult result;
            std::string error;
            if (args.size() != 1 || Model.Documents.OpenOrFocus(args[0], error) == nullptr)
            {
                result.Status = ConsoleStatus::ExecutionFailed;
                result.Error(args.size() != 1 ? "expected one asset path" : error);
                return result;
            }
            result.Info("opened '" + args[0] + "'");
            return result;
        },
    });
}

void DataWorkspace::DefineActions(ActionCatalog& catalog)
{
    const auto active = [this] { return Model.Documents.Active() != nullptr; };
    OfferAction(catalog, ActionHandlers,
                { .Id = "data.new", .Label = "New Data Asset", .Category = "File",
                  .DefaultChords = { { SDLK_N, { .Ctrl = true } } } },
                { .Invoke = [this] {
                    Creation.Clear();
                    Subject = CardSubject::Creating;
                } });
    OfferAction(catalog, ActionHandlers, { .Id = "data.duplicate", .Label = "Duplicate", .Category = "File" },
                { .Invoke = [this] { DuplicateActive(); }, .Enabled = active });
    OfferAction(catalog, ActionHandlers,
                { .Id = "data.delete", .Label = "Delete File", .Category = "File", .Tone = ActionTone::Destructive },
                { .Invoke = [this] {
                      AfterSettling([this](const std::string& path, DirtyDisposition disposition) {
                          OperationError.clear();
                          (void)Model.Delete(path, disposition, OperationError);
                      });
                  },
                  .Enabled = active });
    OfferAction(catalog, ActionHandlers,
                { .Id = "data.close", .Label = "Close", .Category = "File",
                  .DefaultChords = { { SDLK_W, { .Ctrl = true } } } },
                { .Invoke = [this] {
                      AfterSettling([this](const std::string& path, DirtyDisposition disposition) {
                          OperationError.clear();
                          if (const std::optional<std::size_t> index = Model.Documents.IndexOf(path))
                              (void)Model.Documents.Close(*index, disposition, OperationError);
                      });
                  },
                  .Enabled = active });
    OfferAction(catalog, ActionHandlers,
                { .Id = "data.view_source", .Label = "View Source", .Category = "View",
                  .DefaultChords = { { SDLK_U, { .Ctrl = true } } } },
                { .Invoke = [this] { ShowingSource = !ShowingSource; },
                  .Enabled = active,
                  .Checked = [this] { return ShowingSource; } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "explorer.data", .Label = "Data Assets", .Category = "Explore", .Icon = IconId::Folder,
                  .DefaultChords = { { SDLK_D, {} } } },
                {});
}

void DataWorkspace::AfterSettling(std::function<void(const std::string& path, DirtyDisposition disposition)> then)
{
    const DataDocument* document = Model.Documents.Active();
    if (document == nullptr)
        return;
    const std::string path = document->VirtualPath();
    // By path: the documents may move while the question is open.
    AskAboutChanges(Prompts, document->IsDirty() || document->IsEditing(), path,
                    [then = std::move(then), path](DirtyDisposition disposition) { then(path, disposition); });
}

void DataWorkspace::DuplicateActive()
{
    const DataDocument* document = Model.Documents.Active();
    if (document == nullptr)
        return;
    // "input/player.profile.sdata" -> "input/player_copy.profile.sdata", then _copy2...
    const std::string relative = RootRelative(document->VirtualPath());
    const std::size_t slash = relative.rfind('/');
    const std::size_t dot = relative.find('.', slash == std::string::npos ? 0 : slash + 1);
    const std::string stem = relative.substr(0, dot);
    const std::string extension = dot == std::string::npos ? std::string{} : relative.substr(dot);
    for (int n = 1; n < 100; ++n)
    {
        const std::string candidate = stem + (n == 1 ? std::string("_copy") : "_copy" + std::to_string(n)) + extension;
        std::string error;
        if (Model.Duplicate(document->VirtualPath(), candidate, error))
        {
            (void)Model.Documents.OpenOrFocus(DataAssetVirtualPath(candidate), OperationError);
            return;
        }
    }
    OperationError = "No free name for a copy beside it";
}

void DataWorkspace::PublishProblems()
{
    std::vector<Problem> problems;
    for (DataDocument* document : Model.Documents.Documents())
        for (const DataValidationError& error : document->ValidationErrors())
            problems.push_back({ .Severity = ProblemSeverity::Error,
                                 .Message = std::format("{}: {}", PathLeaf(document->VirtualPath()), error.Message),
                                 .Location = error.Path,
                                 .Document = Model.Documents.RefOf(*document) });
    if (!OperationError.empty())
        problems.push_back({ .Severity = ProblemSeverity::Warning, .Message = OperationError });
    Problems.Publish(kProblemSource, std::move(problems));
}

bool DataWorkspace::RevealProblem(const Problem& problem)
{
    if (!problem.Document.has_value() || !OwnsDocument(*problem.Document))
        return false;
    Model.Documents.Reveal(problem.Document->Key);
    Subject = CardSubject::Document;
    ShowingSource = false;
    return true;
}

void DataWorkspace::SaveActive()
{
    if (const DataDocument* active = Model.Documents.Active())
        (void)Model.Sources.Save(Model.Documents.RefOf(*active));
}

void DataWorkspace::Tick(FrameUpdateContext&)
{
    (void)Model.Documents.Store().PushWaiting();
    PublishProblems();
    // Only the active document's editor runs, and only while it can be seen: a
    // background document has no surface on screen and nothing reads its state.
    if (!Visible)
        return;
    if (const DataDocument* document = Model.Documents.Active())
    {
        if (IDataSubtypeAuthoring* authoring = Authoring.Find(document->Subtype()))
        {
            authoring->Update(*document, Model.Documents);
            // A control pressed while listening, written as the author would.
            if (std::optional<std::pair<std::string, std::string>> write = authoring->TakeWrite(*document))
                if (Properties.BeginEdit(write->first))
                    (void)Properties.CommitEdit(write->first, write->second);
        }
    }
}

bool DataWorkspace::ClaimPlatformEvent(PlatformEventContext& ctx)
{
    // A surface listening for a control to bind claims the press outright, or
    // the same keystroke also lands in whatever widget has focus.
    const DataDocument* document = Model.Documents.Active();
    if (document == nullptr)
        return false;
    // A control being listened for is claimed outright, or the same keystroke
    // also lands in whatever has focus.
    IDataSubtypeAuthoring* authoring = Authoring.Find(document->Subtype());
    return authoring != nullptr && authoring->HandlePlatformEvent(ctx.Event);
}

bool DataWorkspace::OwnsDocument(const DocumentRef& document) const
{
    return document.Source == &Model.Documents.Store() && Model.Documents.IndexOf(document.Key).has_value();
}

void DataWorkspace::RevealDocument(const DocumentRef& document)
{
    Model.Documents.Reveal(document.Key);
}

WorkspaceDocumentFacet DataWorkspace::Document() const
{
    WorkspaceDocumentFacet facet;
    for (const DataDocument* document : Model.Documents.Documents())
        facet.Open.push_back({ .Key = document->VirtualPath(),
                               .Label = std::string(PathLeaf(document->VirtualPath())),
                               .Changed = document->IsDirty() });
    if (const DataDocument* active = Model.Documents.Active())
    {
        facet.Label = std::string(PathLeaf(active->VirtualPath()));
        facet.Changed = active->IsDirty();
        facet.Active = active->VirtualPath();
    }
    return facet;
}

void DataWorkspace::ShowDocument(std::string_view key)
{
    if (const std::optional<std::size_t> index = Model.Documents.IndexOf(key))
        Model.Documents.SetActive(*index);
    Subject = CardSubject::Document;
}

WorkspaceSelectionFacet DataWorkspace::Selection()
{
    if (Subject == CardSubject::Creating)
        return { .Title = "New Data Asset", .Sections = { { .Heading = {}, .Source = &Creation } } };
    const DataDocument* document = Model.Documents.Active();
    if (document == nullptr)
        return {};
    WorkspaceSelectionFacet facet;
    facet.Title = std::string(PathLeaf(document->VirtualPath()));
    facet.Actions = { "file.save", "data.view_source", "data.duplicate", "data.close", "data.delete" };
    facet.Sections.push_back({ .Heading = {}, .Source = &FilePath });
    facet.Sections.push_back({ .Heading = {}, .Source = ShowingSource ? static_cast<IPropertySource*>(&Source)
                                                                      : static_cast<IPropertySource*>(&Properties) });
    if (IDataSubtypeAuthoring* authoring = Authoring.Find(document->Subtype()))
        for (const PropertySection& section : authoring->Sections(*document))
            facet.Sections.push_back(section);
    facet.Primary = true;
    return facet;
}

std::vector<WorkspaceExplorerInfo> DataWorkspace::Explorers() const
{
    return { { .Id = std::string(kExplorerId), .Title = "Data Assets", .Shape = ExplorerShape::Outline,
               .Action = "explorer.data" } };
}

ExplorerPage DataWorkspace::Explore(const ExplorerQuery& query)
{
    ExplorerPage page;
    if (query.Explorer != kExplorerId)
        return page;
    page.Actions = { "data.new" };
    page.Empty = "No data asset's path holds that.";
    const DataDocument* active = Model.Documents.Active();
    for (const AssetRecord* record : Model.DataAssets())
    {
        if (!TextFilterMatch(record->Path, query.Filter) || !CountOnPage(page, query))
            continue;
        page.Items.push_back({ .Key = record->Path,
                               .Label = RootRelative(record->Path),
                               .Current = active != nullptr && active->VirtualPath() == record->Path });
    }
    return page;
}

void DataWorkspace::ChooseInExplorer(std::string_view explorer, std::string_view key, ExplorerChoice, std::size_t)
{
    if (explorer != kExplorerId)
        return;
    OperationError.clear();
    if (Model.Documents.OpenOrFocus(key, OperationError) != nullptr)
    {
        Subject = CardSubject::Document;
        ShowingSource = false;
    }
}
