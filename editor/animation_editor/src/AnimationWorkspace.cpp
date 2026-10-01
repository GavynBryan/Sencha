#include "AnimationWorkspace.h"

#include "authoring/AnimationBoneProperties.h"
#include "authoring/AnimationExplorers.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRigCreation.h"
#include "authoring/AnimationStageProperties.h"
#include "authoring/AnimationStageStrokes.h"
#include "render/AnimationPreviewRenderFeature.h"
#include "ui/AnimationPreviewPanels.h"

#include "actions/ActionOffer.h"
#include "data/DataCreation.h"
#include "data/DataDocumentStore.h"
#include "data/DataProperties.h"
#include "prompts/DocumentPrompts.h"
#include "documents/DocumentSourceSet.h"
#include "presenters/ContentPicture.h"
#include "project/Project.h"
#include "render/RenderFeatureDetach.h"
#include "workspaces/WorkspaceWindow.h"

#include <anim/Skeleton.h>
#include <app/Engine.h>
#include <app/Game.h>
#include <app/GameContexts.h>
#include <app/RuntimeContent.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/console/ConsoleTypes.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/Renderer.h>
#include <ui/UiService.h>

#include <SDL3/SDL.h>

#include <array>
#include <optional>
#include <cstdio>
#include <functional>
#include <span>
#include <vector>

namespace
{
constexpr std::array<std::string_view, 1> kPreviewDependsOn{ "editor_ui" };
constexpr std::string_view kCommandOwner = "editor.animation";
// How far a press may move and still pick rather than orbit, in pixels.
constexpr float kPickSlop = 3.0f;
constexpr float kPickRadius = 8.0f;
constexpr float kJointRadius = 3.0f;
constexpr float kPickedJointRadius = 5.0f;
constexpr float kOrbitPerPixel = 0.01f;

constexpr std::string_view kContentExplorer = "anim_content";
constexpr std::string_view kRigExplorer = "anim_rigs";
constexpr std::string_view kSkeletonExplorer = "anim_skeleton";

std::string_view LeafOf(std::string_view path)
{
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}
} // namespace

AnimationWorkspace::AnimationWorkspace(Engine& engine, const ProjectDescriptor& project, Game* module,
                                       DocumentSourceSet& sources, DataDocumentStore& store, PromptQueue& prompts,
                                       ActionCatalog& catalog)
    : EngineRef(engine)
    , Store(store)
    , Prompts(prompts.MakeAsker())
{
    std::function<void(World&)> vocabulary;
    if (module != nullptr)
        vocabulary = [module](World& world) { module->OnRegisterVocabulary(world); };
    const std::filesystem::path authoringRoot =
        project.ContentRoots.empty() ? std::filesystem::path{} : std::filesystem::path(project.ContentRoots.front());
    Workspace = std::make_unique<AnimationPreviewWorkspace>(engine.Content().Assets(), sources, store,
                                                            std::move(vocabulary), authoringRoot);
    Bone = std::make_unique<AnimationBoneProperties>(*Workspace);
    Audition = std::make_unique<AnimationAuditionProperties>(*Workspace);
    Scenario = std::make_unique<AnimationScenarioProperties>(*Workspace);
    Creation = std::make_unique<AnimationRigCreation>(*Workspace, [this] { Subject = CardSubject::Stage; });
    DocumentSheet = std::make_unique<DataProperties>(Workspace->Documents, [this] { return Workspace->Documents.Active(); });
    NewDocument = std::make_unique<DataCreation>(
        [this] { return Workspace->Documents.CreatableSubtypes(); },
        [this](const std::string& subtype, const std::string& path, std::string& error) {
            if (Workspace->Documents.Create(subtype, path, error) == nullptr)
                return false;
            Workspace->RefreshBrowser();
            Subject = CardSubject::Document;
            return true;
        });

    Renderer& renderer = engine.Graphics().MainRenderer;
    Viewport = renderer.StageFeature(
        std::make_unique<AnimationPreviewRenderFeature>(engine.Content().Assets(), Workspace->Viewport.Scene),
        FeatureRegistration{ .Id = "animation_preview", .DependsOn = kPreviewDependsOn });
    std::vector<std::string_view> failed;
    if (!renderer.CommitStagedFeatures(&failed) || !failed.empty())
    {
        std::fprintf(stderr, "[animation] the preview failed to set up; it will not draw\n");
        Viewport = nullptr;
    }
    if (UiService* ui = engine.TryUi(); ui != nullptr && ui->IsReady())
        Picture = std::make_unique<ContentPicture>(*ui, "animation_stage");

    Surface.File.Save = [this, &sources] {
        if (const DataDocument* active = Workspace->Documents.Active())
            (void)sources.Save(Workspace->Documents.RefOf(*active));
    };
    AddAnimationPreviewPanels(Surface, *Workspace);
    DefineActions(catalog);
    RegisterCommands();
}

AnimationWorkspace::~AnimationWorkspace()
{
    EngineRef.Console().Registry().UnregisterOwner(kCommandOwner);
    Workspace->Sources.CancelEdits();
    // The preview borrows the scene the workspace extracts into.
    DetachRenderFeature(EngineRef, Viewport);
    Surface = WorkspaceView{};
    Picture.reset();
    NewDocument.reset();
    DocumentSheet.reset();
    Creation.reset();
    Scenario.reset();
    Audition.reset();
    Bone.reset();
    Workspace.reset();
}

void AnimationWorkspace::RegisterCommands()
{
    EngineRef.Console().Registry().RegisterCommand({
        .Name = "animation.audition",
        .Owner = std::string(kCommandOwner),
        .Usage = "animation.audition <mesh> [clip]",
        .Help = "Audition a skinned mesh, and optionally one of its clips, in the animation workspace.",
        .Callback = [this](ConsoleExecutionContext&, std::span<const std::string> args) {
            ConsoleResult result;
            if (args.empty() || args.size() > 2)
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("expected <mesh> [clip]");
                return result;
            }
            if (!Workspace->Audition.SelectMesh(args[0]) || (args.size() == 2 && !Workspace->AuditionClip(args[1])))
            {
                result.Status = ConsoleStatus::ExecutionFailed;
                result.Error(Workspace->Audition.Error);
                return result;
            }
            result.Info("auditioning '" + args[0] + "'");
            return result;
        },
    });
}

void AnimationWorkspace::DefineActions(ActionCatalog& catalog)
{
    OfferAction(catalog, ActionHandlers, FrameAction(), { .Invoke = [this] {
                    if (Viewport != nullptr)
                        Viewport->FrameSubject();
                } });
    OfferAction(catalog, ActionHandlers, StepBackAction(), { .Invoke = [this] {
                    Workspace->Sources.CancelEdits();
                    if (Workspace->Rig.Navigation.Joint >= 0)
                        Workspace->Rig.Navigation.Joint = -1;
                    else
                        Subject = CardSubject::Stage;
                } });

    OfferAction(catalog, ActionHandlers,
                { .Id = "explorer.anim_content", .Label = "Content", .Category = "Explore", .Icon = IconId::Folder,
                  .DefaultChords = { { SDLK_C, {} } } },
                {});
    OfferAction(catalog, ActionHandlers,
                { .Id = "explorer.anim_rigs", .Label = "Rigs", .Category = "Explore", .Icon = IconId::Folder,
                  .DefaultChords = { { SDLK_R, {} } } },
                {});
    OfferAction(catalog, ActionHandlers,
                { .Id = "explorer.anim_skeleton", .Label = "Skeleton", .Category = "Explore",
                  .DefaultChords = { { SDLK_J, {} } } },
                {});
    OfferAction(catalog, ActionHandlers,
                { .Id = "animation.bind_pose", .Label = "Bind Pose", .Category = "Explore" },
                { .Invoke = [this] { (void)Workspace->AuditionClip({}); } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "animation.neutral_material", .Label = "Neutral Material", .Category = "Explore" },
                { .Invoke = [this] { (void)Workspace->Audition.SelectMaterial({}); } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "animation.refresh", .Label = "Refresh Content", .Category = "Explore", .Icon = IconId::Refresh },
                { .Invoke = [this] { Workspace->RefreshBrowser(); } });

    const auto documentOpen = [this] { return Workspace->Documents.Active() != nullptr; };
    OfferAction(catalog, ActionHandlers,
                { .Id = "animation.document.new", .Label = "New Animation Document", .Category = "File",
                  .DefaultChords = { { SDLK_N, { .Ctrl = true } } } },
                { .Invoke = [this] {
                    NewDocument->Clear();
                    Workspace->Rig.Navigation.Joint = -1;
                    Subject = CardSubject::NewDocument;
                } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "animation.document.close", .Label = "Close Document", .Category = "File",
                  .DefaultChords = { { SDLK_W, { .Ctrl = true } } } },
                { .Invoke = [this] { CloseActiveDocument(); }, .Enabled = documentOpen });

    const auto rigOpen = [this] { return Workspace->Rig.Simulation.IsOpen() && !Workspace->Rig.Path.empty(); };
    OfferAction(catalog, ActionHandlers, { .Id = "animation.rig.new", .Label = "New Rig", .Category = "Resource" },
                { .Invoke = [this] {
                    Workspace->Rig.Navigation.Joint = -1;
                    Subject = CardSubject::NewRig;
                } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "animation.scenario.save", .Label = "Save Scenario", .Category = "Resource" },
                { .Invoke = [this] { (void)Workspace->Rig.Save(); }, .Enabled = rigOpen });
    OfferAction(catalog, ActionHandlers,
                { .Id = "animation.scenario.revert", .Label = "Revert Scenario", .Category = "Resource" },
                { .Invoke = [this] { (void)Workspace->ReloadScenario(); },
                  .Enabled = [this, rigOpen] { return rigOpen() && Workspace->Rig.Simulation.ScenarioModified(); } });
    OfferAction(catalog, ActionHandlers, { .Id = "animation.view.joints", .Label = "Joints", .Category = "View" },
                { .Invoke = [this] { ShowJoints = !ShowJoints; }, .Checked = [this] { return ShowJoints; } });
    OfferAction(catalog, ActionHandlers, { .Id = "animation.view.path", .Label = "Path", .Category = "View" },
                { .Invoke = [this] { ShowPath = !ShowPath; },
                  .Enabled = [this] { return Workspace->Rig.Simulation.SubjectTransform() != nullptr; },
                  .Checked = [this] { return ShowPath; } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "timeline.source.audition", .Label = "Audition Clock", .Category = "View" },
                { .Invoke = [this] { Workspace->Viewport.Source = AnimationViewportSource::Audition; },
                  .Checked = [this] { return Workspace->Viewport.Source == AnimationViewportSource::Audition; } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "timeline.source.simulation", .Label = "Simulation Clock", .Category = "View" },
                { .Invoke = [this] { Workspace->Viewport.Source = AnimationViewportSource::Simulation; },
                  .Enabled = [this] { return Workspace->Rig.Simulation.IsOpen(); },
                  .Checked = [this] { return Workspace->Viewport.Source == AnimationViewportSource::Simulation; } });
}

void AnimationWorkspace::Tick(FrameUpdateContext& ctx)
{
    // The preview runs only while it can be seen; a background tab keeps its
    // place and resumes where it stopped.
    if (!Visible)
        return;
    Workspace->Advance(ctx.WallDeltaSeconds);
    Workspace->ExtractViewport();
    if (Viewport != nullptr && FramedMesh != Workspace->Audition.MeshPath)
    {
        Viewport->FrameSubject();
        FramedMesh = Workspace->Audition.MeshPath;
    }
    ShowStage();
}

void AnimationWorkspace::ShowStage()
{
    if (Picture == nullptr || Viewport == nullptr)
        return;
    Picture->Place(ContentRect);
    const float width = ContentRect.Width();
    const float height = ContentRect.Height();
    if (width < 8.0f || height < 8.0f)
        return;
    Viewport->Show(VkExtent2D{ static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height) },
                   Picture->Image());

    const std::vector<Mat4>& model = Workspace->Viewport.Model();
    std::vector<EditorLineSegment> strokes;
    std::vector<PictureDot> dots;
    Joints.clear();
    if (ShowJoints)
    {
        AppendSkeletonStrokes(Workspace->Audition.Session.Skeleton(), model, strokes);
        Joints = ProjectAnimationJoints(model, Viewport->ViewCamera(width / height).ViewProjection, width, height);
        for (const AnimationJointMarker& joint : Joints)
        {
            const bool picked = Workspace->Rig.Navigation.Joint == static_cast<int>(joint.Joint);
            dots.push_back({ .Centre = ContentRect.Position + Vec2d(joint.X, joint.Y),
                             .Radius = picked ? kPickedJointRadius : kJointRadius,
                             .Selected = picked });
        }
    }
    if (ShowPath && Workspace->Viewport.Source == AnimationViewportSource::Simulation)
        AppendPathStrokes(Workspace->Rig.Simulation,
                          ShownAnimationTick(Workspace->Rig.Simulation, Workspace->Rig.Navigation), strokes);
    Viewport->SetStrokes(std::move(strokes));
    Picture->MarkDots(dots);
}

void AnimationWorkspace::PickJointAt(Vec2d windowPoint)
{
    const Vec2d at = windowPoint - ContentRect.Position;
    const std::optional<std::uint32_t> hit = PickAnimationJoint(Joints, at.X, at.Y, kPickRadius);
    Workspace->Rig.Navigation.Joint = hit ? static_cast<int>(*hit) : -1;
}

void AnimationWorkspace::Interrupt()
{
    Workspace->Audition.Session.Pause();
    Workspace->Sources.CancelEdits();
    Drag = StageDrag::None;
}

void AnimationWorkspace::SetVisible(bool visible)
{
    Visible = visible;
    if (Picture != nullptr)
        Picture->SetShown(visible);
    if (!visible)
        Interrupt();
}

void AnimationWorkspace::MoveTo(const WorkspaceWindow& window)
{
    if (Picture != nullptr)
        Picture->MoveTo(window.Content);
}

void AnimationWorkspace::HandlePlatformEvent(PlatformEventContext& ctx)
{
    const SDL_Event& event = ctx.Event;
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
    {
        Interrupt();
        return;
    }
    if (ctx.Handled || Viewport == nullptr)
        return;
    UiService* ui = EngineRef.TryUi();
    const auto onStage = [&](float x, float y, std::uint32_t window) {
        return !(ui != nullptr && ui->Capture(window).Mouse) && ContentRect.Contains(Vec2d(x, y));
    };
    switch (event.type)
    {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button == SDL_BUTTON_LEFT && onStage(event.button.x, event.button.y, event.button.windowID))
        {
            Drag = StageDrag::Pressed;
            PressedAt = Vec2d(event.button.x, event.button.y);
            ctx.Handled = true;
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (Drag == StageDrag::Pressed)
        {
            const Vec2d moved = Vec2d(event.motion.x, event.motion.y) - PressedAt;
            if (moved.X * moved.X + moved.Y * moved.Y > kPickSlop * kPickSlop)
                Drag = StageDrag::Orbiting;
        }
        if (Drag == StageDrag::Orbiting)
        {
            // Turntable feel: drag right orbits right, drag up orbits above.
            Viewport->Orbit(event.motion.xrel * kOrbitPerPixel, -event.motion.yrel * kOrbitPerPixel);
            ctx.Handled = true;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button == SDL_BUTTON_LEFT && Drag != StageDrag::None)
        {
            if (Drag == StageDrag::Pressed && ShowJoints)
                PickJointAt(Vec2d(event.button.x, event.button.y));
            Drag = StageDrag::None;
            ctx.Handled = true;
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (onStage(event.wheel.mouse_x, event.wheel.mouse_y, event.wheel.windowID))
        {
            Viewport->Zoom(event.wheel.y);
            ctx.Handled = true;
        }
        break;
    default:
        break;
    }
}

bool AnimationWorkspace::OwnsDocument(const DocumentRef& document) const
{
    if (document.Source == &Workspace->ClipEvents)
        return true;
    return document.Source == &Store && Workspace->Documents.IndexOf(document.Key).has_value();
}

void AnimationWorkspace::RevealDocument(const DocumentRef& document)
{
    if (document.Source == &Store)
        Workspace->Documents.Reveal(document.Key);
}

WorkspaceDocumentFacet AnimationWorkspace::Document() const
{
    WorkspaceDocumentFacet facet;
    for (const DataDocument* document : Workspace->Documents.Documents())
        facet.Open.push_back({ .Key = document->VirtualPath(),
                               .Label = std::string(LeafOf(document->VirtualPath())),
                               .Changed = document->IsDirty() });
    if (const DataDocument* active = Workspace->Documents.Active())
    {
        facet.Label = std::string(LeafOf(active->VirtualPath()));
        facet.Changed = active->IsDirty();
        facet.Active = active->VirtualPath();
    }
    return facet;
}

void AnimationWorkspace::ShowDocument(std::string_view key)
{
    if (const std::optional<std::size_t> index = Workspace->Documents.IndexOf(key))
        Workspace->Documents.SetActive(*index);
    Workspace->Rig.Navigation.Joint = -1;
    Subject = CardSubject::Document;
}

void AnimationWorkspace::OpenDocument(const std::string& path)
{
    if (Workspace->Documents.OpenOrFocus(path, Workspace->DocumentError) == nullptr)
        return;
    Workspace->Rig.Navigation.Joint = -1;
    Subject = CardSubject::Document;
}

void AnimationWorkspace::CloseActiveDocument()
{
    const DataDocument* document = Workspace->Documents.Active();
    if (document == nullptr)
        return;
    // By path: the documents may move while the question is open.
    const std::string path = document->VirtualPath();
    AskAboutChanges(Prompts, document->IsDirty() || document->IsEditing(), path,
                    [this, path](DirtyDisposition disposition) {
                        if (const std::optional<std::size_t> index = Workspace->Documents.IndexOf(path))
                            (void)Workspace->Documents.Close(*index, disposition, Workspace->DocumentError);
                        if (Workspace->Documents.Active() == nullptr && Subject == CardSubject::Document)
                            Subject = CardSubject::Stage;
                    });
}

WorkspaceViewFacet AnimationWorkspace::Viewing() const
{
    return { .Projection = Workspace->Viewport.Source == AnimationViewportSource::Simulation ? "Simulation"
                                                                                            : "Audition" };
}

std::vector<WorkspaceResourceFacet> AnimationWorkspace::Resources()
{
    std::string label;
    if (!Workspace->Rig.Path.empty())
    {
        label = std::string(LeafOf(Workspace->Rig.Path));
        if (Workspace->Rig.Simulation.IsOpen())
            label += " \xC2\xB7 " + Workspace->Rig.Simulation.Scenario().Name;
    }
    return { { .Role = "Rig", .Primary = true, .Explorer = std::string(kRigExplorer), .Active = { .Label = label } } };
}

WorkspaceSelectionFacet AnimationWorkspace::Selection()
{
    const SkeletonData& skeleton = Workspace->Audition.Session.Skeleton();
    const int joint = Workspace->Rig.Navigation.Joint;
    if (joint >= 0 && static_cast<std::size_t>(joint) < skeleton.Joints.size())
    {
        const std::string& name = skeleton.Joints[static_cast<std::size_t>(joint)].Name;
        return { .Title = name.empty() ? std::string("Unnamed joint") : name,
                 .Sections = { { .Heading = {}, .Source = Bone.get() } } };
    }
    if (Subject == CardSubject::NewRig)
        return { .Title = "New rig", .Sections = { { .Heading = {}, .Source = Creation.get() } } };
    if (Subject == CardSubject::NewDocument)
        return { .Title = "New animation document", .Sections = { { .Heading = {}, .Source = NewDocument.get() } } };
    if (const DataDocument* document = Workspace->Documents.Active(); Subject == CardSubject::Document && document)
        return { .Title = std::string(LeafOf(document->VirtualPath())),
                 .Actions = { "file.save", "animation.document.close" },
                 .Sections = { { .Heading = {}, .Source = DocumentSheet.get() } } };

    const bool rigOpen = Workspace->Rig.Simulation.IsOpen() && !Workspace->Rig.Path.empty();
    WorkspaceSelectionFacet stage;
    stage.Title = rigOpen                              ? std::string(LeafOf(Workspace->Rig.Path))
                  : !Workspace->Audition.MeshPath.empty() ? std::string(LeafOf(Workspace->Audition.MeshPath))
                                                      : std::string("Stage");
    stage.Sections.push_back({ .Heading = "Audition", .Source = Audition.get() });
    if (rigOpen)
    {
        stage.Sections.push_back({ .Heading = "Scenario", .Source = Scenario.get() });
        stage.Actions = { "animation.scenario.save", "animation.scenario.revert" };
    }
    return stage;
}

std::vector<WorkspaceExplorerInfo> AnimationWorkspace::Explorers() const
{
    return { { .Id = std::string(kContentExplorer), .Title = "Content", .Shape = ExplorerShape::Outline,
               .Action = "explorer.anim_content" },
             { .Id = std::string(kRigExplorer), .Title = "Rigs", .Shape = ExplorerShape::Outline,
               .Action = "explorer.anim_rigs" },
             { .Id = std::string(kSkeletonExplorer), .Title = "Skeleton", .Shape = ExplorerShape::Outline,
               .Action = "explorer.anim_skeleton" } };
}

ExplorerPage AnimationWorkspace::Explore(const ExplorerQuery& query)
{
    ExplorerPage page;
    if (query.Explorer == kContentExplorer)
    {
        page = ContentOutline.Explore(AnimationContentNodes(*Workspace), query);
        page.Actions = { "animation.bind_pose", "animation.neutral_material", "animation.refresh" };
        page.Empty = "No meshes, skeletons, clips or materials hold that.";
    }
    else if (query.Explorer == kRigExplorer)
    {
        page = RigOutline.Explore(AnimationRigNodes(*Workspace), query);
        page.Actions = { "animation.rig.new" };
        page.Empty = "No rig or animation document holds that.";
    }
    else if (query.Explorer == kSkeletonExplorer)
    {
        page = SkeletonOutline.Explore(
            AnimationSkeletonNodes(Workspace->Audition.Session.Skeleton(), Workspace->Rig.Navigation.Joint), query);
        page.Empty = Workspace->Audition.Session.Skeleton().Joints.empty() ? "Nothing with a skeleton is auditioned."
                                                                           : "No joint's name holds that.";
    }
    return page;
}

void AnimationWorkspace::ChooseInExplorer(std::string_view explorer, std::string_view key, ExplorerChoice choice,
                                          std::size_t)
{
    const std::optional<AnimationExplorerItem> item = ParseAnimationExplorerKey(key);
    if (!item)
        return;
    using Kind = AnimationExplorerItem::Kind;
    if (item->What == Kind::Group || (item->What == Kind::Joint && choice == ExplorerChoice::Open))
    {
        TreeOutline& outline = explorer == kContentExplorer ? ContentOutline
                               : explorer == kRigExplorer   ? RigOutline
                                                            : SkeletonOutline;
        outline.Flip(key);
        return;
    }
    AnimationAuditionSelection& audition = Workspace->Audition;
    switch (item->What)
    {
    case Kind::Mesh:     (void)audition.SelectMesh(item->Path); break;
    case Kind::Skeleton: (void)audition.SelectSkeleton(item->Path); break;
    case Kind::Clip:     (void)Workspace->AuditionClip(item->Path); break;
    case Kind::Material: (void)audition.SelectMaterial(item->Path); break;
    case Kind::Rig:
        if (Workspace->OpenRig(item->Path))
            Subject = CardSubject::Stage;
        break;
    case Kind::Document: OpenDocument(item->Path); break;
    case Kind::Joint:    Workspace->Rig.Navigation.Joint = item->Joint; break;
    case Kind::Group:    break;
    }
}
