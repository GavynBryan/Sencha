#include "UiPreviewWorkspace.h"

#include "ui/ActionLogPanel.h"
#include "ui/DiagnosticsPanel.h"
#include "ui/DocumentLibraryPanel.h"
#include "ui/ElementPanel.h"
#include "ui/ModelPanel.h"
#include "ui/OutlinePanel.h"
#include "ui/PreviewPanel.h"
#include "ui/UiPreviewStatusBar.h"
#include "ui/VocabularyPanel.h"

#include "project/ProcessLaunch.h"
#include "project/Project.h"
#include "render/RenderFeatureDetach.h"
#include "ui/EditorUiFeature.h"
#include "ui/EditorUiStyle.h"

#include <app/Engine.h>
#include <app/GameContexts.h>
#include <app/RuntimeContent.h>
#include <assets/hotreload/SourceReloadRoots.h>
#include <assets/runtime/RuntimeAssets.h>
#include <authored/VerbBindingData.h>
#include <core/assets/AssetLease.h>
#include <core/assets/AssetRegistry.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/console/ConsoleTypes.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/Renderer.h>
#include <ui/UiService.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <span>

namespace
{
    constexpr std::string_view kTargetFeatureId = "ui_surface_target";
    constexpr std::array<std::string_view, 1> kTargetDependsOn{ "editor_ui" };
    constexpr std::string_view kCommandOwner = "editor.ui_preview";

    constexpr const char* kLibraryProject = "Project";
    constexpr const char* kLibraryEngine = "Engine";
    constexpr const char* kLibraryEditor = "Editor";

    // The ground the document composes over: the palette's panel well, in
    // linear light because the target is cleared before any encoding.
    Vec3d PreviewGroundLinear()
    {
        const ImVec4 c = EditorUi::PanelBg;
        return Vec3d{ c.x, c.y, c.z };
    }
}

UiPreviewWorkspace::UiPreviewWorkspace(Engine& engine, const ProjectDescriptor* project, Game* module)
    : EngineRef(engine)
    , Watch(engine.Content().SourceReload())
{
    if (module != nullptr)
    {
        Vocabulary.InstallModuleVocabulary(*module);
        for (const std::string& diagnostic : Vocabulary.Errors())
            std::fprintf(stderr, "[ui preview] vocabulary: %s\n", diagnostic.c_str());
    }
    BuildLibrary(project);
    BuildUi();
    RegisterCommands();
}

UiPreviewWorkspace::~UiPreviewWorkspace()
{
    EngineRef.Console().Registry().UnregisterOwner(kCommandOwner);
    // The session closes its screen before the feature that drew it goes, and
    // the feature goes while the caches it borrows are still alive.
    Surface = WorkspaceView{};
    Session.reset();
    DetachRenderFeature(EngineRef, Target);
}

void UiPreviewWorkspace::BuildLibrary(const ProjectDescriptor* project)
{
    // The session has mounted and watches every root; the library only lists
    // the documents in them.
#ifdef SENCHA_EDITOR_UI_DIR
    Library.AddRoot(kLibraryEditor, SENCHA_EDITOR_UI_DIR);
#endif
    if (project != nullptr)
        for (const std::string& root : project->ContentRoots)
            Library.AddRoot(kLibraryProject, root);
    for (const ContentRootPaths& root : EngineRef.Content().Roots())
    {
        bool named = false;
        std::error_code ec;
#ifdef SENCHA_EDITOR_UI_DIR
        named |= std::filesystem::equivalent(root.Authored, SENCHA_EDITOR_UI_DIR, ec);
#endif
        if (project != nullptr)
            for (const std::string& projectRoot : project->ContentRoots)
                named |= std::filesystem::equivalent(root.Authored, projectRoot, ec);
        if (!named)
            Library.AddRoot(kLibraryEngine, root.Authored.generic_string());
    }
    Library.Rescan();
}

void UiPreviewWorkspace::BuildUi()
{
    UiService* ui = EngineRef.TryUi();
    if (ui == nullptr || !ui->IsReady())
    {
        std::fprintf(stderr, "[ui preview] the authored UI layer is unavailable; nothing to preview with\n");
        return;
    }
    Session = std::make_unique<UiPreviewSession>(*ui);

    Renderer& renderer = EngineRef.Graphics().MainRenderer;
    Target = renderer.StageFeature(
        std::make_unique<UiSurfaceTargetRenderFeature>(*ui, EngineRef.Content().Assets().Textures.get()),
        FeatureRegistration{ .Id = kTargetFeatureId, .DependsOn = kTargetDependsOn });
    std::vector<std::string_view> failed;
    if (!renderer.CommitStagedFeatures(&failed) || !failed.empty())
    {
        std::fprintf(stderr, "[ui preview] the preview target failed to set up; the Preview panel is unavailable\n");
        Target = nullptr;
    }

    Surface.Layout = DockLayoutRatios{ .Bottom = 0.22f, .Left = 0.20f, .Right = 0.26f, .RightBottom = 0.45f };
    Surface.Status = [this] { return Session ? Session->PackagePath() : std::string{}; };
    Surface.File.Save = [this] { std::string error; (void)SaveModel(&error); };

    Surface.AddPanel(std::make_unique<DocumentLibraryPanel>(
        Library, *Session,
        DocumentLibraryPanel::Actions{
            .Open = [this](const std::string& package) { OpenDocument(package); },
            .Rescan = [this] { RescanLibrary(); },
            .OpenInEditor = [this](const DocumentEntry& entry) { OpenInEditor(entry); },
        }));
    if (Target != nullptr)
    {
        // Bound before the panel is made, because the panel holds the binding
        // it was given; a binding is a declaration, and its target is created
        // on the first frame drawn.
        Binding = Target->Bind(Session->Surface(), PreviewGroundLinear());
        Surface.AddPanel(std::make_unique<PreviewPanel>(*Session, ViewState, *Target, Binding));
    }
    Surface.AddPanel(std::make_unique<OutlinePanel>(*Session, ViewState));
    Surface.AddPanel(std::make_unique<ElementPanel>(*Session, ViewState));
    Surface.AddPanel(std::make_unique<ModelPanel>(
        *Session, ViewState,
        ModelPanel::Actions{
            .Save = [this](std::string* error) { return SaveModel(error); },
            .Reset = [this] { ResetModel(); },
        }));
    Surface.AddPanel(std::make_unique<ActionLogPanel>(*Session));
    Surface.AddPanel(std::make_unique<VocabularyPanel>(Vocabulary, [this] {
        // Every structured-data asset in the mounted stack whose compiled
        // value is a binding set, inspected against the metadata catalog. The
        // path order is sorted so the panel reads the same way twice.
        std::vector<VocabularyPanel::BindingAsset> found;
        RuntimeAssets& stack = EngineRef.Content().Assets();
        std::vector<std::string> paths;
        for (const auto& [path, record] : stack.Registry.Records())
        {
            if (record.Type == AssetType::Data)
                paths.push_back(path);
        }
        std::sort(paths.begin(), paths.end());
        for (const std::string& path : paths)
        {
            const AssetLease lease = stack.Assets.LoadLease(path, AssetType::Data);
            if (!lease.IsValid())
                continue;
            const auto* library = stack.DataAssets.TryGet<VerbBindingLibrary>(
                DataAssetHandle::FromToken(lease.OpaqueToken()), kVerbBindingsTypeName);
            if (library == nullptr)
                continue;
            found.push_back({ path, Vocabulary.Inspect(*library, &stack.Registry,
                                                         &stack.DataAssets) });
        }
        return found;
    }));
    Surface.AddPanel(std::make_unique<DiagnosticsPanel>(
        *Session, ViewState, [this](const std::string& path) {
            if (const DocumentEntry* entry = Library.Find("asset://" + path))
                OpenInEditor(*entry);
        }));

    if (Watch != nullptr)
    {
        auto statusBar = std::make_shared<UiPreviewStatusBar>(*Session, Library, *Watch);
        Surface.Chrome.push_back([statusBar] { statusBar->Draw(); });
    }
}

void UiPreviewWorkspace::RegisterCommands()
{
    EngineRef.Console().Registry().RegisterCommand({
        .Name = "ui_preview.open",
        .Owner = std::string(kCommandOwner),
        .Usage = "ui_preview.open <asset>",
        .Help = "Open an authored document in the UI preview workspace.",
        .Callback = [this](ConsoleExecutionContext&, std::span<const std::string> args) {
            ConsoleResult result;
            if (args.size() != 1 || !Session)
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("expected one document path");
                return result;
            }
            OpenDocument(args[0]);
            result.Info("opened '" + args[0] + "'");
            return result;
        },
    });
}

void UiPreviewWorkspace::SetVisible(bool visible)
{
    if (!Session)
        return;
    // A screen in the background still updates and draws, so it closes with
    // the tab and comes back with the model it had.
    if (!visible && Session->IsOpen())
    {
        Suspended.emplace(Session->PackagePath(), Session->Model());
        Session->Close();
    }
    else if (visible && Suspended.has_value())
    {
        auto [package, model] = std::move(*Suspended);
        Suspended.reset();
        (void)Session->Open(package, std::move(model));
    }
}

void UiPreviewWorkspace::HandlePlatformEvent(PlatformEventContext& ctx)
{
    // The previewer's own keys; the preview surface has already had its
    // chance at this event, and what it consumed never got here.
    const bool typing = Window != nullptr && Window->GetInputCapture().Keyboard;
    if (ctx.Handled || typing || ctx.Event.type != SDL_EVENT_KEY_DOWN || ctx.Event.key.repeat
        || (ctx.Event.key.mod & SDL_KMOD_CTRL) == 0)
        return;
    switch (ctx.Event.key.scancode)
    {
    case SDL_SCANCODE_R:
        RescanLibrary();
        ctx.Handled = true;
        return;
    case SDL_SCANCODE_I:
        if (Session)
            Session->SetPointerMode(Session->Mode() == UiPreviewSession::PointerMode::Inspect
                                        ? UiPreviewSession::PointerMode::Interact
                                        : UiPreviewSession::PointerMode::Inspect);
        ctx.Handled = true;
        return;
    default:
        return;
    }
}

void UiPreviewWorkspace::Tick(FrameUpdateContext&)
{
    if (!Session)
        return;
    // The keyboard follows the pointer: while it is over the preview the
    // document has it, and the moment it leaves the editor's shortcuts are
    // back. Read from the layer, because the events the document consumed
    // never reached this side.
    UiService* ui = EngineRef.TryUi();
    if (ui != nullptr && Session->Mode() == UiPreviewSession::PointerMode::Interact)
        Session->SetActivated(ui->IsPointerOver(Session->Surface()));
    Session->Poll();
}

void UiPreviewWorkspace::OpenDocument(const std::string& packagePath)
{
    if (!Session)
        return;
    const DocumentEntry* entry = Library.Find(packagePath);
    UiPreviewModel model;
    OpenSource.clear();
    if (entry != nullptr)
    {
        OpenSource = entry->SourcePath();
        std::string error;
        if (const std::optional<UiPreviewModel> loaded =
                UiPreviewModel::Load(UiPreviewModel::SidecarFor(OpenSource), &error))
        {
            model = *loaded;
        }
        else
        {
            // No sidecar yet, or a broken one: open with the model the document
            // names and nothing declared, and let the Bindings tab say what it
            // asked for.
            model.ModelName = entry->ModelName;
            if (entry->HasPreviewModel)
                std::fprintf(stderr, "[ui preview] %s: %s\n", packagePath.c_str(), error.c_str());
        }
    }
    ViewState.Hovered = {};
    ViewState.Selected = {};
    Session->ClearDiagnostics();
    Session->ClearActions();
    if (!Session->Open(packagePath, std::move(model)))
        std::fprintf(stderr, "[ui preview] '%s' did not open; see Diagnostics\n", packagePath.c_str());
}

void UiPreviewWorkspace::RescanLibrary()
{
    Library.Rescan();
    if (Watch)
        Watch->Rescan();
}

bool UiPreviewWorkspace::SaveModel(std::string* error)
{
    if (!Session || !Session->IsOpen() || OpenSource.empty())
    {
        if (error) *error = "no document open";
        return false;
    }
    const bool saved = Session->Model().Save(UiPreviewModel::SidecarFor(OpenSource), error);
    if (saved)
        Library.Rescan();
    return saved;
}

void UiPreviewWorkspace::ResetModel()
{
    if (!Session || !Session->IsOpen())
        return;
    OpenDocument(Session->PackagePath());
}

void UiPreviewWorkspace::OpenInEditor(const DocumentEntry& entry)
{
    // The author's editor: $VISUAL, then $EDITOR, then the desktop's handler.
    const char* editor = std::getenv("VISUAL");
    if (editor == nullptr || editor[0] == '\0')
        editor = std::getenv("EDITOR");
    std::string binary = editor != nullptr && editor[0] != '\0' ? editor : "xdg-open";
    long pid = 0;
    std::string error;
    if (!SpawnProcess(binary, { entry.SourcePath().string() }, "", pid, &error))
        std::fprintf(stderr, "[ui preview] could not open '%s' in '%s': %s\n",
                     entry.SourcePath().string().c_str(), binary.c_str(), error.c_str());
}
