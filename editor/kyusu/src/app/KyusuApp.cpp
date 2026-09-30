#include "app/KyusuApp.h"
#include "ui/EditorUiStyle.h"
#include <chrono>
#include <ui/UiService.h>
#include <render/feature/UiRenderFeature.h>

#include "app/ProjectSession.h"
#include "app/SynthesizedInput.h"
#include "app/WorkspaceKinds.h"
#include "render/RenderFeatureDetach.h"
#include "input/KeymapFile.h"
#include "input/SdlEventTranslation.h"
#include "ui/EditorThemeStartup.h"
#include "ui/EditorUiFeature.h"
#include "workspaces/WorkspaceHost.h"

#include <app/Engine.h>
#include <app/EngineSchedule.h>
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
constexpr std::string_view kEditorUiFeatureId = "editor_ui";
constexpr std::string_view kContentUiFeatureId = "content_ui";
constexpr std::string_view kEditorUiDependsOn[] = { kContentUiFeatureId };

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
    // The editor is its own ImGui host; a window holds only one ImGui context,
    // so the engine's default debug overlay must not be created.
    ctx.Config.Console.UiEnabled = false;
    // An editor is a tool, not an application a player sits in front of: no
    // pause shell, no options page, no Back action reading its Escape.
    ctx.Config.Runtime.ApplicationShell = false;
    // The project session mounts the project's content roots into the engine's
    // stack; the runtime's default mount of `assets` beside the working
    // directory would be the wrong directory.
    ctx.Config.Runtime.ContentRoots.clear();
    // Each frame the level editor re-uploads every brush wireframe/solid/overlay
    // once per viewport (up to 4) into a single frame-scratch slice; the game's
    // 1 MB default overflows on real scenes (dropped draws look like warped or
    // missing geometry). Only a session that opens a level pays for more.
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
    MainWindowActions = std::make_unique<WindowActions>();
    MainWindowActions->Application = MainWindowActions->Stack.Push(ApplicationActions, ActionRank::Application);
    Workspaces = std::make_unique<WorkspaceHost>(BuildWorkspaceKinds(engine, *window, *Session, Catalog, Prompts),
                                                 Session->Project() != nullptr, mainWindow);

    // Chrome theme (behavior from data), loaded before the UI feature applies
    // the ImGui style.
    ApplyEditorThemeFromConsole(engine.Console());
    auto ui = std::make_unique<EditorUiFeature>(engine, *window, graphics.Instance, graphics.Frames,
                                                "kyusu.imgui.ini");
    ui->SetWorkspaceHost(*Workspaces, mainWindow);
    ui->SetActionCatalog(&Catalog);
    Renderer& renderer = graphics.MainRenderer;
    // The window's content layer draws before the panels, which draw before
    // the engine's own authored UI: content, then panels, then the shell.
    std::span<const std::string_view> uiDependsOn;
    if (UiService* authored = engine.TryUi())
    {
        (void)renderer.StageFeature(std::make_unique<UiRenderFeature>(*authored, engine.Content().Assets().Textures.get(),
                                                                      0, kContentUiLayer),
                                    FeatureRegistration{ .Id = kContentUiFeatureId });
        uiDependsOn = kEditorUiDependsOn;
    }
    Ui = renderer.StageFeature(std::move(ui), FeatureRegistration{ .Id = kEditorUiFeatureId, .DependsOn = uiDependsOn });
    std::vector<std::string_view> failed;
    if (!renderer.CommitStagedFeatures(&failed) || !failed.empty())
    {
        std::fprintf(stderr, "[kyusu] the editor UI failed to set up; nothing can be drawn\n");
        Ui = nullptr;
    }

    Workspaces->SetWindowOpener([this](const WorkspaceKind& kind) { return OpenDetachedWindow(kind); });
    Workspaces->SetPlacedListener([this](const WorkspaceKind& kind, IWorkspace& workspace) {
        const PresentationId placedIn = Workspaces->WindowOf(kind.Id);
        if (EditorUiFeature* shell = ShellFor(placedIn))
            workspace.Place(*shell);
        if (const ShellWindow* windowShell = WindowShellFor(placedIn))
            workspace.MoveTo(windowShell->Binding());
    });
    Workspaces->SetWindowEmptiedListener([this](PresentationId emptied) { CloseDetachedWindow(emptied); });

    InstallDocumentActions();
    DefineApplicationActions();
    if (Ui != nullptr)
    {
        Ui->DefineThemeActions();
        MainWindowActions->View = MainWindowActions->Stack.Push(Ui->ViewActionContext(), ActionRank::Window);
    }
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
    Engine& engine = GetEngine();
    if (Ui == nullptr)
        return {};
    GraphicsServices& graphics = engine.Graphics();
    SdlWindowService& windows = engine.Platform().Windows;
    WindowCreateInfo info;
    info.Title = "Kyusu - " + kind.DisplayName;
    const WindowExtent extent = Ui->GetWindow().GetExtent();
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

    // Content, then panels, then the shell, as in the main window: features
    // in one phase draw in the order they were added.
    UiRenderFeature* contentFeature = nullptr;
    if (UiService* ui = engine.TryUi())
        contentFeature = graphics.MainRenderer.AddFeature(
            std::make_unique<UiRenderFeature>(*ui, engine.Content().Assets().Textures.get(), window.GetId(),
                                              kContentUiLayer),
            RenderFeatureScope::For(presentation));
    auto shell = std::make_unique<EditorUiFeature>(*Ui, window);
    shell->SetWorkspaceHost(*Workspaces, presentation);
    shell->SetActionCatalog(&Catalog);
    EditorUiFeature* added = graphics.MainRenderer.AddFeature(std::move(shell), RenderFeatureScope::For(presentation));
    if (added == nullptr)
    {
        if (contentFeature != nullptr)
            DetachRenderFeature(engine, contentFeature);
        (void)ClosePresentationWindow(graphics, windows, presentation);
        return {};
    }
    auto actions = std::make_unique<WindowActions>();
    actions->Application = actions->Stack.Push(ApplicationActions, ActionRank::Application);
    actions->View = actions->Stack.Push(added->ViewActionContext(), ActionRank::Window);
    // The window's own authored UI: its shell, drawn by a UI feature of its own.
    UiRenderFeature* uiFeature = nullptr;
    if (UiService* ui = engine.TryUi())
        uiFeature = graphics.MainRenderer.AddFeature(
            std::make_unique<UiRenderFeature>(*ui, engine.Content().Assets().Textures.get(), window.GetId()),
            RenderFeatureScope::For(presentation));
    std::unique_ptr<ShellWindow> windowShell =
        MakeShell(window, presentation, *actions, [this, presentation] { WindowsToClose.push_back(presentation); });
    Detached.push_back(DetachedWindow{ presentation, window.GetId(), added, std::move(actions),
                                       std::move(windowShell), uiFeature, contentFeature });
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
    DetachRenderFeature(engine, it->Shell);
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
    // Closing a window brings its workspaces back, which empties and closes it.
    for (const PresentationId window : closing)
        Workspaces->Gather(window);
}

EditorUiFeature* KyusuApp::ShellFor(PresentationId window) const
{
    if (Workspaces != nullptr && window == Workspaces->MainWindow())
        return Ui;
    for (const DetachedWindow& entry : Detached)
        if (entry.Presentation == window)
            return entry.Shell;
    return nullptr;
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

EditorUiFeature* KyusuApp::ShellForWindowId(std::uint32_t windowId) const
{
    for (const DetachedWindow& entry : Detached)
        if (entry.WindowId == windowId)
            return entry.Shell;
    return Ui;
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
        std::string defineError;
        if (!Catalog.Define({ .Id = id, .Label = std::move(label),
                              .Category = id.substr(0, id.find('.')) == "edit" ? "Edit" : "File",
                              .DefaultChords = std::move(chords) }, &defineError))
            std::fprintf(stderr, "[kyusu] %s\n", defineError.c_str());
        ApplicationActions.Claim(std::move(id), std::move(handler));
    };
    offer("edit.undo", "Undo", { { SDLK_Z, { .Ctrl = true } } },
          { .Invoke = [this] { Undo(); }, .Enabled = [this] { return CanUndo(); } });
    offer("edit.redo", "Redo", { { SDLK_Z, { .Ctrl = true, .Shift = true } }, { SDLK_Y, { .Ctrl = true } } },
          { .Invoke = [&documents] { documents.Redo(); }, .Enabled = [&documents] { return documents.CanRedo(); } });
    offer("file.save", "Save", { { SDLK_S, { .Ctrl = true } } },
          { .Invoke = [this] {
                IWorkspace* active = Workspaces->Active();
                if (active != nullptr && active->View().File.Save)
                    active->View().File.Save();
            },
            .Enabled = [this] {
                IWorkspace* active = Workspaces->Active();
                return active != nullptr && active->View().File.Save != nullptr;
            } });
    offer("file.save_all", "Save All", { { SDLK_S, { .Ctrl = true, .Shift = true } } },
          { .Invoke = [&documents] { (void)documents.SaveAll(); } });
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
    std::vector<std::string> labels;
    for (const DocumentRef& document : changed)
        labels.push_back(document.Source->DocumentLabel(document.Key));
    CloseAsker.Ask({
        .Title = "Close " + kind.DisplayName,
        .Message = "Save changes before closing?",
        .Items = std::move(labels),
        .Choices = { { "save", "Save and close" },
                     { "discard", "Discard and close", ActionTone::Destructive },
                     { "cancel", "Keep editing" } },
        .Answer =
            [this, id = kind.Id, changed = std::move(changed)](std::string_view key) {
                if (key == "cancel")
                    return;
                DocumentSourceSet& documents = Session->Documents();
                for (const DocumentRef& document : changed)
                {
                    if (key == "discard")
                        document.Source->DiscardDocument(document.Key);
                    else
                        (void)documents.Save(document);
                }
                // Asked again, by the close guard, if a save left something
                // unsaved; the save's follow-up says what.
                Workspaces->Request({ WorkspaceAction::Close, id });
            },
    });
}

void KyusuApp::RegisterWorkspaceCommands()
{
    ConsoleRegistry& registry = GetEngine().Console().Registry();
    const auto registerAction = [&](const char* name, WorkspaceAction action, const char* help) {
        registry.RegisterCommand({
            .Name = name,
            .Owner = "editor",
            .Usage = std::string(name) + " <kind>",
            .Help = help,
            .Callback = [this, action](ConsoleExecutionContext&, std::span<const std::string> args) {
                ConsoleResult result;
                if (args.size() != 1 || !Workspaces->IsOffered(args[0]))
                {
                    result.Status = ConsoleStatus::InvalidArguments;
                    std::string kinds;
                    for (const WorkspaceKind& kind : Workspaces->Kinds())
                        if (Workspaces->IsOffered(kind.Id))
                            kinds += (kinds.empty() ? "" : ", ") + kind.Id;
                    result.Error("expected one of: " + kinds);
                    return result;
                }
                if (action != WorkspaceAction::Open && action != WorkspaceAction::Close
                    && Workspaces->Find(args[0]) == nullptr)
                {
                    result.Status = ConsoleStatus::InvalidArguments;
                    result.Error("'" + args[0] + "' is not open; workspace.open opens it");
                    return result;
                }
                // Mid-frame, applied at the next frame boundary: a workspace
                // adds and removes render work as it comes and goes.
                Workspaces->Request({ action, args[0] });
                if (!FramesStarted)
                    Workspaces->ApplyRequests();
                result.Info("done for '" + args[0] + "'");
                return result;
            },
        });
    };
    registry.RegisterCommand({
        .Name = "shell.popover",
        .Owner = "editor",
        .Usage = "shell.popover <hatch|workspaces|document|none>",
        .Help = "Open one of the main window's rail popovers, or close it.",
        .Callback = [this](ConsoleExecutionContext&, std::span<const std::string> args) {
            ConsoleResult result;
            if (MainWindowShell != nullptr)
                MainWindowShell->ShowPopover(args.empty() ? std::string_view{} : std::string_view(args[0]));
            result.Info("done");
            return result;
        },
    });
    registerAction("workspace.open", WorkspaceAction::Open, "Open a workspace, or bring it to the front if open.");
    registerAction("workspace.close", WorkspaceAction::Close, "Close a workspace.");
    registerAction("workspace.activate", WorkspaceAction::Activate, "Bring an open workspace to the front.");
    registerAction("workspace.detach", WorkspaceAction::Detach, "Move an open workspace into a window of its own.");
    registerAction("workspace.attach", WorkspaceAction::Attach, "Move a workspace back into the main window.");

    registry.RegisterCommand({
        .Name = "editor.window.screenshot",
        .Owner = "editor",
        .Usage = "editor.window.screenshot <kind> <path.png> [frame]",
        .Help = "Write the window showing an open workspace to a PNG, once the renderer has drawn [frame] frames.",
        .Callback = [this](ConsoleExecutionContext&, std::span<const std::string> args) {
            ConsoleResult result;
            const PresentationId window = args.size() >= 2 ? Workspaces->WindowOf(args[0]) : PresentationId{};
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
    if (Ui == nullptr || Workspaces == nullptr)
        return;
    // Every window's events arrive here; each goes to the shell of its window
    // and the workspace showing there.
    EditorUiFeature* shell = ShellForWindowId(ctx.WindowId);
    const PresentationId window = shell->ShownWindow();
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
    if (ctx.Event.type == SDL_EVENT_KEY_DOWN && !ctx.Event.key.repeat
        && ctx.Event.key.scancode == SDL_SCANCODE_GRAVE)
    {
        shell->ToggleConsole();
        ctx.Handled = true;
        return;
    }
    IWorkspace* active = Workspaces->ActiveIn(window);
    if (active != nullptr && active->ClaimPlatformEvent(ctx))
    {
        ctx.Handled = true;
        return;
    }
    shell->ProcessSdlEvent(ctx.Event);
    if (active != nullptr)
        active->HandlePlatformEvent(ctx);
    // Keys the active workspace left alone, unless a text field has them, are
    // the window's actions.
    if (ctx.Handled || shell->GetInputCapture().Keyboard)
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
    // frames it submitted may still be executing, and the workspaces free ImGui
    // descriptor sets and render features as they close.
    if (GraphicsServices* graphics = GetEngine().TryGraphics())
        graphics->WaitIdle();

    // Inside the Game shutdown window, before the engine disconnects the asset
    // stack they hold leases into. The workspaces first: their documents hold
    // code the session's module compiled.
    // Detached workspaces come home before anything closes, so each one's
    // window, and the shell drawn in it, go while the workspace still lives.
    if (Workspaces)
    {
        while (!Detached.empty())
            Workspaces->Gather(Detached.back().Presentation);
        MainWindowShell.reset();
        if (MainWindowActions != nullptr)
        {
            MainWindowActions->Workspace.Reset();
            MainWindowActions->View.Reset();
            MainWindowActions->Layered = nullptr;
        }
        Workspaces->CloseAll();
    }
    DetachRenderFeature(GetEngine(), Ui);
    Workspaces.reset();
    // Both read the journal the session is about to take with it.
    CloseAsker = {};
    SaveFollowUp.reset();
    ExitHold.reset();
    Session.reset();
}

std::unique_ptr<ShellWindow> KyusuApp::MakeShell(SdlWindow& window, PresentationId presentation,
                                                 WindowActions& actions, std::function<void()> close)
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
        .Presentation = presentation,
        .CloseWindow = std::move(close),
        .Title = "Kyusu",
    });
}

void KyusuApp::UpdateShells()
{
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    double requestedScale = 0.0;
    if (const CVarMetadata* var = GetEngine().Console().Registry().FindCVar("editor.ui.scale"))
        if (const double* value = std::get_if<double>(&var->CurrentValue))
            requestedScale = *value;
    if (MainWindowShell != nullptr)
    {
        MainWindowShell->Update(now, requestedScale);
        if (Ui != nullptr)
            Ui->SetContentTopInset(MainWindowShell->RailHeight());
    }
    for (DetachedWindow& entry : Detached)
    {
        if (entry.WindowShell == nullptr)
            continue;
        entry.WindowShell->Update(now, requestedScale);
        if (entry.Shell != nullptr)
            entry.Shell->SetContentTopInset(entry.WindowShell->RailHeight());
    }
}
