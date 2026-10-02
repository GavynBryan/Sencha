#include "AnimationWorkspace.h"

#include "authoring/AnimationBlendProperties.h"
#include "authoring/AnimationBlendspacePad.h"
#include "authoring/AnimationBoneProperties.h"
#include "authoring/AnimationTimelineLanes.h"
#include "authoring/AnimationEventProperties.h"
#include "authoring/AnimationExplorers.h"
#include "authoring/AnimationFactProperties.h"
#include "authoring/AnimationFlowProperties.h"
#include "authoring/AnimationLayerProperties.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRigCreation.h"
#include "authoring/AnimationRootMotionProperties.h"
#include "authoring/AnimationRuleProperties.h"
#include "authoring/AnimationStageProperties.h"
#include "authoring/AnimationStageStrokes.h"
#include "authoring/AnimationTimeline.h"
#include "render/AnimationPreviewRenderFeature.h"
#include "ui/AnimationPreviewPanels.h"

#include "actions/ActionOffer.h"
#include "data/DataCreation.h"
#include "data/DataDocumentStore.h"
#include "data/DataProperties.h"
#include "prompts/DocumentPrompts.h"
#include "documents/DocumentSourceSet.h"
#include "presenters/ContentPicture.h"
#include "presenters/PlanePad.h"
#include "project/Project.h"
#include "render/RenderFeatureDetach.h"
#include "workspaces/WorkspaceWindow.h"

#include <anim/AnimationClip.h>
#include <anim/Skeleton.h>
#include <assets/cook/MeshImportSettings.h>
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

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <deque>
#include <format>
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
// The blendspace pad, in window pixels from the stage's corner.
constexpr float kPadMargin = 16.0f;
constexpr float kPadSize = 240.0f;
constexpr float kPadLineHeight = 64.0f;
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
                                       ActionCatalog& catalog, const DataSubtypeAuthoringSetup& authorings)
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
    RuleCard = std::make_unique<AnimationRuleProperties>(*Workspace,
                                                         [this](const std::string& path) { OpenDocument(path); });
    LayerCard = std::make_unique<AnimationLayerProperties>(*Workspace);
    FlowCard = std::make_unique<AnimationFlowProperties>(*Workspace,
                                                         [this](const std::string& path) { OpenDocument(path); });
    BlendCard = std::make_unique<AnimationBlendProperties>(*Workspace);
    RootMotionCard = std::make_unique<AnimationRootMotionProperties>(*Workspace);
    FactCard = std::make_unique<AnimationFactProperties>(*Workspace);
    Clock = std::make_unique<AnimationTimeline>(*Workspace);
    EventCard = std::make_unique<AnimationEventProperties>(*Workspace, [this] { return PickedEvent; });
    Audition = std::make_unique<AnimationAuditionProperties>(*Workspace);
    Scenario = std::make_unique<AnimationScenarioProperties>(*Workspace);
    Creation = std::make_unique<AnimationRigCreation>(*Workspace, [this] { Subject = CardSubject::Stage; });
    authorings(Authoring);
    DocumentSheet = std::make_unique<DataProperties>(
        Workspace->Documents, [this] { return Workspace->Documents.Active(); }, &Authoring);
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
    {
        Picture = std::make_unique<ContentPicture>(*ui, "animation_stage");
        Pad = std::make_unique<PlanePad>(*ui);
    }

    Surface.File.Save = [this, &sources] {
        if (AnimationClipEventsDocument* events = AuditionedEvents(); events != nullptr && ShowingLens(AnimationLens::Events))
            (void)sources.Save(Workspace->ClipEvents.RefOf(*events));
        else if (const DataDocument* active = Workspace->Documents.Active())
            (void)sources.Save(Workspace->Documents.RefOf(*active));
    };
    AddAnimationPreviewPanels(Surface, *Workspace);
    DefineActions(catalog);
    DefineTransport(catalog);
    DefineEventActions(catalog);
    DefineLenses(catalog);
    RegisterCommands();
}

AnimationWorkspace::~AnimationWorkspace()
{
    EngineRef.Console().Registry().UnregisterOwner(kCommandOwner);
    Workspace->Sources.CancelEdits();
    // The preview borrows the scene the workspace extracts into.
    DetachRenderFeature(EngineRef, Viewport);
    Surface = WorkspaceView{};
    Pad.reset();
    Picture.reset();
    NewDocument.reset();
    DocumentSheet.reset();
    Creation.reset();
    Scenario.reset();
    Audition.reset();
    FactCard.reset();
    RootMotionCard.reset();
    BlendCard.reset();
    FlowCard.reset();
    LayerCard.reset();
    RuleCard.reset();
    Bone.reset();
    EventCard.reset();
    Clock.reset();
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
    EngineRef.Console().Registry().RegisterCommand({
        .Name = "animation.rig",
        .Owner = std::string(kCommandOwner),
        .Usage = "animation.rig <rig>",
        .Help = "Open a rig and its scenario in the animation workspace.",
        .Callback = [this](ConsoleExecutionContext&, std::span<const std::string> args) {
            ConsoleResult result;
            if (args.size() != 1)
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("expected <rig>");
                return result;
            }
            if (!Workspace->OpenRig(args[0]))
            {
                result.Status = ConsoleStatus::ExecutionFailed;
                result.Error(Workspace->Rig.Error);
                return result;
            }
            result.Info("opened '" + args[0] + "'");
            return result;
        },
    });
}

void AnimationWorkspace::DefineActions(ActionCatalog& catalog)
{
    OfferAction(catalog, ActionHandlers, FrameAction(), { .Invoke = [this] {
                    if (Viewport != nullptr)
                        (void)Viewport->FrameSubject();
                } });
    OfferAction(catalog, ActionHandlers, StepBackAction(), { .Invoke = [this] {
                    Workspace->Sources.CancelEdits();
                    AnimationNavigation& navigation = Workspace->Rig.Navigation;
                    if (navigation.Joint >= 0)
                        navigation.Joint = -1;
                    else if (ShowingLens(AnimationLens::Rules) && navigation.Rule >= 0)
                        navigation.Rule = -1;
                    else if (ShowingLens(AnimationLens::Layers) && LayerCard->StepBack())
                        return;
                    else if (ShowingLens(AnimationLens::Events) && PickedEvent)
                        PickedEvent.reset();
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
                    ShowCard(CardSubject::NewDocument);
                } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "animation.document.close", .Label = "Close Document", .Category = "File",
                  .DefaultChords = { { SDLK_W, { .Ctrl = true } } } },
                { .Invoke = [this] { CloseActiveDocument(); }, .Enabled = documentOpen });

    const auto rigOpen = [this] { return Workspace->Rig.Simulation.IsOpen() && !Workspace->Rig.Path.empty(); };
    OfferAction(catalog, ActionHandlers, { .Id = "animation.rig.new", .Label = "New Rig", .Category = "Resource" },
                { .Invoke = [this] {
                    ShowCard(CardSubject::NewRig);
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

void AnimationWorkspace::DefineTransport(ActionCatalog& catalog)
{
    const auto offer = [&](std::string id, std::string label, std::vector<KeyChord> chords, ActionHandler handler) {
        OfferAction(catalog, ActionHandlers,
                    { .Id = std::move(id), .Label = std::move(label), .Category = "Timeline",
                      .DefaultChords = std::move(chords) },
                    std::move(handler));
    };
    offer("timeline.play", "Play", { { SDLK_SPACE, {} } },
          { .Invoke = [this] { Clock->TogglePlay(); }, .Checked = [this] { return Clock->IsPlaying(); } });
    offer("timeline.restart", "Restart", { { SDLK_HOME, {} } }, { .Invoke = [this] { Clock->Restart(); } });
    offer("timeline.step_back", "Step Back", { { SDLK_COMMA, {} } }, { .Invoke = [this] { Clock->Step(-1); } });
    offer("timeline.step_forward", "Step Forward", { { SDLK_PERIOD, {} } }, { .Invoke = [this] { Clock->Step(1); } });
    offer("timeline.loop", "Loop", { { SDLK_L, {} } },
          { .Invoke = [this] { Workspace->Audition.Session.SetLoop(!Workspace->Audition.Session.IsLooping()); },
            .Enabled = [this] { return !Clock->IsSimulation(); },
            .Checked = [this] { return !Clock->IsSimulation() && Workspace->Audition.Session.IsLooping(); } });
    offer("timeline.slower", "Slower", { { SDLK_LEFTBRACKET, {} } }, { .Invoke = [this] { Clock->ChangeSpeed(-1); } });
    offer("timeline.faster", "Faster", { { SDLK_RIGHTBRACKET, {} } }, { .Invoke = [this] { Clock->ChangeSpeed(1); } });
    offer("timeline.run_ahead", "Run a Second Ahead", { { SDLK_PERIOD, { .Shift = true } } },
          { .Invoke = [this] { Clock->RunAhead(); }, .Enabled = [this] { return Clock->IsSimulation(); } });
    offer("timeline.live", "Back to Live", { { SDLK_END, {} } },
          { .Invoke = [this] { Clock->ReturnToLive(); }, .Checked = [this] { return Clock->IsLive(); } });
}

namespace
{
struct LensRow
{
    AnimationLens Lens;
    std::string_view Action;
    std::string_view Label;
    std::string_view Hint;
};
constexpr std::array<LensRow, 7> kLenses{ {
    { AnimationLens::Rules, "animation.lens.rules", "Rules",
      "Inspect a rule to see why it won or lost; the deck's lane shows what won the layer" },
    { AnimationLens::Layers, "animation.lens.layers", "Layers",
      "Inspect a layer for its weight and mask; pick a joint to mask it" },
    { AnimationLens::Flow, "animation.lens.flow", "Flow",
      "The deck's lane shows the sections the layer played; Edit opens the flow as a sheet" },
    { AnimationLens::Blends, "animation.lens.blends", "Blends",
      "Drag the pad to move a blendspace; the deck's lane shows each blend as long as it ran" },
    { AnimationLens::RootMotion, "animation.lens.root_motion", "Root motion",
      "Move the character to see how far its clips carry it; the deck plots carried against achieved" },
    { AnimationLens::Facts, "animation.lens.facts", "Facts",
      "Drive an input fact for the next tick; Plot puts a fact along the deck" },
    { AnimationLens::Events, "animation.lens.events", "Events", "E adds an event at the playhead" },
} };
} // namespace

void AnimationWorkspace::DefineLenses(ActionCatalog& catalog)
{
    for (const LensRow& lens : kLenses)
        OfferAction(catalog, ActionHandlers,
                    { .Id = std::string(lens.Action), .Label = std::string(lens.Label), .Category = "Lens" },
                    { .Invoke = [this, which = lens.Lens] { ShowLens(which); },
                      .Checked = [this, which = lens.Lens] { return ShownLens == which; } });
}

WorkspaceOperationFacet AnimationWorkspace::Operation()
{
    const auto shown = std::ranges::find(kLenses, ShownLens, &LensRow::Lens);
    WorkspaceOperationFacet facet{ .Name = std::string(shown->Label), .Hint = std::string(shown->Hint) };
    for (const LensRow& lens : kLenses)
        facet.Choices.push_back(
            { .Label = std::string(lens.Label), .Action = std::string(lens.Action), .Current = lens.Lens == ShownLens });
    return facet;
}

void AnimationWorkspace::DefineEventActions(ActionCatalog& catalog)
{
    const auto events = [this] { return !Clock->IsSimulation() && AuditionedEvents() != nullptr; };
    OfferAction(catalog, ActionHandlers,
                { .Id = "animation.event.add", .Label = "Add Event at Playhead", .Category = "Edit",
                  .DefaultChords = { { SDLK_E, {} } } },
                { .Invoke = [this] {
                      AnimationClipEventsDocument* document = AuditionedEvents();
                      if (document == nullptr)
                          return;
                      AnimationClipEvent event;
                      event.Time = static_cast<float>(std::clamp(Workspace->Audition.Session.NormalizedTime(), 0.0, 1.0));
                      if (const AnimBoundRig* rig = Workspace->Rig.Simulation.Rig(); rig != nullptr && rig->Bindings.Size() > 0)
                          event.Binding = rig->Bindings.All().front().KeyText;
                      else
                          event.Binding = "anim.event";
                      PickedEvent = document->Add(std::move(event));
                      Workspace->ClipEvents.Changed(*document);
                      ShowLens(AnimationLens::Events);
                  },
                  .Enabled = events });
    OfferAction(catalog, ActionHandlers, DeleteAction(),
                { .Invoke = [this] {
                      AnimationClipEventsDocument* document = AuditionedEvents();
                      if (document == nullptr || !PickedEvent)
                          return;
                      document->Remove(*PickedEvent);
                      PickedEvent.reset();
                      Workspace->ClipEvents.Changed(*document);
                  },
                  .Enabled = [this] { return ShowingLens(AnimationLens::Events) && PickedEvent.has_value(); } });
}

AnimationClipEventsDocument* AnimationWorkspace::AuditionedEvents() const
{
    AnimationClipEventsDocument* document = Workspace->ClipEvents.Active();
    return document != nullptr && document->ClipPath() == Workspace->Audition.ClipPath ? document : nullptr;
}

void AnimationWorkspace::FollowAuditionedClip()
{
    const std::string& clip = Workspace->Audition.ClipPath;
    if (clip == FollowedClip)
        return;
    FollowedClip = clip;
    PickedEvent.reset();
    if (!clip.empty() && MeshClipSourceOf(clip).has_value())
    {
        Workspace->Sources.CancelEdits();
        std::string error;
        (void)Workspace->ClipEvents.OpenOrFocus(clip, error);
    }
}

WorkspaceTimelineFacet AnimationWorkspace::Timeline()
{
    WorkspaceTimelineFacet facet = Clock->Facet();
    const AnimationNavigation& navigation = Workspace->Rig.Navigation;
    if (ShownLens == AnimationLens::Rules && Clock->IsSimulation())
        facet.Lanes.push_back(AnimationDecisionLane(Workspace->Rig.Simulation, navigation.Layer, navigation.Rule));
    if (ShownLens == AnimationLens::Facts && Clock->IsSimulation() && !FactCard->Plotted().empty())
        facet.Lanes.push_back(AnimationFactLane(Workspace->Rig.Simulation, FactCard->Plotted()));
    if (ShownLens == AnimationLens::RootMotion && Clock->IsSimulation())
        for (TimelineLane& lane : AnimationTravelLanes(Workspace->Rig.Simulation))
            facet.Lanes.push_back(std::move(lane));
    if (ShownLens == AnimationLens::Blends && Clock->IsSimulation())
        facet.Lanes.push_back(AnimationBlendLane(Workspace->Rig.Simulation, navigation.Layer));
    if (ShownLens == AnimationLens::Flow && Clock->IsSimulation())
        facet.Lanes.push_back(AnimationFlowLane(Workspace->Rig.Simulation, navigation.Layer));
    // What won each layer, as many as the deck has lanes for.
    if (const AnimBoundRig* rig = Workspace->Rig.Simulation.Rig();
        ShownLens == AnimationLens::Layers && Clock->IsSimulation() && rig != nullptr)
        for (std::size_t layer = 0; layer < rig->Layers.size() && layer < kTimelineLaneLimit; ++layer)
            facet.Lanes.push_back(AnimationDecisionLane(Workspace->Rig.Simulation, layer,
                                                        layer == navigation.Layer ? navigation.Rule : -1));
    const AnimationClipEventsDocument* document = Clock->IsSimulation() ? nullptr : AuditionedEvents();
    if (document == nullptr || facet.End <= 0.0)
        return facet;
    for (const AnimationClipEvent& event : document->Events())
        facet.Marks.push_back({ .Key = std::to_string(event.Key),
                                .Time = static_cast<double>(event.Time) * facet.End,
                                .Label = EventLabel(event),
                                .Shape = event.Scope == AnimEventScope::Gameplay ? TimelineMarkShape::Diamond
                                                                                  : TimelineMarkShape::Round,
                                .Selected = PickedEvent == event.Key,
                                .Movable = true });
    return facet;
}

void AnimationWorkspace::PickOnTimeline(std::string_view key)
{
    if (const std::optional<AnimationLanePick> pick = ParseAnimationLaneKey(key))
    {
        AnimationNavigation& navigation = Workspace->Rig.Navigation;
        navigation.Layer = pick->Layer;
        if (pick->Names == AnimationLanePick::What::Section)
        {
            ShowLens(AnimationLens::Flow);
            return;
        }
        if (pick->Names == AnimationLanePick::What::Blend)
        {
            ShowLens(AnimationLens::Blends);
            return;
        }
        navigation.Rule = pick->Index;
        ShowLens(AnimationLens::Rules);
        return;
    }
    std::uint32_t event = 0;
    if (std::from_chars(key.data(), key.data() + key.size(), event).ec != std::errc{})
        return;
    PickedEvent = event;
    ShowLens(AnimationLens::Events);
}

void AnimationWorkspace::MoveMark(std::string_view key, double time, TimelineDrag drag)
{
    AnimationClipEventsDocument* document = AuditionedEvents();
    const double duration = Workspace->Audition.Session.Duration();
    std::uint32_t eventKey = 0;
    if (document == nullptr || duration <= 0.0
        || std::from_chars(key.data(), key.data() + key.size(), eventKey).ec != std::errc{})
        return;
    if (drag == TimelineDrag::Begin)
    {
        PickOnTimeline(key);
        Workspace->Sources.CancelEdits();
    }
    else if (document->EditingKey() != eventKey)
        return;
    PreviewEventTime(Workspace->ClipEvents, *document, eventKey, time / duration);
    if (drag == TimelineDrag::End && document->IsEditing())
    {
        document->CommitEdit();
        Workspace->ClipEvents.Changed(*document);
    }
}

void AnimationWorkspace::Scrub(double time, TimelineDrag)
{
    Clock->Scrub(time);
}

void AnimationWorkspace::Tick(FrameUpdateContext& ctx)
{
    // The preview runs only while it can be seen; a background tab keeps its
    // place and resumes where it stopped.
    if (!Visible)
        return;
    FollowAuditionedClip();
    if (const DataDocument* document = Workspace->Documents.Active(); document != nullptr && Subject == CardSubject::Document)
        if (IDataSubtypeAuthoring* authoring = Authoring.Find(document->Subtype()))
            authoring->Update(*document, Workspace->Documents);
    Workspace->Advance(ctx.WallDeltaSeconds);
    Workspace->ExtractViewport();
    // A new subject on the stage is framed once it can be: a mesh, or a skeleton alone.
    const AnimationAuditionSelection& audition = Workspace->Audition;
    const std::string& subject = audition.MeshPath.empty() ? audition.Session.SkeletonPath() : audition.MeshPath;
    if (Viewport != nullptr && FramedSubject != subject && Viewport->FrameSubject())
        FramedSubject = subject;
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
    ShowPad();
}

void AnimationWorkspace::ShowPad()
{
    const std::optional<AnimationBlendspaceView> space =
        ShownLens == AnimationLens::Blends && Clock->IsSimulation()
            ? AnimationBlendspaceOn(Workspace->Rig.Simulation, Workspace->Rig.Navigation.Layer)
            : std::nullopt;
    if (Pad == nullptr || !space)
    {
        if (Pad != nullptr)
            Pad->Hide();
        if (Drag == StageDrag::Pad)
            Drag = StageDrag::None;
        return;
    }
    std::vector<PlanePadMark> marks;
    for (const AnimationBlendspaceMark& sample : space->Samples)
        marks.push_back({ .At = Vec2d(static_cast<float>(sample.U), static_cast<float>(sample.V)),
                          .Weight = sample.Weight,
                          .Label = sample.Label });
    const bool line = space->Axes < 2;
    const Rect2d region(ContentRect.Position + Vec2d(kPadMargin, kPadMargin),
                        Vec2d(kPadSize, line ? kPadLineHeight : kPadSize));
    std::string caption = space->AxisFact[0];
    if (!line)
        caption += " Ã " + space->AxisFact[1];
    Pad->Show(region, marks, Vec2d(static_cast<float>(space->U), static_cast<float>(space->V)), std::move(caption),
              line);
}

void AnimationWorkspace::DragPadTo(Vec2d windowPoint)
{
    const Vec2d at = Pad->ValueAt(windowPoint);
    (void)PlaceAnimationBlendspacePoint(Workspace->Rig.Simulation, Workspace->Rig.Navigation.Layer, at.X, at.Y);
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
    if (Pad != nullptr && !visible)
        Pad->Hide();
    if (!visible)
        Interrupt();
}

void AnimationWorkspace::MoveTo(const WorkspaceWindow& window)
{
    if (Picture != nullptr)
        Picture->MoveTo(window.Content);
    if (Pad != nullptr)
        Pad->MoveTo(window.Content);
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
        if (event.button.button == SDL_BUTTON_LEFT && onStage(event.button.x, event.button.y, event.button.windowID)
            && Pad != nullptr && Pad->Contains(Vec2d(event.button.x, event.button.y)))
        {
            Drag = StageDrag::Pad;
            DragPadTo(Vec2d(event.button.x, event.button.y));
            ctx.Handled = true;
        }
        else if (event.button.button == SDL_BUTTON_LEFT
                 && onStage(event.button.x, event.button.y, event.button.windowID))
        {
            Drag = StageDrag::Pressed;
            PressedAt = Vec2d(event.button.x, event.button.y);
            ctx.Handled = true;
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (Drag == StageDrag::Pad)
        {
            DragPadTo(Vec2d(event.motion.x, event.motion.y));
            ctx.Handled = true;
        }
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
    if (const AnimationClipEventsDocument* events = AuditionedEvents())
        facet.Open.push_back({ .Key = "events:" + events->ClipPath(),
                               .Label = std::string(LeafOf(events->ClipPath())) + " events",
                               .Changed = events->IsDirty() });
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
    if (key.starts_with("events:"))
    {
        PickedEvent.reset();
        ShowLens(AnimationLens::Events);
        return;
    }
    if (const std::optional<std::size_t> index = Workspace->Documents.IndexOf(key))
        Workspace->Documents.SetActive(*index);
    ShowCard(CardSubject::Document);
}

void AnimationWorkspace::ShowCard(CardSubject subject)
{
    Workspace->Rig.Navigation.Joint = -1;
    Subject = subject;
}

void AnimationWorkspace::ShowLens(AnimationLens lens)
{
    ShownLens = lens;
    ShowCard(CardSubject::Lens);
}

void AnimationWorkspace::OpenDocument(const std::string& path)
{
    if (Workspace->Documents.OpenOrFocus(path, Workspace->DocumentError) == nullptr)
        return;
    ShowCard(CardSubject::Document);
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
    if (ShowingLens(AnimationLens::Facts))
        return { .Title = "Facts", .Sections = { { .Heading = {}, .Source = FactCard.get() } } };
    if (ShowingLens(AnimationLens::RootMotion))
        return { .Title = "Root motion", .Sections = { { .Heading = {}, .Source = RootMotionCard.get() } } };
    if (ShowingLens(AnimationLens::Blends))
        return { .Title = "Blends",
                 .Sections = { { .Heading = {}, .Source = BlendCard.get() } },
                 .Details = { { .Heading = "Overrides and A/B", .Source = &BlendCard->Comparison() } } };
    if (ShowingLens(AnimationLens::Flow))
        return { .Title = FlowCard->Title(), .Sections = { { .Heading = {}, .Source = FlowCard.get() } } };
    if (ShowingLens(AnimationLens::Layers))
        return { .Title = LayerCard->Title(), .Sections = { { .Heading = {}, .Source = LayerCard.get() } } };
    if (ShowingLens(AnimationLens::Rules))
        return { .Title = RuleCard->Title(),
                 .Sections = { { .Heading = {}, .Source = RuleCard.get() } },
                 .Details = { { .Heading = "This tick", .Source = &RuleCard->Decisions() } } };
    if (ShowingLens(AnimationLens::Events) && AuditionedEvents() != nullptr)
    {
        const AnimationClipEventsDocument& document = *AuditionedEvents();
        const AnimationClipEvent* event = PickedEvent ? document.Find(*PickedEvent) : nullptr;
        return { .Title = event == nullptr ? std::string(LeafOf(document.ClipPath())) + " events" : EventLabel(*event),
                 .Actions = { "animation.event.add", "edit.delete", "file.save" },
                 .Sections = { { .Heading = {}, .Source = EventCard.get() } } };
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
