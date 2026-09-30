#include "ShojiServices.h"

#include "ui/ActionLogPanel.h"
#include "ui/DiagnosticsPanel.h"
#include "ui/DocumentLibraryPanel.h"
#include "ui/ElementPanel.h"
#include "ui/ModelPanel.h"
#include "ui/OutlinePanel.h"
#include "ui/PreviewPanel.h"
#include "ui/ShojiStatusBar.h"
#include "ui/VocabularyPanel.h"

#include "project/ProcessLaunch.h"
#include "project/ProjectContentMount.h"
#include "project/SourceReloadRoots.h"
#include "ui/AuthoredThemeStyleSheet.h"
#include "ui/EditorThemeStartup.h"
#include "ui/EditorUiFeature.h"

#include <app/Engine.h>
#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <app/RuntimeContent.h>
#include <assets/runtime/RuntimeAssets.h>
#include <authored/VerbBindingData.h>
#include <core/assets/AssetLease.h>
#include <core/assets/AssetRegistry.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/Renderer.h>
#include <platform/SdlWindow.h>
#include <ui/UiService.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>

#ifndef SENCHA_EDITOR_SHOJI_BRAND_DIR
#define SENCHA_EDITOR_SHOJI_BRAND_DIR "."
#endif

namespace
{
    constexpr std::string_view kUiFeatureId = "editor_ui";
    constexpr std::string_view kTargetFeatureId = "ui_surface_target";
    constexpr std::array<std::string_view, 1> kTargetDependsOn{ kUiFeatureId };

    constexpr const char* kLibraryProject = "Project";
    constexpr const char* kLibraryEngine = "Engine";
    constexpr const char* kLibraryEditor = "Editor";

    // One callback per frame, off the event path.
    class FrameHook
    {
    public:
        explicit FrameHook(std::function<void()> fn) : Fn(std::move(fn)) {}
        void FrameUpdate(FrameUpdateContext&) { if (Fn) Fn(); }

    private:
        std::function<void()> Fn;
    };

    // The ground the document composes over: the palette's panel well, in
    // linear light because the target is cleared before any encoding.
    Vec3d PreviewGroundLinear()
    {
        const ImVec4 c = EditorUi::PanelBg;
        return Vec3d{ c.x, c.y, c.z };
    }
}

ShojiServices::ShojiServices(Engine& engine,
                             SdlWindow& window,
                             const EngineConfig&,
                             std::optional<std::string> projectPath,
                             std::optional<std::string> initialDocument)
    : EnginePtr(&engine)
    , Window(&window)
    , ProjectPath(std::move(projectPath))
    , InitialDocument(std::move(initialDocument))
{
    LoadProject();
    LoadVocabulary();
    MountLibraries();
    BuildSourceWatch();
    BuildUi();

    if (InitialDocument)
        OpenDocument(*InitialDocument);
}

ShojiServices::~ShojiServices()
{
    // The session closes its screen before the feature that drew it goes, and
    // the feature goes while the caches it borrows are still alive.
    Session.reset();
    if (Target != nullptr && EnginePtr != nullptr)
    {
        if (GraphicsServices* graphics = EnginePtr->TryGraphics();
            graphics == nullptr || !graphics->MainRenderer.RemoveFeature(Target))
        {
            std::fprintf(stderr, "[shoji] the preview target feature could not be removed\n");
        }
        Target = nullptr;
    }
    Watch.reset();
    // The World that holds the module's declarations goes before the module.
    Vocabulary.reset();
    if (GameModule.IsValid())
        ModuleLoader.Unload(GameModule);
}

// The vocabulary a project's documents author against. The module is loaded
// for one hook and never started: no components, no systems, no engine state,
// and no dispatcher behind the names it declares.
void ShojiServices::LoadVocabulary()
{
    Vocabulary = std::make_unique<VocabularyCatalog>();
    if (!Project || Project->GameModulePath.empty())
        return;

    std::string error;
    GameModule = ModuleLoader.Load(Project->GameModulePath, &error);
    if (!GameModule.IsValid())
    {
        std::fprintf(stderr, "[shoji] failed to load game module '%s': %s\n",
                     Project->GameModulePath.c_str(), error.c_str());
        return;
    }
    Vocabulary->InstallModuleVocabulary(*GameModule.Instance);
    for (const std::string& diagnostic : Vocabulary->Errors())
        std::fprintf(stderr, "[shoji] vocabulary: %s\n", diagnostic.c_str());
}

void ShojiServices::LoadProject()
{
    if (!ProjectPath)
    {
        std::fprintf(stderr, "[shoji] no project: pass --project <path.senchaproj> or set SENCHA_PROJECT; "
                             "the engine's and the editor's own documents are still available\n");
        return;
    }
    ProjectDescriptor descriptor;
    std::string error;
    if (!ProjectDescriptor::Load(*ProjectPath, descriptor, &error))
    {
        std::fprintf(stderr, "[shoji] failed to open project '%s': %s\n", ProjectPath->c_str(), error.c_str());
        return;
    }
    Project = std::move(descriptor);
}

void ShojiServices::MountLibraries()
{
    Engine& engine = *EnginePtr;
    RuntimeAssets& assets = engine.Content().Assets();

    // Into the ENGINE's stack, because Engine::Ui() resolves packages through
    // it: a document mounted anywhere else is one the UI layer cannot find.
    // The engine's own content is already there; the editor's surfaces and
    // the project's roots join it.
#ifdef SENCHA_EDITOR_UI_DIR
    MountEditorContent(SENCHA_EDITOR_UI_DIR, assets, engine.Logging(), &engine.Jobs());
    Library.AddRoot(kLibraryEditor, SENCHA_EDITOR_UI_DIR);
#endif
    if (Project)
    {
        for (const std::string& root : Project->ContentRoots)
        {
            MountEditorContent(root, assets, engine.Logging(), &engine.Jobs());
            Library.AddRoot(kLibraryProject, root);
        }
    }
    for (const ContentRootPaths& root : engine.Content().Roots())
    {
        const std::string path = root.Authored.generic_string();
        bool named = false;
#ifdef SENCHA_EDITOR_UI_DIR
        named |= std::filesystem::equivalent(root.Authored, SENCHA_EDITOR_UI_DIR);
#endif
        if (Project)
            for (const std::string& projectRoot : Project->ContentRoots)
                named |= std::filesystem::equivalent(root.Authored, projectRoot);
        if (!named)
            Library.AddRoot(kLibraryEngine, path);
    }
    Library.Rescan();
}

void ShojiServices::BuildSourceWatch()
{
    Engine& engine = *EnginePtr;
    Watch = std::make_unique<SourceReloadRoots>(engine.Logging(), &engine.Jobs(), engine.Tasks());
    RuntimeAssets& assets = engine.Content().Assets();
    // Every library root, against the stack it was mounted into. A save in
    // any of them re-cooks and the open document rebuilds in place.
    for (const DocumentLibrary::LibraryRoot& root : Library.LibraryRoots())
    {
        Watch->AddRoot(root.Path, { ".rml", ".rcss", ".ttf", ".otf", ".png" },
                       assets.Assets, assets.Registry);
    }
}

void ShojiServices::BuildUi()
{
    Engine& engine = *EnginePtr;
    ApplyEditorThemeFromConsole(engine.Console());

    UiService* ui = engine.TryUi();
    if (ui == nullptr || !ui->IsReady())
    {
        std::fprintf(stderr, "[shoji] the authored UI layer is unavailable; nothing to preview with\n");
        return;
    }
    Session = std::make_unique<UiPreviewSession>(*ui);

    Renderer& renderer = engine.Graphics().MainRenderer;
    RuntimeAssets& assets = engine.Content().Assets();

    auto target = std::make_unique<UiSurfaceTargetRenderFeature>(*ui, assets.Textures.get());
    Target = renderer.StageFeature(std::move(target),
                                   FeatureRegistration{ .Id = kTargetFeatureId,
                                                        .DependsOn = kTargetDependsOn });

    auto uiFeature = std::make_unique<EditorUiFeature>(
        engine, *Window, engine.Graphics().Instance, engine.Graphics().Frames, "shoji.imgui.ini",
        DockLayoutRatios{ .Bottom = 0.22f, .Left = 0.20f, .Right = 0.26f, .RightBottom = 0.45f });
    UiFeature = uiFeature.get();
    UiFeature->SetIdentity(ShellIdentity{
        .Product = "SHOJI",
        .LogoPath = std::string(SENCHA_EDITOR_SHOJI_BRAND_DIR) + "/shoji-logo.svg",
    });
    // The theme belongs on a previewed document only when the author says so
    // (the Preview's Host theme switch), never by the shell's default.
    UiFeature->SetAuthoredThemePublishing(false);
    UiFeature->SetStatusProvider([this] { return Session ? Session->PackagePath() : std::string{}; });
    UiFeature->SetFileActions({}, {}, [this] { std::string e; (void)SaveModel(&e); }, {});

    UiFeature->AddPanel(std::make_unique<DocumentLibraryPanel>(
        Library, *Session,
        DocumentLibraryPanel::Actions{
            .Open = [this](const std::string& package) { OpenDocument(package); },
            .Rescan = [this] { RescanLibrary(); },
            .OpenInEditor = [this](const DocumentEntry& entry) { OpenInEditor(entry); },
        }));
    if (Target != nullptr)
    {
        // Bound before the panel is made, because the panel holds the binding
        // it was given. The feature is only staged at this point; a binding is
        // a declaration and its target is created on the first frame drawn.
        Binding = Target->Bind(Session->Surface(), PreviewGroundLinear());
        UiFeature->AddPanel(std::make_unique<PreviewPanel>(*Session, View, *Target, Binding));
    }
    else
    {
        std::fprintf(stderr, "[shoji] the preview target feature failed to stage; "
                             "the Preview panel is unavailable\n");
    }
    UiFeature->AddPanel(std::make_unique<OutlinePanel>(*Session, View));
    UiFeature->AddPanel(std::make_unique<ElementPanel>(*Session, View));
    UiFeature->AddPanel(std::make_unique<ModelPanel>(
        *Session, View,
        ModelPanel::Actions{
            .Save = [this](std::string* error) { return SaveModel(error); },
            .Reset = [this] { ResetModel(); },
        }));
    UiFeature->AddPanel(std::make_unique<ActionLogPanel>(*Session));
    UiFeature->AddPanel(std::make_unique<VocabularyPanel>(*Vocabulary, [this] {
        // Every structured-data asset in the mounted stack whose compiled
        // value is a binding set, inspected against the metadata catalog. The
        // path order is sorted so the panel reads the same way twice.
        std::vector<VocabularyPanel::BindingAsset> found;
        RuntimeAssets& stack = EnginePtr->Content().Assets();
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
            found.push_back({ path, Vocabulary->Inspect(*library, &stack.Registry,
                                                         &stack.DataAssets) });
        }
        return found;
    }));
    UiFeature->AddPanel(std::make_unique<DiagnosticsPanel>(
        *Session, View, [this](const std::string& path) {
            if (const DocumentEntry* entry = Library.Find("asset://" + path))
                OpenInEditor(*entry);
        }));

    auto statusBar = std::make_shared<ShojiStatusBar>(*Session, Library, *Watch);
    UiFeature->AddChrome([statusBar] { statusBar->Draw(); });

    renderer.StageFeature(std::move(uiFeature), FeatureRegistration{ .Id = kUiFeatureId });

    std::vector<std::string_view> failed;
    if (!renderer.CommitStagedFeatures(&failed))
        std::fprintf(stderr, "[shoji] render feature batch was refused; the editor runs without its own features\n");
    const auto didFail = [&failed](std::string_view id) {
        return std::find(failed.begin(), failed.end(), id) != failed.end();
    };
    if (didFail(kUiFeatureId))
    {
        std::fprintf(stderr, "[shoji] UI feature failed to set up; panels are unavailable\n");
        UiFeature = nullptr;
    }
    if (didFail(kTargetFeatureId))
        Target = nullptr;
}

void ShojiServices::RegisterSystems(EngineSchedule& schedule)
{
    schedule.Register<FrameHook>([this] { ProcessFrame(); });
}

void ShojiServices::HandlePlatformEvent(PlatformEventContext& ctx)
{
    // The editor's own shortcuts, but never while a panel's field is taking
    // text. The preview surface has already had its chance at this event: what
    // it consumed never got here.
    const bool typing = UiFeature != nullptr && UiFeature->GetInputCapture().Keyboard;
    if (!typing && ctx.Event.type == SDL_EVENT_KEY_DOWN && !ctx.Event.key.repeat
        && (ctx.Event.key.mod & SDL_KMOD_CTRL) != 0)
    {
        switch (ctx.Event.key.scancode)
        {
        case SDL_SCANCODE_S:
        {
            std::string error;
            (void)SaveModel(&error);
            ctx.Handled = true;
            return;
        }
        case SDL_SCANCODE_R:
            RescanLibrary();
            ctx.Handled = true;
            return;
        case SDL_SCANCODE_I:
            if (Session)
            {
                Session->SetPointerMode(Session->Mode() == UiPreviewSession::PointerMode::Inspect
                                            ? UiPreviewSession::PointerMode::Interact
                                            : UiPreviewSession::PointerMode::Inspect);
            }
            ctx.Handled = true;
            return;
        default:
            break;
        }
    }
    if (UiFeature != nullptr)
        UiFeature->ProcessSdlEvent(ctx.Event);
}

void ShojiServices::ProcessFrame()
{
    if (Watch)
        (void)Watch->Poll(std::chrono::steady_clock::now());
    if (!Session)
        return;

    UiService* ui = EnginePtr->TryUi();
    // The keyboard follows the pointer: while it is over the preview the
    // document has it, and the moment it leaves the editor's shortcuts are
    // back. Read from the layer, because the events the document consumed
    // never reached this side.
    if (ui != nullptr && Session->Mode() == UiPreviewSession::PointerMode::Interact)
        Session->SetActivated(ui->IsPointerOver(Session->Surface()));

    if (View.EditorTheme != EditorThemeApplied || View.EditorTheme)
    {
        // Republished every frame it is on: SetHostStyleSheet compares before
        // it restyles, so an unchanged theme costs a comparison.
        Session->SetHostTheme(View.EditorTheme ? std::optional<std::string>(BuildAuthoredThemeStyleSheet())
                                               : std::nullopt);
        EditorThemeApplied = View.EditorTheme;
    }

    Session->Poll();
    UpdateTitle();
}

void ShojiServices::OpenDocument(const std::string& packagePath)
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
                std::fprintf(stderr, "[shoji] %s: %s\n", packagePath.c_str(), error.c_str());
        }
    }
    View.Hovered = {};
    View.Selected = {};
    Session->ClearDiagnostics();
    Session->ClearActions();
    if (!Session->Open(packagePath, std::move(model)))
        std::fprintf(stderr, "[shoji] '%s' did not open; see Diagnostics\n", packagePath.c_str());
}

void ShojiServices::RescanLibrary()
{
    Library.Rescan();
    if (Watch)
        Watch->Rescan();
}

bool ShojiServices::SaveModel(std::string* error)
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

void ShojiServices::ResetModel()
{
    if (!Session || !Session->IsOpen())
        return;
    OpenDocument(Session->PackagePath());
}

void ShojiServices::OpenInEditor(const DocumentEntry& entry)
{
    // The author's editor: $VISUAL, then $EDITOR, then the desktop's handler.
    const char* editor = std::getenv("VISUAL");
    if (editor == nullptr || editor[0] == '\0')
        editor = std::getenv("EDITOR");
    std::string binary = editor != nullptr && editor[0] != '\0' ? editor : "xdg-open";
    long pid = 0;
    std::string error;
    if (!SpawnProcess(binary, { entry.SourcePath().string() }, "", pid, &error))
        std::fprintf(stderr, "[shoji] could not open '%s' in '%s': %s\n",
                     entry.SourcePath().string().c_str(), binary.c_str(), error.c_str());
}

void ShojiServices::UpdateTitle()
{
    std::string title = "Shoji";
    if (Session && Session->IsOpen())
        title += " - " + Session->PackagePath();
    if (title != LastWindowTitle)
    {
        Window->SetTitle(title);
        LastWindowTitle = title;
    }
}
