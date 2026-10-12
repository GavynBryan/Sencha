#include "app/KyusuApp.h"
#include "ui/EditorUiStyle.h"
#include <ui/UiService.h>
#include <render/feature/UiRenderFeature.h>

#include "app/ProjectSession.h"
#include "project/ProjectFiles.h"
#include "actions/ActionOffer.h"
#include "app/SynthesizedInput.h"
#include "app/WorkspaceKinds.h"
#include "render/RenderFeatureDetach.h"
#include "input/KeymapFile.h"
#include "input/SdlEventTranslation.h"
#include "ui/UiScaleChoice.h"
#include "workspaces/WorkspaceHost.h"

#include <app/Engine.h>
#include <app/EngineSchedule.h>
#include <app/RuntimeContent.h>
#include <assets/texture/Image.h>
#include <assets/texture/ImageLoader.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/console/ConsoleTypes.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/PresentationTarget.h>
#include <graphics/vulkan/PresentationWindows.h>
#include <graphics/vulkan/Renderer.h>
#include <platform/PlatformServices.h>
#include <platform/SdlWindow.h>
#include <platform/SdlWindowService.h>
#include <platform/WindowCreateInfo.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <cstdlib>
#include <memory>
#include <optional>
#include <span>
#include <string>

#ifndef SENCHA_EDITOR_BRAND_DIR
#define SENCHA_EDITOR_BRAND_DIR "."
#endif

namespace
{
constexpr std::string_view kContentUiFeatureId = "content_ui";
constexpr std::string_view kMotionCVar = "editor.ui.motion";
constexpr std::string_view kScaleCVar = "editor.ui.scale";

// Platform branding, not theme art: the icon is the product's and never
// changes with a theme, so it is set once and forgotten. SDL copies the
// surface, which only borrows our pixels, so both are done with here.
void ApplyWindowIcon(SdlWindow& window)
{
    const std::string path = std::string(SENCHA_EDITOR_BRAND_DIR) + "/kyusu-icon.png";
    const std::optional<Image> icon = LoadImageFromFile(path, /*srgb*/ true);
    if (!icon.has_value() || !icon->IsValid())
        return;
    SDL_Surface* surface = SDL_CreateSurfaceFrom(static_cast<int>(icon->Width), static_cast<int>(icon->Height),
                                                 SDL_PIXELFORMAT_RGBA32,
                                                 const_cast<unsigned char*>(icon->Pixels.data()),
                                                 static_cast<int>(icon->Width) * 4);
    if (surface == nullptr)
        return;
    SDL_SetWindowIcon(window.GetHandle(), surface);
    SDL_DestroySurface(surface);
}

// Workspaces are opened and closed while the application runs, after the
// schedule is fixed, so they are ticked by one system rather than each
// registering its own.
struct WorkspaceTickSystem
{
    WorkspaceTickSystem(WorkspaceHost& host, bool& framesStarted, std::function<void()> atBoundary)
        : Host(host)
        , FramesStarted(framesStarted)
        , AtBoundary(std::move(atBoundary))
    {
    }

    void FrameUpdate(FrameUpdateContext& ctx)
    {
        FramesStarted = true;
        AtBoundary();
        Host.Tick(ctx);
    }

    WorkspaceHost& Host;
    bool& FramesStarted;
    // What the application settles at the frame boundary before the host does.
    std::function<void()> AtBoundary;
};
} // namespace

KyusuApp::KyusuApp(std::optional<std::string> projectPath)
    : ProjectPath(std::move(projectPath))
{
}

KyusuApp::~KyusuApp() = default;

bool KyusuApp::OpensLevel() const
{
    // A bare game module still edits levels, beside their files.
    const char* module = std::getenv("SENCHA_GAME_MODULE");
    return ProjectPath.has_value() || (module != nullptr && module[0] != '\0');
}

void KyusuApp::OnConfigure(GameConfigureContext& ctx)
{
    ctx.Config.Window.Title = "Kyusu";
    // The editor draws its own caption; the window keeps the platform frame
    // only where client decorations are unavailable.
    ctx.Config.Window.ClientDecorations = true;
    // An editor is a tool, not an application a player sits in front of: no
    // pause shell, no options page, no Back action reading its Escape.
    ctx.Config.Runtime.ApplicationShell = false;
    // The project session mounts the project's content roots into the engine's
    // stack; the runtime's default mount of `assets` beside the working
    // directory would be the wrong directory.
    ctx.Config.Runtime.ContentRoots.clear();
    // The level views re-upload every brush once per view each frame into one
    // scratch slice, which the game's 1 MB default overflows on real scenes.
    if (OpensLevel())
        ctx.Config.Graphics.FrameScratchBytesPerFrame = 64ull * 1024 * 1024;
}

void KyusuApp::OnStart(GameStartupContext&)
{
    Engine& engine = GetEngine();
    SdlWindow* window = engine.Platform().Windows.GetPrimaryWindow();
    if (window == nullptr)
        return;
    ApplyWindowIcon(*window);

    const bool opensLevel = OpensLevel();
    GraphicsServices& graphics = engine.Graphics();
    const PresentationId mainWindow = graphics.MainRenderer.PrimaryPresentation();
    Session = std::make_unique<ProjectSession>(engine, std::move(ProjectPath));
    PublishAdoptionProblems();
    MainWindowActions = std::make_unique<WindowActions>();
    MainWindowActions->Application = MainWindowActions->Stack.Push(ApplicationActions, ActionRank::Application);
    Workspaces = std::make_unique<WorkspaceHost>(BuildWorkspaceKinds(engine, *window, *Session, Catalog, Prompts, Problems, Notices),
                                                 Session->Project() != nullptr, mainWindow);

    Theme = std::make_unique<ThemeSheet>(engine.Console().Registry(), Problems, SENCHA_EDITOR_THEME_DIR);
    // The window's content layer draws before the engine's own authored UI:
    // content, then the shell.
    if (UiService* authored = engine.TryUi())
    {
        Renderer& renderer = graphics.MainRenderer;
        (void)renderer.StageFeature(std::make_unique<UiRenderFeature>(*authored, engine.Content().Assets().Textures.get(),
                                                                      0, kContentUiLayer),
                                    FeatureRegistration{ .Id = kContentUiFeatureId });
        std::vector<std::string_view> failed;
        if (!renderer.CommitStagedFeatures(&failed) || !failed.empty())
            std::fprintf(stderr, "[kyusu] the content layer failed to set up; views will not show\n");
    }

    Workspaces->SetWindowOpener([this](const WorkspaceKind& kind) { return OpenDetachedWindow(kind); });
    Workspaces->SetPlacedListener([this](const WorkspaceKind& kind, IWorkspace& workspace) {
        if (const ShellWindow* windowShell = WindowShellFor(Workspaces->WindowOf(kind.Id)))
            workspace.MoveTo(windowShell->Binding());
    });
    Workspaces->SetWindowEmptiedListener([this](PresentationId emptied) { CloseDetachedWindow(emptied); });

    InstallDocumentActions();
    DefineApplicationActions();
    Theme->DefineActions(Catalog, ApplicationActions);
    if (engine.TryUi() != nullptr)
        MainWindowShell = MakeShell(*window, mainWindow, *MainWindowActions, [&engine] { engine.RequestExit(); });
    RegisterWorkspaceCommands();
    Input.Register(engine.Console().Registry(), [window] { return window->GetId(); });
    // Before the startup script runs, so an argv +editor.open or +cook finds
    // the level it acts on. Without a project to edit, choose one.
    (void)Workspaces->Open(opensLevel ? "level" : "project");
}

PresentationId KyusuApp::OpenDetachedWindow(const WorkspaceKind& kind)
{
    return OpenWindow("Kyusu - " + kind.DisplayName, ShellRole::Workspaces);
}

PresentationId KyusuApp::OpenWindow(std::string title, ShellRole role)
{
    Engine& engine = GetEngine();
    GraphicsServices& graphics = engine.Graphics();
    SdlWindowService& windows = engine.Platform().Windows;
    const SdlWindow* primary = windows.GetPrimaryWindow();
    if (primary == nullptr || engine.TryUi() == nullptr)
        return {};
    WindowCreateInfo info;
    info.Title = std::move(title);
    const WindowExtent extent = primary->GetExtent();
    info.Width = extent.Width;
    info.Height = extent.Height;
    info.ClientDecorations = engine.Config().Window.ClientDecorations;
    // With the stencil aspect authored UI clips rounded shapes against.
    const PresentationId presentation =
        OpenPresentationWindow(graphics, windows, info, PresentationDesc{ .DepthStencil = true });
    if (presentation.IsNull())
        return {};
    SdlWindow& window = graphics.Frames.FindPresentation(presentation)->Window();
    ApplyWindowIcon(window);

    // Content, then the shell, as in the main window: features in one phase
    // draw in the order they were added.
    UiService& ui = *engine.TryUi();
    UiRenderFeature* contentFeature = graphics.MainRenderer.AddFeature(
        std::make_unique<UiRenderFeature>(ui, engine.Content().Assets().Textures.get(), window.GetId(), kContentUiLayer),
        RenderFeatureScope::For(presentation));
    UiRenderFeature* uiFeature = graphics.MainRenderer.AddFeature(
        std::make_unique<UiRenderFeature>(ui, engine.Content().Assets().Textures.get(), window.GetId()),
        RenderFeatureScope::For(presentation));
    auto actions = std::make_unique<WindowActions>();
    actions->Application = actions->Stack.Push(ApplicationActions, ActionRank::Application);
    std::unique_ptr<ShellWindow> windowShell = MakeShell(
        window, presentation, *actions, [this, presentation] { WindowsToClose.push_back(presentation); }, role);
    Detached.push_back(DetachedWindow{ presentation, window.GetId(), std::move(actions), std::move(windowShell),
                                       uiFeature, contentFeature });
    return presentation;
}

void KyusuApp::CloseDetachedWindow(PresentationId window)
{
    const auto it = std::find_if(Detached.begin(), Detached.end(),
                                 [window](const DetachedWindow& entry) { return entry.Presentation == window; });
    if (it == Detached.end())
        return;
    Engine& engine = GetEngine();
    // Everything recording into it first: a presentation something still
    // records into refuses to go.
    it->WindowShell.reset();
    it->Actions.reset();
    if (it->Ui != nullptr)
        DetachRenderFeature(engine, it->Ui);
    if (it->ContentUi != nullptr)
        DetachRenderFeature(engine, it->ContentUi);
    (void)ClosePresentationWindow(engine.Graphics(), engine.Platform().Windows, window);
    Detached.erase(it);
}

void KyusuApp::CloseRequestedWindows()
{
    std::vector<PresentationId> closing = std::move(WindowsToClose);
    WindowsToClose.clear();
    // Closing a window brings its workspaces back, which empties and closes
    // it; one that held none, the shelf's own, closes as it is.
    for (const PresentationId window : closing)
    {
        Workspaces->Gather(window);
        CloseDetachedWindow(window);
        if (window == ShelfWindow)
            ShelfWindow = {};
    }
}

std::vector<std::filesystem::path> KyusuApp::ContentRoots() const
{
    std::vector<std::filesystem::path> roots;
    if (const ProjectDescriptor* project = Session != nullptr ? Session->Project() : nullptr)
        roots.assign(project->ContentRoots.begin(), project->ContentRoots.end());
    return roots;
}

void KyusuApp::PublishAdoptionProblems()
{
    std::vector<Problem> problems;
    for (const std::string& error : Session->AdoptionErrors())
        problems.push_back({ .Severity = ProblemSeverity::Error, .Message = error });
    for (const std::filesystem::path& sidecar : Session->Adoption().Unpaired)
        problems.push_back({ .Message = sidecar.filename().string()
                                        + " was left behind by a file that moved, and no one file of its name can be it",
                             .Location = sidecar.generic_string() });
    Problems.Publish("assets", std::move(problems));
}

std::vector<std::filesystem::path> KyusuApp::OtherContentRoots() const
{
    const std::vector<std::filesystem::path> project = ContentRoots();
    std::vector<std::filesystem::path> others;
    for (const ContentRootPaths& root : GetEngine().Content().Roots())
    {
        std::error_code ec;
        if (std::ranges::none_of(project, [&](const std::filesystem::path& own) {
                return std::filesystem::equivalent(root.Authored, own, ec);
            }))
            others.push_back(root.Authored);
    }
    return others;
}

ShellWindow* KyusuApp::WindowShellFor(PresentationId window) const
{
    if (Workspaces != nullptr && window == Workspaces->MainWindow())
        return MainWindowShell.get();
    for (const DetachedWindow& entry : Detached)
        if (entry.Presentation == window)
            return entry.WindowShell.get();
    return nullptr;
}

PresentationId KyusuApp::WindowOf(std::uint32_t windowId) const
{
    for (const DetachedWindow& entry : Detached)
        if (entry.WindowId == windowId)
            return entry.Presentation;
    return Workspaces->MainWindow();
}

void KyusuApp::InstallDocumentActions()
{
    DocumentSourceSet& documents = Session->Documents();
    ExitHold = std::make_unique<DocumentExitHold>(GetEngine(), documents, Prompts);
    SaveFollowUp = std::make_unique<DocumentSaveFollowUp>(documents, Problems, Prompts);
    CloseAsker = Prompts.MakeAsker();

    documents.SetStepObserver([this](const DocumentRef& document) {
        for (const WorkspaceHost::Entry& entry : Workspaces->OpenWorkspaces())
        {
            if (!entry.Instance->OwnsDocument(document))
                continue;
            // Nothing changes out of sight: the workspace comes forward, at the
            // frame boundary since a step can arrive mid-event.
            Workspaces->Request({ WorkspaceAction::Activate, entry.Kind->Id });
            entry.Instance->RevealDocument(document);
            return;
        }
    });

    Workspaces->SetCloseGuard([this, &documents](const WorkspaceKind& kind, IWorkspace& workspace) {
        std::vector<DocumentRef> changed;
        for (const DocumentRef& document : documents.ChangedDocuments())
            if (workspace.OwnsDocument(document))
                changed.push_back(document);
        if (changed.empty())
            return true;
        if (!CloseAsker.IsAsking())
            AskBeforeClosing(kind, std::move(changed));
        return false;
    });
}

void KyusuApp::Undo()
{
    IWorkspace* active = Workspaces->Active();
    if (active == nullptr || !active->UndoStagedEdit())
        Session->Documents().Undo();
}

bool KyusuApp::CanUndo() const
{
    const IWorkspace* active = Workspaces->Active();
    return Session->Documents().CanUndo() || (active != nullptr && active->HasStagedEdit());
}

void KyusuApp::DefineApplicationActions()
{
    // Before anything is defined, so every action -- a workspace's too, when
    // it first opens -- is also a console command of the same id, performed by
    // whatever claims it in the window in use.
    ConsoleRegistry& registry = GetEngine().Console().Registry();
    Catalog.SetDefinedObserver([this, &registry](const ActionDefinition& action) {
        (void)registry.RegisterCommand({
            .Name = action.Id,
            .Owner = "editor.actions",
            .Usage = action.Id,
            .Help = action.Label + " (" + action.Category + ")",
            .Callback = [this, id = action.Id](ConsoleExecutionContext&, std::span<const std::string>) {
                ConsoleResult result;
                SyncActionLayers();
                const WorkspaceKind* active = Workspaces->ActiveKind();
                const PresentationId window = active != nullptr ? Workspaces->WindowOf(active->Id)
                                                                : Workspaces->MainWindow();
                if (!ActionsFor(window).Stack.Invoke(id))
                {
                    result.Status = ConsoleStatus::ExecutionFailed;
                    result.Error("nothing in use performs '" + id + "'");
                    return result;
                }
                result.Info("done");
                return result;
            },
        });
    });

    std::string error;
    const auto keymap = LoadKeymapOverrides("keybinds.json", &error);
    if (!error.empty())
        std::fprintf(stderr, "[kyusu] %s\n", error.c_str());
    (void)Catalog.ApplyKeymap(keymap);

    DocumentSourceSet& documents = Session->Documents();
    const auto offer = [this](std::string id, std::string label, std::vector<KeyChord> chords,
                              ActionHandler handler) {
        std::string category = id.substr(0, id.find('.')) == "edit" ? "Edit" : "File";
        OfferAction(Catalog, ApplicationActions,
                    { .Id = std::move(id), .Label = std::move(label), .Category = std::move(category),
                      .DefaultChords = std::move(chords) },
                    std::move(handler));
    };
    offer("edit.undo", "Undo", { { SDLK_Z, { .Ctrl = true } } },
          { .Invoke = [this] { Undo(); }, .Enabled = [this] { return CanUndo(); } });
    offer("edit.redo", "Redo", { { SDLK_Z, { .Ctrl = true, .Shift = true } }, { SDLK_Y, { .Ctrl = true } } },
          { .Invoke = [&documents] { documents.Redo(); }, .Enabled = [&documents] { return documents.CanRedo(); } });
    offer("file.save_all", "Save All", { { SDLK_S, { .Ctrl = true, .Shift = true } } },
          { .Invoke = [&documents] { (void)documents.SaveAll(); } });
    // One shelf window: asked again while it is open, it comes to the front.
    OfferAction(Catalog, ApplicationActions,
                { .Id = "shelf.window", .Label = "Shelf Window", .Category = "View", .Icon = IconId::Folder },
                { .Invoke = [this] {
                    if (ShelfWindow.IsNull())
                        ShelfWindow = OpenWindow("Kyusu - Shelf", ShellRole::Shelf);
                    else if (auto* presentation = GetEngine().Graphics().Frames.FindPresentation(ShelfWindow))
                        (void)SDL_RaiseWindow(presentation->Window().GetHandle());
                } });

    registry.RegisterCVar({
        .Name = std::string(kScaleCVar),
        .Owner = "editor",
        .Type = CVarType::Double,
        .DefaultValue = 0.0,
        .CurrentValue = 0.0,
        .Flags = CVarFlags::Archive,
        .Help = "Editor UI scale. 0 follows each window's display scale.",
        .Source = { "editor" },
        .Min = 0.0,
        .Max = 4.0,
    });
    registry.RegisterCVar({
        .Name = std::string(kMotionCVar),
        .Owner = "editor",
        .Type = CVarType::Bool,
        .DefaultValue = true,
        .CurrentValue = true,
        .Flags = CVarFlags::Archive,
        .Help = "Editor transitions play; off, everything changes at once.",
        .Source = { "editor" },
    });
    const auto motion = [&registry] {
        const CVarMetadata* var = registry.FindCVar(kMotionCVar);
        const bool* on = var != nullptr ? std::get_if<bool>(&var->CurrentValue) : nullptr;
        return on == nullptr || *on;
    };
    OfferAction(Catalog, ApplicationActions, { .Id = "view.motion", .Label = "Motion", .Category = "View" },
                { .Invoke = [&registry, motion] {
                      (void)registry.SetCVar(kMotionCVar, !motion(), ConsoleValueSource{ .Description = "view action" },
                                             ConsolePhase::GameplayStarted);
                  },
                  .Checked = motion });
}

KyusuApp::WindowActions& KyusuApp::ActionsFor(PresentationId window)
{
    for (DetachedWindow& entry : Detached)
        if (entry.Presentation == window && entry.Actions != nullptr)
            return *entry.Actions;
    return *MainWindowActions;
}

void KyusuApp::SyncActionLayers()
{
    const auto sync = [this](PresentationId window, WindowActions& actions) {
        IWorkspace* active = Workspaces->ActiveIn(window);
        if (active == actions.Layered)
            return;
        actions.Workspace.Reset();
        actions.Layered = active;
        if (active != nullptr)
            if (ActionContext* context = active->Actions())
                actions.Workspace = actions.Stack.Push(*context, ActionRank::Workspace);
    };
    if (MainWindowActions != nullptr)
        sync(Workspaces->MainWindow(), *MainWindowActions);
    for (DetachedWindow& entry : Detached)
        if (entry.Actions != nullptr)
            sync(entry.Presentation, *entry.Actions);
}

void KyusuApp::AskBeforeClosing(const WorkspaceKind& kind, std::vector<DocumentRef> changed)
{
    AskAboutDocuments(CloseAsker, "Close " + kind.DisplayName, changed,
                      [this, id = kind.Id, changed](DirtyDisposition disposition) {
                          DocumentSourceSet& documents = Session->Documents();
                          for (const DocumentRef& document : changed)
                              if (disposition == DirtyDisposition::Discard)
                                  document.Source->DiscardDocument(document.Key);
                              else
                                  (void)documents.Save(document);
                          // Asked again, by the close guard, if a save left
                          // something unsaved; the save's follow-up says what.
                          Workspaces->Request({ WorkspaceAction::Close, id });
                      });
}

void KyusuApp::RegisterWorkspaceCommands()
{
    ConsoleRegistry& registry = GetEngine().Console().Registry();
    // Each editor is gone to by an action of its own, so the hatch, the
    // palette and the console all reach it the same way. Before the first
    // frame a startup script waits on it, so it is placed at once.
    for (const WorkspaceKind& kind : Workspaces->Kinds())
        if (Workspaces->IsOffered(kind.Id))
            OfferAction(Catalog, ApplicationActions,
                        { .Id = "workspace.open." + kind.Id, .Label = "Go to " + kind.DisplayName,
                          .Category = "Workspace" },
                        { .Invoke = [this, id = kind.Id] {
                              Workspaces->Request({ WorkspaceAction::Open, id });
                              if (!FramesStarted)
                                  Workspaces->ApplyRequests();
                          },
                          .Checked = [this, id = kind.Id] {
                              const WorkspaceKind* active = Workspaces->ActiveKind();
                              return active != nullptr && active->Id == id;
                          } });
    registry.RegisterCommand({
        .Name = "shell.popover",
        .Owner = "editor",
        .Usage = "shell.popover <hatch|tools|problems|category:<name>|none>",
        .Help = "Open one of the main window's rail popovers, or close it.",
        .Callback = [this](ConsoleExecutionContext&, std::span<const std::string> args) {
            ConsoleResult result;
            if (MainWindowShell != nullptr)
                MainWindowShell->ShowPopover(args.empty() ? std::string_view{} : std::string_view(args[0]));
            result.Info("done");
            return result;
        },
    });
    registry.RegisterCommand({
        .Name = "editor.open_asset",
        .Owner = "editor",
        .Usage = "editor.open_asset <asset path>",
        .Help = "Open a content file in the workspace that opens its kind, or bring it forward if open.",
        .Callback = [this](ConsoleExecutionContext&, std::span<const std::string> args) {
            ConsoleResult result;
            const std::optional<ProjectFile> file =
                args.size() == 1 ? FindProjectFile(ContentRoots(), args[0]) : std::nullopt;
            if (!file.has_value() || !Workspaces->RequestOpen(*file))
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("expected a content file some workspace opens");
                return result;
            }
            result.Info("opening '" + args[0] + "'");
            return result;
        },
    });
    registry.RegisterCommand({
        .Name = "editor.window.screenshot",
        .Owner = "editor",
        .Usage = "editor.window.screenshot <kind|shelf> <path.png> [frame]",
        .Help = "Write the window showing an open workspace, or the shelf's own window, to a PNG, once the "
                "renderer has drawn [frame] frames.",
        .Callback = [this](ConsoleExecutionContext&, std::span<const std::string> args) {
            ConsoleResult result;
            const PresentationId window = args.size() < 2      ? PresentationId{}
                                        : args[0] == "shelf" ? ShelfWindow
                                                             : Workspaces->WindowOf(args[0]);
            std::uint64_t atFrame = 0;
            if (args.size() == 3)
                atFrame = std::strtoull(args[2].c_str(), nullptr, 10);
            if (window.IsNull() || args.size() > 3
                || !GetEngine().Graphics().MainRenderer.CaptureFrame(window, args[1], atFrame))
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("expected an open workspace, a path, and optionally a frame");
                return result;
            }
            result.Info("capture armed for '" + args[0] + "'");
            return result;
        },
    });
}

void KyusuApp::OnRegisterSystems(SystemRegisterContext& ctx)
{
    if (Workspaces)
        ctx.Schedule.Register<WorkspaceTickSystem>(*Workspaces, FramesStarted, [this] {
            CloseRequestedWindows();
            SyncActionLayers();
            UpdateShells();
            Input.Advance();
        });
}

void KyusuApp::OnPlatformEvent(PlatformEventContext& ctx)
{
    if (Workspaces == nullptr)
        return;
    // Every window's events arrive here; each goes to the workspace showing in
    // its window.
    const PresentationId window = WindowOf(ctx.WindowId);
    if (window != Workspaces->MainWindow())
    {
        if (ctx.Event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        {
            WindowsToClose.push_back(window);
            ctx.Handled = true;
            return;
        }
    }
    // The workspace a window shows is the one in use once its window has focus.
    if (ctx.Event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
        if (const WorkspaceKind* kind = Workspaces->ActiveKindIn(window))
            Workspaces->Request({ WorkspaceAction::Activate, kind->Id });
    // A file from outside dropped on a window joins the project there.
    if (ctx.Event.type == SDL_EVENT_DROP_FILE && ctx.Event.drop.data != nullptr)
        if (ShellWindow* shell = WindowShellFor(window))
        {
            const std::filesystem::path dropped(ctx.Event.drop.data);
            shell->Import({ &dropped, 1 });
            ctx.Handled = true;
            return;
        }
    // A number being dragged in a sheet, or a wheel the shell has up, holds the
    // pointer and the keys until let go.
    if (ShellWindow* shell = WindowShellFor(window); shell != nullptr && shell->HoldsInput())
        if (const std::optional<InputEvent> event = TranslateSdlEvent(ctx.Event);
            event.has_value() && shell->TakeHeldInput(*event))
        {
            ctx.Handled = true;
            return;
        }
    IWorkspace* active = Workspaces->ActiveIn(window);
    if (active != nullptr && active->ClaimPlatformEvent(ctx))
    {
        ctx.Handled = true;
        return;
    }
    if (active != nullptr)
        active->HandlePlatformEvent(ctx);
    // Keys the active workspace left alone, unless a text field has them, are
    // the window's actions.
    const UiService* ui = GetEngine().TryUi();
    if (ctx.Handled || (ui != nullptr && ui->Capture(ctx.WindowId).Keyboard))
        return;
    const std::optional<InputEvent> event = TranslateSdlEvent(ctx.Event);
    const KeyDownEvent* key = event.has_value() ? std::get_if<KeyDownEvent>(&*event) : nullptr;
    if (key == nullptr)
        return;
    SyncActionLayers();
    if (ActionsFor(window).Stack.Dispatch(Catalog, KeyChord{ key->Key, key->Modifiers }))
        ctx.Handled = true;
}

void KyusuApp::OnShutdown(GameShutdownContext&)
{
    // Drain the GPU first. The frame loop returns without waiting, so the last
    // frames it submitted may still be executing, and the workspaces free
    // render features as they close.
    if (GraphicsServices* graphics = GetEngine().TryGraphics())
        graphics->WaitIdle();

    // Before the engine disconnects the asset stack they lease from, and the
    // workspaces first: their documents hold module code. Detached ones come
    // home first, so their windows close while they still live.
    if (Workspaces)
    {
        while (!Detached.empty())
            Workspaces->Gather(Detached.back().Presentation);
        MainWindowShell.reset();
        if (MainWindowActions != nullptr)
        {
            MainWindowActions->Workspace.Reset();
            MainWindowActions->Layered = nullptr;
        }
        Workspaces->CloseAll();
    }
    Workspaces.reset();
    // Both read the journal the session is about to take with it.
    CloseAsker = {};
    SaveFollowUp.reset();
    ExitHold.reset();
    Session.reset();
}

std::unique_ptr<ShellWindow> KyusuApp::MakeShell(SdlWindow& window, PresentationId presentation,
                                                 WindowActions& actions, std::function<void()> close, ShellRole role)
{
    Engine& engine = GetEngine();
    return std::make_unique<ShellWindow>(ShellWindowServices{
        .Ui = engine.Ui(),
        .Workspaces = *Workspaces,
        .Catalog = Catalog,
        .Actions = actions.Stack,
        .Problems = Problems,
        .Notices = Notices,
        .Prompts = Prompts,
        .Window = window,
        .Pointer = engine.PlatformEvents(),
        .Presentation = presentation,
        .CloseWindow = std::move(close),
        .Title = "Kyusu",
        .Content = { .ContentRoots = ContentRoots(),
                     .OtherRoots = OtherContentRoots(),
                     .Pictures = Session != nullptr ? &Session->Pictures() : nullptr,
                     .References = Session != nullptr ? &Session->References() : nullptr,
                     .Files = Session != nullptr ? Session->Files() : nullptr },
        .Role = role,
    });
}

void KyusuApp::UpdateShells()
{
    const double now = NoticeClockNow();
    ShellPreferences preferences;
    const ConsoleRegistry& registry = GetEngine().Console().Registry();
    if (const CVarMetadata* var = registry.FindCVar(kScaleCVar))
        if (const double* value = std::get_if<double>(&var->CurrentValue))
            preferences.Scale = *value;
    if (const CVarMetadata* var = registry.FindCVar(kMotionCVar))
        if (const bool* value = std::get_if<bool>(&var->CurrentValue))
            preferences.Motion = *value;
    if (const SdlWindow* primary = GetEngine().Platform().Windows.GetPrimaryWindow())
        EditorUi::UiScale = ChooseUiScale(preferences.Scale, primary->GetDisplayScale());
    Theme->Update(GetEngine().TryUi());
    if (Session != nullptr)
        Session->UpdatePictures();
    LoadProblems.Update(GetEngine().Content().Assets().Assets);
    if (MainWindowShell != nullptr)
        MainWindowShell->Update(now, preferences);
    for (DetachedWindow& entry : Detached)
        if (entry.WindowShell != nullptr)
            entry.WindowShell->Update(now, preferences);
}
