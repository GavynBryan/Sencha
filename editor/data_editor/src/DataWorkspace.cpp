#include "DataWorkspace.h"

#include "DataEditorPanels.h"
#include "SubtypeEditors.h"

#include <app/Engine.h>
#include <app/GameContexts.h>
#include <app/RuntimeContent.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/console/ConsoleTypes.h>

#include <memory>
#include <span>
#include <utility>

namespace
{
constexpr std::string_view kCommandOwner = "editor.data";
}

DataWorkspace::DataWorkspace(Engine& engine, const ProjectDescriptor& project, DocumentSourceSet& sources,
                             DataDocumentStore& store)
    : EngineRef(engine)
    , Model(engine.Content().Assets(), project, sources, store)
{
    RegisterBuiltInSubtypeEditors(SubtypeEditors);
    BuildUi();
    RegisterCommands();
}

DataWorkspace::~DataWorkspace()
{
    EngineRef.Console().Registry().UnregisterOwner(kCommandOwner);
    Surface = WorkspaceView{};
}

void DataWorkspace::BuildUi()
{
    Surface.Layout = DockLayoutRatios{
        .Bottom = 0.18f,
        .Left = 0.23f,
        .Right = 0.30f,
        .CenterBottom = 0.30f,
    };
    Surface.File.Save = [this] { SaveActive(); };
    Surface.Status = [this] {
        const DataDocument* document = Model.Documents.Active();
        if (document == nullptr)
            return std::string{};
        return document->VirtualPath() + (document->IsDirty() ? " *" : "");
    };

    Surface.AddPanel(std::make_unique<DataAssetBrowserPanel>(Model));
    Surface.AddPanel(std::make_unique<DataFormPanel>(Model, SubtypeEditors));
    Surface.AddPanel(std::make_unique<DataDocumentationPanel>(Model));
    Surface.AddPanel(std::make_unique<DataValidationPanel>(Model));
    Surface.AddPanel(std::make_unique<DataRawJsonPanel>(Model));
    // Documents open and close at runtime, so a subtype's panel gates itself on
    // the active subtype rather than appearing and disappearing.
    for (const auto& editor : SubtypeEditors.Entries())
        for (auto& panel : editor->CreatePanels(Model.Documents))
            Surface.AddPanel(std::move(panel));
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

void DataWorkspace::SaveActive()
{
    if (const DataDocument* active = Model.Documents.Active())
        (void)Model.Sources.Save(Model.Documents.RefOf(*active));
}

void DataWorkspace::Tick(FrameUpdateContext&)
{
    (void)Model.Documents.Store().PushWaiting();
    // Only the active document's editor runs, and only while it can be seen: a
    // background document has no surface on screen and nothing reads its state.
    if (!Visible)
        return;
    if (const DataDocument* document = Model.Documents.Active())
        if (IDataSubtypeEditor* editor = SubtypeEditors.Find(document->Subtype()))
            editor->UpdateForFrame(*document, Model.Documents);
}

bool DataWorkspace::ClaimPlatformEvent(PlatformEventContext& ctx)
{
    // A surface listening for a control to bind claims the press outright, or
    // the same keystroke also lands in whatever widget has focus.
    const DataDocument* document = Model.Documents.Active();
    if (document == nullptr)
        return false;
    IDataSubtypeEditor* editor = SubtypeEditors.Find(document->Subtype());
    return editor != nullptr && editor->HandlePlatformEvent(ctx.Event);
}

bool DataWorkspace::OwnsDocument(const DocumentRef& document) const
{
    return document.Source == &Model.Documents.Store() && Model.Documents.IndexOf(document.Key).has_value();
}

void DataWorkspace::RevealDocument(const DocumentRef& document)
{
    Model.Documents.Reveal(document.Key);
}
