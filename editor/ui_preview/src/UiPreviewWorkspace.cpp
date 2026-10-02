#include "UiPreviewWorkspace.h"

#include "text/PathLeaf.h"

#include "authoring/PreviewReports.h"

#include "EditorTheme.h"
#include "actions/ActionOffer.h"
#include "notices/NoticeQueue.h"
#include "presenters/ContentPicture.h"
#include "problems/ProblemList.h"
#include "project/ProcessLaunch.h"
#include "project/Project.h"
#include "render/RenderFeatureDetach.h"
#include "workspaces/WorkspaceWindow.h"

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
#include <platform/SdlWindow.h>
#include <ui/UiService.h>
#include <ui/UiSurfacePlacement.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <span>

namespace
{
constexpr std::string_view kTargetFeatureId = "ui_surface_target";
constexpr std::array<std::string_view, 1> kTargetDependsOn{ "editor_ui" };
constexpr std::string_view kCommandOwner = "editor.ui_preview";
constexpr std::string_view kProblemSource = "ui_preview";
constexpr std::string_view kVocabularyProblemSource = "vocabulary";
constexpr std::string_view kDocumentsExplorer = "ui_documents";
constexpr std::string_view kOutlineExplorer = "ui_outline";
constexpr std::string_view kVocabularyExplorer = "vocabulary";

constexpr const char* kLibraryProject = "Project";
constexpr const char* kLibraryEngine = "Engine";
constexpr const char* kLibraryEditor = "Editor";

struct ResolutionRow
{
    std::string_view Id;
    std::string_view Label;
    RenderExtent Size;
};
constexpr std::array<ResolutionRow, 5> kResolutions{ {
    { "ui_preview.resolution.720p", "1280 x 720", { 1280, 720 } },
    { "ui_preview.resolution.1080p", "1920 x 1080", { 1920, 1080 } },
    { "ui_preview.resolution.1440p", "2560 x 1440", { 2560, 1440 } },
    { "ui_preview.resolution.2160p", "3840 x 2160", { 3840, 2160 } },
    { "ui_preview.resolution.handheld", "1280 x 800 handheld", { 1280, 800 } },
} };

struct ScaleRow
{
    std::string_view Id;
    std::string_view Label;
    float Scale;
};
constexpr std::array<ScaleRow, 4> kScales{ {
    { "ui_preview.scale.100", "Scale 1", 1.0f },
    { "ui_preview.scale.125", "Scale 1.25", 1.25f },
    { "ui_preview.scale.150", "Scale 1.5", 1.5f },
    { "ui_preview.scale.200", "Scale 2", 2.0f },
} };

struct ZoomRow
{
    std::string_view Id;
    std::string_view Label;
    PreviewZoom Zoom;
};
constexpr std::array<ZoomRow, 4> kZooms{ {
    { "ui_preview.zoom.fit", "Fit", PreviewZoom::Fit },
    { "ui_preview.zoom.half", "50%", PreviewZoom::Half },
    { "ui_preview.zoom.actual", "100%", PreviewZoom::Actual },
    { "ui_preview.zoom.double", "200%", PreviewZoom::Double },
} };

struct NavigationRow
{
    std::string_view Id;
    std::string_view Label;
    UiNavigation Direction;
};
constexpr std::array<NavigationRow, 6> kNavigation{ {
    { "ui_preview.navigate.up", "Focus Up", UiNavigation::Up },
    { "ui_preview.navigate.down", "Focus Down", UiNavigation::Down },
    { "ui_preview.navigate.left", "Focus Left", UiNavigation::Left },
    { "ui_preview.navigate.right", "Focus Right", UiNavigation::Right },
    { "ui_preview.navigate.accept", "Accept", UiNavigation::Accept },
    { "ui_preview.navigate.back", "Back", UiNavigation::Cancel },
} };

// The ground the document composes over, in linear light because the target
// is cleared before any encoding.
Vec3d PreviewGroundLinear()
{
    return Vec3d{ EditorTheme::DocumentGround[0], EditorTheme::DocumentGround[1], EditorTheme::DocumentGround[2] };
}
} // namespace

UiPreviewWorkspace::UiPreviewWorkspace(Engine& engine, const ProjectDescriptor* project, Game* module,
                                       ActionCatalog& catalog, ProblemList& problems, NoticeQueue& notices)
    : EngineRef(engine)
    , Watch(engine.Content().SourceReload())
    , Problems(problems)
    , Notices(notices)
{
    if (module != nullptr)
    {
        Vocabulary.InstallModuleVocabulary(*module);
        for (const std::string& diagnostic : Vocabulary.Errors())
            std::fprintf(stderr, "[ui preview] vocabulary: %s\n", diagnostic.c_str());
    }
    BuildLibrary(project);
    BuildUi();
    DefineActions(catalog);
    RegisterCommands();
}

UiPreviewWorkspace::~UiPreviewWorkspace()
{
    EngineRef.Console().Registry().UnregisterOwner(kCommandOwner);
    Problems.Clear(kProblemSource);
    Problems.Clear(kVocabularyProblemSource);
    // The session closes its screen before the feature that drew it goes, and
    // the feature goes while the caches it borrows are still alive.
    Surface = WorkspaceView{};
    Picture.reset();
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
    Picture = std::make_unique<ContentPicture>(*ui, "ui_preview");
    ElementCard = std::make_unique<ElementProperties>(*Session, [this] { return Selected; });
    SurfaceCard = std::make_unique<SurfaceProperties>(*Session, [this] { return OpenEntry(); }, Watch);

    Renderer& renderer = EngineRef.Graphics().MainRenderer;
    Target = renderer.StageFeature(
        std::make_unique<UiSurfaceTargetRenderFeature>(*ui, EngineRef.Content().Assets().Textures.get()),
        FeatureRegistration{ .Id = kTargetFeatureId, .DependsOn = kTargetDependsOn });
    std::vector<std::string_view> failed;
    if (!renderer.CommitStagedFeatures(&failed) || !failed.empty())
    {
        std::fprintf(stderr, "[ui preview] the preview target failed to set up; nothing will be drawn\n");
        Target = nullptr;
    }
    if (Target != nullptr)
        Binding = Target->Bind(Session->Surface(), PreviewGroundLinear());

    ModelCard = std::make_unique<PreviewModelProperties>(*Session);
    MissCard = std::make_unique<BindingMissProperties>(*Session);
    Surface.File.Save = [this] { std::string error; (void)SaveModel(&error); };
    ScanVocabulary();
}

void UiPreviewWorkspace::ScanVocabulary()
{
    // The path order is sorted so the explorer reads the same way twice.
    Bindings.clear();
    RuntimeAssets& stack = EngineRef.Content().Assets();
    std::vector<std::string> paths;
    for (const auto& [path, record] : stack.Registry.Records())
        if (record.Type == AssetType::Data)
            paths.push_back(path);
    std::sort(paths.begin(), paths.end());
    for (const std::string& path : paths)
    {
        const AssetLease lease = stack.Assets.LoadLease(path, AssetType::Data);
        if (!lease.IsValid())
            continue;
        const auto* library = stack.DataAssets.TryGet<VerbBindingLibrary>(
            DataAssetHandle::FromToken(lease.OpaqueToken()), kVerbBindingsTypeName);
        if (library != nullptr)
            Bindings.push_back({ path, Vocabulary.Inspect(*library, &stack.Registry, &stack.DataAssets) });
    }
    Problems.Publish(kVocabularyProblemSource, VocabularyProblems(Vocabulary, Bindings));
}

void UiPreviewWorkspace::DefineActions(ActionCatalog& catalog)
{
    const auto open = [this] { return Session && Session->IsOpen(); };
    OfferAction(catalog, ActionHandlers,
                { .Id = "explorer.ui_documents", .Label = "Documents", .Category = "Explore", .Icon = IconId::Folder,
                  .DefaultChords = { { SDLK_D, {} } } },
                {});
    OfferAction(catalog, ActionHandlers,
                { .Id = "explorer.ui_outline", .Label = "Elements", .Category = "Explore",
                  .DefaultChords = { { SDLK_O, {} } } },
                {});
    OfferAction(catalog, ActionHandlers,
                { .Id = "ui_preview.rescan", .Label = "Rescan Documents", .Category = "Explore", .Icon = IconId::Refresh,
                  .DefaultChords = { { SDLK_R, { .Ctrl = true } } } },
                { .Invoke = [this] {
                    RescanLibrary();
                    ScanVocabulary();
                } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "explorer.vocabulary", .Label = "Vocabulary", .Category = "Explore",
                  .DefaultChords = { { SDLK_V, {} } } },
                {});
    OfferAction(catalog, ActionHandlers,
                { .Id = "ui_preview.revert_model", .Label = "Revert Preview Model", .Category = "File" },
                { .Invoke = [this] {
                      if (Session && Session->IsOpen())
                          OpenDocument(Session->PackagePath());
                  },
                  .Enabled = [this] { return ModelCard != nullptr && ModelCard->Changed(); } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "ui_preview.edit_source", .Label = "Edit Document Source", .Category = "File" },
                { .Invoke = [this] {
                      if (const DocumentEntry* entry = OpenEntry())
                          OpenInEditor(*entry);
                  },
                  .Enabled = [this] { return OpenEntry() != nullptr; } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "ui_preview.clear_problems", .Label = "Clear Preview Problems", .Category = "Edit" },
                { .Invoke = [this] {
                    if (Session)
                        Session->ClearDiagnostics();
                } });
    OfferAction(catalog, ActionHandlers, StepBackAction(), { .Invoke = [this] { Selected = {}; } });

    OfferAction(catalog, ActionHandlers,
                { .Id = "ui_preview.interact", .Label = "Interact", .Category = "Mode", .Icon = IconId::Pointer },
                { .Invoke = [this] { SetPointerMode(UiPreviewSession::PointerMode::Interact); },
                  .Checked = [this] { return Session && Session->Mode() == UiPreviewSession::PointerMode::Interact; } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "ui_preview.inspect", .Label = "Inspect", .Category = "Mode", .Icon = IconId::Eye,
                  .DefaultChords = { { SDLK_I, { .Ctrl = true } } } },
                { .Invoke = [this] {
                      const bool inspecting = Session && Session->Mode() == UiPreviewSession::PointerMode::Inspect;
                      SetPointerMode(inspecting ? UiPreviewSession::PointerMode::Interact
                                                : UiPreviewSession::PointerMode::Inspect);
                  },
                  .Checked = [this] { return Session && Session->Mode() == UiPreviewSession::PointerMode::Inspect; } });

    for (const ResolutionRow& row : kResolutions)
        OfferAction(catalog, ActionHandlers,
                    { .Id = std::string(row.Id), .Label = std::string(row.Label), .Category = "View" },
                    { .Invoke = [this, size = row.Size] {
                          if (Session)
                              Session->SetResolution(size);
                      },
                      .Checked = [this, size = row.Size] { return Session && Session->Resolution() == size; } });
    for (const ScaleRow& row : kScales)
        OfferAction(catalog, ActionHandlers,
                    { .Id = std::string(row.Id), .Label = std::string(row.Label), .Category = "View" },
                    { .Invoke = [this, scale = row.Scale] {
                          if (Session)
                              Session->SetDisplayScale(scale);
                      },
                      .Checked = [this, scale = row.Scale] { return Session && Session->DisplayScale() == scale; } });
    for (const ZoomRow& row : kZooms)
        OfferAction(catalog, ActionHandlers,
                    { .Id = std::string(row.Id), .Label = std::string(row.Label), .Category = "View" },
                    { .Invoke = [this, zoom = row.Zoom] { SetZoom(zoom); },
                      .Checked = [this, zoom = row.Zoom] { return Zoom == zoom; } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "ui_preview.safe_area", .Label = "Safe Area", .Category = "View", .Icon = IconId::GridFrame },
                { .Invoke = [this] { SafeArea = !SafeArea; }, .Checked = [this] { return SafeArea; } });
    for (const NavigationRow& row : kNavigation)
        OfferAction(catalog, ActionHandlers,
                    { .Id = std::string(row.Id), .Label = std::string(row.Label), .Category = "Navigate" },
                    { .Invoke = [this, direction = row.Direction] {
                          if (Session && Session->IsOpen())
                              Session->Service().Navigate(Session->Surface(), direction);
                      },
                      .Enabled = open });
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
    Visible = visible;
    Panning = false;
    Hovered = {};
    if (Picture != nullptr)
        Picture->SetShown(visible);
    if (!Session)
        return;
    // A screen in the background still updates and draws, so it closes with
    // the workspace and comes back with the model it had.
    if (!visible && Session->IsOpen())
    {
        Suspended.emplace(Session->PackagePath(), Session->Model());
        Session->Close();
        Session->SetPlacement(std::nullopt);
    }
    else if (visible && Suspended.has_value())
    {
        auto [package, model] = std::move(*Suspended);
        Suspended.reset();
        (void)Session->Open(package, std::move(model));
    }
}

void UiPreviewWorkspace::MoveTo(const WorkspaceWindow& window)
{
    if (Picture != nullptr)
        Picture->MoveTo(window.Content);
    if (Session && window.Window != nullptr)
        Session->Service().SetSurfaceWindow(Session->Surface(), window.Window->GetId());
}

void UiPreviewWorkspace::HandlePlatformEvent(PlatformEventContext& ctx)
{
    if (!Session || ctx.Handled || !Visible)
        return;
    const SDL_Event& event = ctx.Event;
    UiService& ui = Session->Service();
    const bool inspecting = Session->Mode() == UiPreviewSession::PointerMode::Inspect;
    const auto onContent = [&](float x, float y, std::uint32_t window) {
        return !ui.Capture(window).Mouse && ContentRect.Contains(Vec2d(x, y));
    };
    switch (event.type)
    {
    case SDL_EVENT_MOUSE_MOTION:
        if (Panning)
        {
            Pan = Pan + Vec2d(event.motion.xrel, event.motion.yrel);
            ctx.Handled = true;
        }
        else if (inspecting)
            Hovered = onContent(event.motion.x, event.motion.y, event.motion.windowID)
                          ? ElementAtWindowPoint(Vec2d(event.motion.x, event.motion.y))
                          : UiElementRef{};
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (!onContent(event.button.x, event.button.y, event.button.windowID))
            break;
        if (event.button.button == SDL_BUTTON_MIDDLE && Zoom != PreviewZoom::Fit)
        {
            Panning = true;
            ctx.Handled = true;
        }
        else if (event.button.button == SDL_BUTTON_LEFT && inspecting)
        {
            Selected = ElementAtWindowPoint(Vec2d(event.button.x, event.button.y));
            ctx.Handled = true;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button == SDL_BUTTON_MIDDLE && Panning)
        {
            Panning = false;
            ctx.Handled = true;
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        Panning = false;
        break;
    default:
        break;
    }
}

void UiPreviewWorkspace::Tick(FrameUpdateContext&)
{
    if (!Session)
        return;
    UiService& ui = Session->Service();
    // The keyboard follows the pointer: while it is over the preview the
    // document has it, and the moment it leaves the editor's keys are back.
    if (Session->Mode() == UiPreviewSession::PointerMode::Interact)
        Session->SetActivated(Visible && ui.IsPointerOver(Session->Surface()));
    Session->Poll();
    // A rebuild after a save replaces every element: what was picked is gone.
    if (Selected.IsValid() && !ui.DescribeElement(Selected))
        Selected = {};
    if (Hovered.IsValid() && !ui.DescribeElement(Hovered))
        Hovered = {};
    ShowPicture();
    Report();
}

void UiPreviewWorkspace::ShowPicture()
{
    if (Picture == nullptr)
        return;
    if (!Visible || !Session->IsOpen() || ContentRect.Width() < 8.0f || ContentRect.Height() < 8.0f)
    {
        Session->SetPlacement(std::nullopt);
        Picture->Place(ContentRect, Rect2d{});
        Picture->Mark({});
        return;
    }
    const RenderExtent surface = Session->Resolution();
    const Rect2d placed = PlacePreview(ContentRect, surface, Zoom, Pan);
    // The layer maps the pointer into the document through this placement, and
    // inspection maps it the same way, so the two never disagree by a pixel.
    Session->SetPlacement(placed);
    Picture->Place(ContentRect, placed);
    if (Target != nullptr)
        (void)Target->Show(Binding, Picture->Image());

    std::vector<PictureFrame> frames{ { placed, PictureFrameRole::Bounds } };
    const auto frameElement = [&](UiElementRef element, PictureFrameRole role) {
        if (const std::optional<UiElementInfo> info = Session->Service().DescribeElement(element))
        {
            frames.push_back({ BoxOnPicture(info->Boxes.Border, placed, surface), role });
            frames.push_back({ BoxOnPicture(info->Boxes.Content, placed, surface), role, true });
        }
    };
    if (Session->Mode() == UiPreviewSession::PointerMode::Inspect && Hovered.IsValid() && Hovered != Selected)
        frameElement(Hovered, PictureFrameRole::Hover);
    if (Selected.IsValid())
        frameElement(Selected, PictureFrameRole::Selected);
    if (SafeArea)
    {
        const std::array<Rect2d, 2> safe = SafeAreas(placed);
        frames.push_back({ safe[0], PictureFrameRole::Caution, true });
        frames.push_back({ safe[1], PictureFrameRole::Caution });
    }
    Picture->Mark(frames);
}

void UiPreviewWorkspace::Report()
{
    if (Session->DiagnosticsVersion() != ReportedDiagnostics)
    {
        ReportedDiagnostics = Session->DiagnosticsVersion();
        Reported = PreviewProblems(Session->Diagnostics(), Session->Surface());
        Problems.Publish(kProblemSource, Reported);
    }
    const std::uint64_t raised = Session->RaisedCount();
    const std::deque<UiAction>& actions = Session->Actions();
    const std::size_t fresh = static_cast<std::size_t>(
        std::min<std::uint64_t>({ raised - ReportedRaised, actions.size(), NoticeQueue::kVisible }));
    ReportedRaised = raised;
    const double now = NoticeClockNow();
    for (std::size_t i = actions.size() - fresh; i < actions.size(); ++i)
        Notices.Post("Raised " + DescribeRaisedAction(actions[i], Session->Model()), NoticeTone::Info, now);
}

UiElementRef UiPreviewWorkspace::ElementAtWindowPoint(Vec2d point) const
{
    const std::optional<Vec2d> onSurface =
        MapWindowPointToSurface(point, Picture != nullptr ? Picture->Placed() : Rect2d{}, Session->Resolution());
    return onSurface ? Session->Service().ElementAt(Session->Surface(), *onSurface) : UiElementRef{};
}

void UiPreviewWorkspace::SetPointerMode(UiPreviewSession::PointerMode mode)
{
    if (!Session)
        return;
    Session->SetPointerMode(mode);
    Hovered = {};
}

void UiPreviewWorkspace::SetZoom(PreviewZoom zoom)
{
    Zoom = zoom;
    Pan = Vec2d(0.0f, 0.0f);
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
    Hovered = {};
    Selected = {};
    Session->ClearDiagnostics();
    Session->ClearActions();
    if (!Session->Open(packagePath, std::move(model)))
        std::fprintf(stderr, "[ui preview] '%s' did not open; see the problems\n", packagePath.c_str());
    if (ModelCard != nullptr)
        ModelCard->MarkSaved();
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
    {
        Library.Rescan();
        ModelCard->MarkSaved();
        Notices.Post("Saved the preview model", NoticeTone::Done, NoticeClockNow());
    }
    else
        Notices.Post("The preview model did not save: " + (error != nullptr ? *error : std::string{}),
                     NoticeTone::Failed, NoticeClockNow());
    return saved;
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

const DocumentEntry* UiPreviewWorkspace::OpenEntry() const
{
    return Session && Session->IsOpen() ? Library.Find(Session->PackagePath()) : nullptr;
}

WorkspaceDocumentFacet UiPreviewWorkspace::Document() const
{
    return { .Label = Session ? Session->PackagePath() : std::string{},
             .Changed = ModelCard != nullptr && ModelCard->Changed() };
}

WorkspaceOperationFacet UiPreviewWorkspace::Operation()
{
    if (!Session)
        return {};
    const bool inspecting = Session->Mode() == UiPreviewSession::PointerMode::Inspect;
    return {
        .Name = inspecting ? "Inspect" : "Interact",
        .Icon = inspecting ? IconId::Eye : IconId::Pointer,
        .Hint = !Session->IsOpen() ? "Open a document from the library (D)"
                : inspecting       ? "Click an element to look at it"
                                   : "The document has the pointer, and the keys while it is over it",
        .Choices = { { .Label = "Interact", .Icon = IconId::Pointer, .Action = "ui_preview.interact",
                       .Current = !inspecting },
                     { .Label = "Inspect", .Icon = IconId::Eye, .Action = "ui_preview.inspect",
                       .Current = inspecting } },
    };
}

WorkspaceViewFacet UiPreviewWorkspace::Viewing() const
{
    if (!Session)
        return {};
    const RenderExtent size = Session->Resolution();
    const auto zoom = std::ranges::find(kZooms, Zoom, &ZoomRow::Zoom);
    return { .Projection = std::format("{} x {} @{:g} · {}", size.Width, size.Height, Session->DisplayScale(),
                                       zoom->Label) };
}

WorkspaceSelectionFacet UiPreviewWorkspace::Selection()
{
    if (!Session || !Session->IsOpen())
        return {};
    if (const std::optional<UiElementInfo> element = Session->Service().DescribeElement(Selected))
        return { .Title = ElementLabel(*element),
                 .Actions = { "ui_preview.edit_source" },
                 .Sections = { { .Heading = {}, .Source = ElementCard.get() } } };
    const std::string& package = Session->PackagePath();
    WorkspaceSelectionFacet surface{ .Title = std::string(PathLeaf(package)),
                                     .Actions = { "ui_preview.edit_source", "ui_preview.revert_model" },
                                     .Sections = { { .Heading = "Surface", .Source = SurfaceCard.get() } },
                                     .Details = { { .Heading = "Preview model", .Source = ModelCard.get() } } };
    if (!MissCard->Nodes().empty())
        surface.Sections.push_back({ .Heading = "Missing from the model", .Source = MissCard.get() });
    return surface;
}

bool UiPreviewWorkspace::RevealProblem(const Problem& problem)
{
    const bool ours = std::ranges::any_of(Reported, [&problem](const Problem& reported) {
        return reported.Message == problem.Message && reported.Location == problem.Location;
    });
    if (!ours || problem.Location.empty())
        return false;
    // What the model misses is listed on the surface's card.
    if (problem.Location == kPreviewModelLocation)
    {
        Selected = {};
        return true;
    }
    const std::string path = problem.Location.substr(0, problem.Location.find(':'));
    if (const DocumentEntry* entry = Library.Find("asset://" + path))
    {
        OpenInEditor(*entry);
        return true;
    }
    return false;
}

std::vector<WorkspaceExplorerInfo> UiPreviewWorkspace::Explorers() const
{
    return { { .Id = std::string(kDocumentsExplorer), .Title = "Documents", .Shape = ExplorerShape::Outline,
               .Action = "explorer.ui_documents" },
             { .Id = std::string(kOutlineExplorer), .Title = "Elements", .Shape = ExplorerShape::Outline,
               .Action = "explorer.ui_outline" },
             { .Id = std::string(kVocabularyExplorer), .Title = "Vocabulary", .Shape = ExplorerShape::Outline,
               .Action = "explorer.vocabulary" } };
}

ExplorerPage UiPreviewWorkspace::Explore(const ExplorerQuery& query)
{
    if (query.Explorer == kDocumentsExplorer)
        return Libraries.Explore(Library, Session ? std::string_view(Session->PackagePath()) : std::string_view{},
                                 query);
    if (query.Explorer == kVocabularyExplorer)
        return VocabularyExplorer.Explore(Vocabulary, Bindings, query);
    if (query.Explorer != kOutlineExplorer)
        return {};
    if (!Session || !Session->IsOpen())
    {
        ExplorerPage page;
        page.Empty = "No document is open.";
        return page;
    }
    const std::vector<UiElementInfo> tree = Session->Service().ElementTree(Session->OpenScreen());
    return Outline.Explore(tree, Selected, query);
}

void UiPreviewWorkspace::ChooseInExplorer(std::string_view explorer, std::string_view key, ExplorerChoice choice,
                                          std::size_t)
{
    if (explorer == kVocabularyExplorer)
    {
        (void)VocabularyExplorer.Flip(key);
        return;
    }
    if (explorer == kDocumentsExplorer)
    {
        if (Libraries.Flip(key) || choice == ExplorerChoice::Open)
            return;
        OpenDocument(std::string(key));
        return;
    }
    if (explorer != kOutlineExplorer || !Session || !Session->IsOpen())
        return;
    const std::vector<UiElementInfo> tree = Session->Service().ElementTree(Session->OpenScreen());
    const std::optional<UiElementRef> element = ElementOutline::Resolve(tree, key);
    if (!element)
        return;
    if (choice == ExplorerChoice::Open)
        Outline.Flip(*element);
    else
        Selected = *element;
}
