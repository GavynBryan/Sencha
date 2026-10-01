#include "AnimationWorkspace.h"

#include "authoring/AnimationBoneProperties.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationStageStrokes.h"
#include "render/AnimationPreviewRenderFeature.h"
#include "ui/AnimationPreviewPanels.h"

#include "actions/ActionOffer.h"
#include "data/DataDocumentStore.h"
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
} // namespace

AnimationWorkspace::AnimationWorkspace(Engine& engine, const ProjectDescriptor& project, Game* module,
                                       DocumentSourceSet& sources, DataDocumentStore& store, PromptQueue& prompts,
                                       ActionCatalog& catalog)
    : EngineRef(engine)
    , Store(store)
{
    std::function<void(World&)> vocabulary;
    if (module != nullptr)
        vocabulary = [module](World& world) { module->OnRegisterVocabulary(world); };
    const std::filesystem::path authoringRoot =
        project.ContentRoots.empty() ? std::filesystem::path{} : std::filesystem::path(project.ContentRoots.front());
    Workspace = std::make_unique<AnimationPreviewWorkspace>(engine.Content().Assets(), sources, store,
                                                            std::move(vocabulary), authoringRoot);
    Bone = std::make_unique<AnimationBoneProperties>(*Workspace);

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
    AddAnimationPreviewPanels(Surface, *Workspace, prompts);
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
                    Workspace->Rig.Navigation.Joint = -1;
                } });
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
    const DataDocument* active = Workspace->Documents.Active();
    if (active == nullptr)
        return {};
    return { .Label = active->VirtualPath(), .Changed = active->IsDirty() };
}

WorkspaceViewFacet AnimationWorkspace::Viewing() const
{
    return { .Projection = Workspace->Viewport.Source == AnimationViewportSource::Simulation ? "Simulation"
                                                                                            : "Audition" };
}

WorkspaceSelectionFacet AnimationWorkspace::Selection()
{
    const SkeletonData& skeleton = Workspace->Audition.Session.Skeleton();
    const int joint = Workspace->Rig.Navigation.Joint;
    if (joint < 0 || static_cast<std::size_t>(joint) >= skeleton.Joints.size())
        return {};
    const std::string& name = skeleton.Joints[static_cast<std::size_t>(joint)].Name;
    return { .Title = name.empty() ? std::string("Unnamed joint") : name,
             .Sections = { { .Heading = {}, .Source = Bone.get() } } };
}
