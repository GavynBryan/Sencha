#include "DataWorkspace.h"

#include "project/ProjectFileSteps.h"
#include "project/ProjectFiles.h"
#include "text/PathLeaf.h"

#include "actions/ActionOffer.h"
#include "problems/ProblemList.h"

#include <app/Engine.h>
#include <app/GameContexts.h>
#include <app/RuntimeContent.h>

#include <SDL3/SDL_keycode.h>

#include <utility>

namespace
{
constexpr std::string_view kProblemSource = "data";
} // namespace

ActionDefinition NewDataAssetAction()
{
    return { .Id = "data.new", .Label = "New Data Asset", .Category = "File",
             .DefaultChords = { { SDLK_N, { .Ctrl = true } } } };
}

DataWorkspace::DataWorkspace(Engine& engine, const ProjectDescriptor& project, DocumentSourceSet& sources,
                             DataDocumentStore& store, ProjectFileSteps* files, PromptQueue& prompts,
                             ActionCatalog& catalog, ProblemList& problems, const DataSubtypeAuthoringSetup& authorings)
    : EngineRef(engine)
    , Model(engine.Content().Assets(), project, sources, store)
    , Files(files)
    , Problems(problems)
    , Sheet(Model.Documents, prompts, authorings, files)
    , Source(Model.Documents, [this] { return Model.Documents.Active(); })
    , Creation([this] { return Model.Documents.CreatableSubtypes(); },
               [this](const std::string& subtype, const std::string& path, std::string& error) {
                   if (Model.Documents.Create(subtype, path, error) == nullptr)
                       return false;
                   ShowSheet();
                   return true;
               })
{
    DefineActions(catalog);
    OfferAction(catalog, ActionHandlers, SaveAction(), { .Invoke = [this] { SaveActive(); } });
}

DataWorkspace::~DataWorkspace()
{
    Problems.Clear(kProblemSource);
}

void DataWorkspace::DefineActions(ActionCatalog& catalog)
{
    const auto active = [this] { return Model.Documents.Active() != nullptr; };
    OfferAction(catalog, ActionHandlers, NewDataAssetAction(),
                { .Invoke = [this] {
                    Creation.Clear();
                    Subject = CardSubject::Creating;
                } });
    const auto changesFiles = [this, active] { return Files != nullptr && active(); };
    OfferAction(catalog, ActionHandlers, { .Id = "data.duplicate", .Label = "Duplicate", .Category = "File" },
                { .Invoke = [this] { DuplicateActive(); }, .Enabled = changesFiles });
    OfferAction(catalog, ActionHandlers,
                { .Id = "data.delete", .Label = "Trash File", .Category = "File", .Tone = ActionTone::Destructive },
                { .Invoke = [this] { TrashActive(); }, .Enabled = changesFiles });
    OfferAction(catalog, ActionHandlers,
                { .Id = "data.close", .Label = "Close", .Category = "File",
                  .DefaultChords = { { SDLK_W, { .Ctrl = true } } } },
                { .Invoke = [this] { Sheet.CloseActive(); }, .Enabled = active });
    OfferAction(catalog, ActionHandlers,
                { .Id = "data.view_source", .Label = "View Source", .Category = "View",
                  .DefaultChords = { { SDLK_U, { .Ctrl = true } } } },
                { .Invoke = [this] { ShowingSource = !ShowingSource; },
                  .Enabled = active,
                  .Checked = [this] { return ShowingSource; } });
}

void DataWorkspace::DuplicateActive()
{
    const DataDocument* document = Model.Documents.Active();
    if (document == nullptr || Files == nullptr)
        return;
    // What is saved is copied; unsaved changes stay with the original.
    const AssetRegistry& registry = EngineRef.Content().Assets().Registry;
    const std::string copy =
        CopyPath(document->VirtualPath(), [&](const std::string& path) { return registry.Contains(path); });
    std::string refused = Files->Duplicate(document->VirtualPath(), copy);
    if (refused.empty())
        (void)Sheet.Open(copy);
    else
        Sheet.SetError(std::move(refused));
}

void DataWorkspace::TrashActive()
{
    // Closed first, its changes settled: a file an editor holds is not trashed.
    Sheet.CloseActive([this](const std::string& path) {
        if (Files != nullptr)
            Sheet.SetError(Files->Trash(path));
    });
}

bool DataWorkspace::RevealProblem(const Problem& problem)
{
    if (!problem.Document.has_value() || !OwnsDocument(*problem.Document))
        return false;
    (void)Sheet.Show(problem.Document->Key);
    ShowSheet();
    return true;
}

void DataWorkspace::SaveActive()
{
    if (const std::optional<DocumentRef> active = Sheet.ActiveRef())
        (void)Model.Sources.Save(*active);
}

void DataWorkspace::ShowSheet()
{
    Subject = CardSubject::Document;
    ShowingSource = false;
}

void DataWorkspace::Tick(FrameUpdateContext&)
{
    (void)Model.Documents.Store().PushWaiting();
    Problems.Publish(kProblemSource, Sheet.Problems());
    // Only the active document's editor runs, and only while it can be seen: a
    // background document has no surface on screen and nothing reads its state.
    if (Visible)
        Sheet.Update();
}

bool DataWorkspace::ClaimPlatformEvent(PlatformEventContext& ctx)
{
    return Sheet.ClaimPlatformEvent(ctx.Event);
}

bool DataWorkspace::OwnsDocument(const DocumentRef& document) const
{
    return Sheet.Owns(document);
}

void DataWorkspace::RevealDocument(const DocumentRef& document)
{
    if (Sheet.Show(document.Key))
        ShowSheet();
}

WorkspaceDocumentFacet DataWorkspace::Document() const
{
    WorkspaceDocumentFacet facet;
    Sheet.AppendOpen(facet);
    if (const std::optional<WorkspaceOpenDocument> active = Sheet.ActiveEntry())
    {
        facet.Label = active->Label;
        facet.Changed = active->Changed;
        facet.Active = active->Key;
    }
    return facet;
}

void DataWorkspace::ShowDocument(std::string_view key)
{
    (void)Sheet.Show(key);
    Subject = CardSubject::Document;
}

WorkspaceSelectionFacet DataWorkspace::Selection()
{
    if (Subject == CardSubject::Creating)
        return { .Title = "New Data Asset", .Sections = { { .Heading = {}, .Source = &Creation } } };
    const DataDocument* document = Model.Documents.Active();
    if (document == nullptr)
        return {};
    return { .Title = std::string(PathLeaf(document->VirtualPath())),
             .Actions = { "file.save", "data.view_source", "data.duplicate", "data.close", "data.delete" },
             .Sections = Sheet.Sections(ShowingSource ? &Source : nullptr),
             .Primary = true };
}

bool DataWorkspace::OpenAsset(const ProjectFile& file)
{
    if (!Sheet.Open(file.AssetPath))
        return false;
    ShowSheet();
    return true;
}
