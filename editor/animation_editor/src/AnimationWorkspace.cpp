#include "AnimationWorkspace.h"

#include "authoring/AnimationAdmissionProperties.h"
#include "authoring/AnimationBlendProperties.h"
#include "authoring/AnimationBlendspacePad.h"
#include "authoring/AnimationBoneProperties.h"
#include "authoring/AnimationEventProperties.h"
#include "authoring/AnimationExplorers.h"
#include "authoring/AnimationFactProperties.h"
#include "authoring/AnimationFlowProperties.h"
#include "authoring/AnimationLabProperties.h"
#include "authoring/AnimationLayerProperties.h"
#include "authoring/AnimationMigrationProperties.h"
#include "authoring/AnimationPreviewStatus.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRequestProperties.h"
#include "authoring/AnimationRigCreation.h"
#include "authoring/AnimationRigDocumentEdits.h"
#include "authoring/AnimationRigProblems.h"
#include "authoring/AnimationRigReportProperties.h"
#include "authoring/AnimationRootMotionProperties.h"
#include "authoring/AnimationRuleProperties.h"
#include "authoring/AnimationScenarioBatchProperties.h"
#include "authoring/AnimationStageProperties.h"
#include "authoring/AnimationStageStrokes.h"
#include "authoring/AnimationTimeline.h"
#include "authoring/AnimationTimelineLanes.h"
#include "authoring/AnimationTraceProperties.h"
#include "problems/ProblemList.h"
#include "project/ProjectFiles.h"
#include "render/AnimationPreviewRenderFeature.h"
#include "text/PathLeaf.h"

#include "actions/ActionOffer.h"
#include "data/DataCreation.h"
#include "data/DataDocumentSheet.h"
#include "data/DataDocumentStore.h"
#include "documents/DocumentSourceSet.h"
#include "presenters/ContentPicture.h"
#include "presenters/PlanePad.h"
#include "project/Project.h"
#include "prompts/DocumentPrompts.h"
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
#include <ui/UiService.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <format>
#include <initializer_list>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace
{
constexpr std::string_view kCommandOwner = "editor.animation";
constexpr std::string_view kProblemSource = "animation";
constexpr std::string_view kDeclareAction = "animation.declare_names";
constexpr std::string_view kCloseAction = "animation.document.close";
constexpr float kPickRadius = 8.0f;
constexpr float kJointRadius = 3.0f;
constexpr float kPickedJointRadius = 5.0f;
// The blendspace pad, in window pixels from the stage's corner.
constexpr float kPadMargin = 16.0f;
constexpr float kPadSize = 240.0f;
constexpr float kPadLineHeight = 64.0f;
} // namespace

ActionDefinition NewRigAction()
{
    return { .Id = "animation.rig.new", .Label = "New Rig", .Category = "Resource" };
}

AnimationWorkspace::AnimationWorkspace(Engine& engine, const ProjectDescriptor& project, Game* module,
                                       DocumentSourceSet& sources, DataDocumentStore& store, ProjectFileSteps* files,
                                       PromptQueue& prompts, ActionCatalog& catalog, ProblemList& problems,
                                       const DataSubtypeAuthoringSetup& authorings)
    : EngineRef(engine)
    , Sources(sources)
    , Store(store)
    , Problems(problems)
    , Prompts(prompts.MakeAsker())
{
    std::function<void(World&)> vocabulary;
    if (module != nullptr)
        vocabulary = [module](World& world) { module->OnRegisterVocabulary(world); };
    const std::filesystem::path authoringRoot =
        project.ContentRoots.empty() ? std::filesystem::path{} : std::filesystem::path(project.ContentRoots.front());
    Workspace = std::make_unique<AnimationPreviewWorkspace>(engine.Content().Assets(), sources, store,
                                                            std::move(vocabulary), authoringRoot);
    const auto openDocument = [this](const std::string& path) { (void)OpenDocument(path); };
    const auto openRig = [this](const std::string& path) { OpenRig(path); };
    Bone = std::make_unique<AnimationBoneProperties>(*Workspace);
    RuleCard = std::make_unique<AnimationRuleProperties>(*Workspace, openDocument);
    LayerCard = std::make_unique<AnimationLayerProperties>(*Workspace);
    FlowCard = std::make_unique<AnimationFlowProperties>(*Workspace, openDocument);
    BlendCard = std::make_unique<AnimationBlendProperties>(*Workspace);
    RootMotionCard = std::make_unique<AnimationRootMotionProperties>(*Workspace);
    FactCard = std::make_unique<AnimationFactProperties>(*Workspace);
    RequestCard = std::make_unique<AnimationRequestProperties>(*Workspace);
    Admissions = std::make_unique<AnimationAdmissionProperties>(*Workspace);
    RigReport = std::make_unique<AnimationRigReportProperties>(*Workspace);
    Batch = std::make_unique<AnimationScenarioBatchProperties>(*Workspace, openRig);
    Migration = std::make_unique<AnimationMigrationProperties>(*Workspace);
    Trace = std::make_unique<AnimationTraceProperties>(openRig);
    LabSheet = std::make_unique<AnimationLabProperties>(*Workspace);
    Clock = std::make_unique<AnimationTimeline>(*Workspace);
    EventCard = std::make_unique<AnimationEventProperties>(*Workspace, [this] { return PickedEvent; });
    Audition = std::make_unique<AnimationAuditionProperties>(*Workspace);
    Scenario = std::make_unique<AnimationScenarioProperties>(*Workspace);
    Creation = std::make_unique<AnimationRigCreation>(*Workspace, [this] { ShowCard(CardSubject::Stage); });
    Sheet = std::make_unique<DataDocumentSheet>(Workspace->Documents, prompts, authorings, files);
    NewDocument = std::make_unique<DataCreation>(
        [this] { return Workspace->Documents.CreatableSubtypes(); },
        [this](const std::string& subtype, const std::string& path, std::string& error) {
            if (Workspace->Documents.Create(subtype, path, error) == nullptr)
                return false;
            Workspace->RefreshBrowser();
            ShowCard(CardSubject::Document);
            return true;
        });

    Viewport = AttachRenderFeature(
        engine, std::make_unique<AnimationPreviewRenderFeature>(engine.Content().Assets(), Workspace->Viewport.Scene),
        "animation_preview");
    if (Viewport == nullptr)
        SetupProblem = "The animation preview did not set up, so the stage will not draw.";
    if (UiService* ui = engine.TryUi(); ui != nullptr && ui->IsReady())
    {
        Picture = std::make_unique<ContentPicture>(*ui, "animation_stage");
        Pad = std::make_unique<PlanePad>(*ui);
    }

    DefineActions(catalog);
    DefineDocumentActions(catalog);
    DefineExplorers(catalog);
    DefineTransport(catalog);
    DefineEventActions(catalog);
    DefineLenses(catalog);
    DefineRuns(catalog);
    RegisterCommands();
}

AnimationWorkspace::~AnimationWorkspace()
{
    EngineRef.Console().Registry().UnregisterOwner(kCommandOwner);
    Workspace->Sources.CancelEdits();
    // The preview borrows the scene the workspace extracts into.
    DetachRenderFeature(EngineRef, Viewport);
    Pad.reset();
    Picture.reset();
    NewDocument.reset();
    Sheet.reset();
    Creation.reset();
    Scenario.reset();
    Audition.reset();
    Problems.Clear(kProblemSource);
    LabSheet.reset();
    Trace.reset();
    Migration.reset();
    Batch.reset();
    RigReport.reset();
    Admissions.reset();
    RequestCard.reset();
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
                    else if (const LensView& lens = ViewOf(ShownLens);
                             Subject == CardSubject::Lens && lens.StepBack && lens.StepBack())
                        return;
                    else
                        ShowCard(CardSubject::Stage);
                } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "animation.bind_pose", .Label = "Bind Pose", .Category = "Explore" },
                { .Invoke = [this] { (void)Workspace->AuditionClip({}); } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "animation.neutral_material", .Label = "Neutral Material", .Category = "Explore" },
                { .Invoke = [this] { (void)Workspace->Audition.SelectMaterial({}); } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "animation.refresh", .Label = "Refresh Content", .Category = "Explore", .Icon = IconId::Refresh },
                { .Invoke = [this] { Workspace->RefreshBrowser(); } });
    OfferAction(catalog, ActionHandlers, NewRigAction(), { .Invoke = [this] { ShowCard(CardSubject::NewRig); } });
    OfferAction(catalog, ActionHandlers, { .Id = "animation.view.joints", .Label = "Joints", .Category = "View" },
                { .Invoke = [this] { ShowJoints = !ShowJoints; }, .Checked = [this] { return ShowJoints; } });
    OfferAction(catalog, ActionHandlers, { .Id = "animation.view.path", .Label = "Path", .Category = "View" },
                { .Invoke = [this] { ShowPath = !ShowPath; },
                  .Enabled = [this] { return Workspace->Rig.Simulation.SubjectTransform() != nullptr; },
                  .Checked = [this] { return ShowPath; } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "timeline.source.audition", .Label = "Audition Clock", .Category = "View" },
                { .Invoke = [this] {
                      CancelRetime();
                      Workspace->Viewport.Source = AnimationViewportSource::Audition;
                  },
                  .Checked = [this] { return Workspace->Viewport.Source == AnimationViewportSource::Audition; } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "timeline.source.simulation", .Label = "Simulation Clock", .Category = "View" },
                { .Invoke = [this] {
                      CancelRetime();
                      Workspace->Viewport.Source = AnimationViewportSource::Simulation;
                  },
                  .Enabled = [this] { return Workspace->Rig.Simulation.IsOpen(); },
                  .Checked = [this] { return Workspace->Viewport.Source == AnimationViewportSource::Simulation; } });
}

void AnimationWorkspace::DefineDocumentActions(ActionCatalog& catalog)
{
    OfferAction(catalog, ActionHandlers, SaveAction(), { .Invoke = [this] {
                    if (const std::optional<DocumentRef> edited = EditedDocument())
                        (void)Sources.Save(*edited);
                } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "animation.document.new", .Label = "New Animation Document", .Category = "File",
                  .DefaultChords = { { SDLK_N, { .Ctrl = true } } } },
                { .Invoke = [this] {
                    NewDocument->Clear();
                    ShowCard(CardSubject::NewDocument);
                } });
    OfferAction(catalog, ActionHandlers,
                { .Id = std::string(kCloseAction), .Label = "Close Document", .Category = "File",
                  .DefaultChords = { { SDLK_W, { .Ctrl = true } } } },
                { .Invoke = [this] { CloseEditedDocument(); },
                  .Enabled = [this] {
                      const std::optional<DocumentRef> edited = EditedDocument();
                      return edited && edited->Source != &Workspace->Rig.Scenario;
                  } });
    const auto rigOpen = [this] { return Workspace->Rig.Simulation.IsOpen() && Workspace->Rig.Scenario.IsOpen(); };
    OfferAction(catalog, ActionHandlers,
                { .Id = "animation.scenario.save", .Label = "Save Scenario", .Category = "Resource" },
                { .Invoke = [this] { (void)Sources.Save(Workspace->Rig.Scenario.Ref()); }, .Enabled = rigOpen });
    OfferAction(catalog, ActionHandlers,
                { .Id = "animation.scenario.revert", .Label = "Revert Scenario", .Category = "Resource" },
                { .Invoke = [this] { (void)Workspace->RevertScenario(); },
                  .Enabled = [this, rigOpen] { return rigOpen() && Workspace->Rig.Scenario.IsChanged(); } });
}

void AnimationWorkspace::DefineExplorers(ActionCatalog& catalog)
{
    // A group opens or closes rather than being chosen.
    const auto flipped = [](TreeOutline& outline, std::string_view key) {
        const std::optional<AnimationExplorerItem> item = ParseAnimationExplorerKey(key);
        if (!item || item->What != AnimationExplorerItem::Kind::Group)
            return false;
        outline.Flip(key);
        return true;
    };
    ExplorerRows.Offer(
        catalog, ActionHandlers,
        { .Id = "anim_content", .Title = "Content", .Icon = IconId::Folder, .Chords = { { SDLK_C, {} } },
          .Explore =
              [this](const ExplorerQuery& query) {
                  ExplorerPage page = ContentOutline.Explore(AnimationContentNodes(*Workspace), query);
                  page.Actions = { "animation.bind_pose", "animation.neutral_material", "animation.refresh" };
                  page.Empty = "No meshes, skeletons, clips or materials hold that.";
                  return page;
              },
          .Choose =
              [this, flipped](std::string_view key, ExplorerChoice, std::size_t) {
                  if (flipped(ContentOutline, key))
                      return;
                  const std::optional<AnimationExplorerItem> item = ParseAnimationExplorerKey(key);
                  if (!item)
                      return;
                  AnimationAuditionSelection& audition = Workspace->Audition;
                  using Kind = AnimationExplorerItem::Kind;
                  if (item->What == Kind::Mesh)
                      (void)audition.SelectMesh(item->Path);
                  else if (item->What == Kind::Skeleton)
                      (void)audition.SelectSkeleton(item->Path);
                  else if (item->What == Kind::Clip)
                      (void)Workspace->AuditionClip(item->Path);
                  else if (item->What == Kind::Material)
                      (void)audition.SelectMaterial(item->Path);
              } });
    ExplorerRows.Offer(catalog, ActionHandlers,
                       { .Id = "anim_rigs", .Title = "Rigs", .Icon = IconId::Folder, .Chords = { { SDLK_R, {} } },
                         .Explore =
                             [this](const ExplorerQuery& query) {
                                 ExplorerPage page = RigOutline.Explore(AnimationRigNodes(*Workspace), query);
                                 page.Actions = { NewRigAction().Id };
                                 page.Empty = "No rig or animation document holds that.";
                                 return page;
                             },
                         .Choose =
                             [this, flipped](std::string_view key, ExplorerChoice, std::size_t) {
                                 if (flipped(RigOutline, key))
                                     return;
                                 const std::optional<AnimationExplorerItem> item = ParseAnimationExplorerKey(key);
                                 if (item && item->What == AnimationExplorerItem::Kind::Rig)
                                     OpenRig(item->Path);
                                 else if (item && item->What == AnimationExplorerItem::Kind::Document)
                                     (void)OpenDocument(item->Path);
                             } });
    ExplorerRows.Offer(
        catalog, ActionHandlers,
        { .Id = "anim_skeleton", .Title = "Skeleton", .Chords = { { SDLK_J, {} } },
          .Explore =
              [this](const ExplorerQuery& query) {
                  const SkeletonData& skeleton = Workspace->Audition.Session.Skeleton();
                  ExplorerPage page = SkeletonOutline.Explore(
                      AnimationSkeletonNodes(skeleton, Workspace->Rig.Navigation.Joint), query);
                  page.Empty = skeleton.Joints.empty() ? "Nothing with a skeleton is auditioned."
                                                       : "No joint's name holds that.";
                  return page;
              },
          .Choose =
              [this](std::string_view key, ExplorerChoice choice, std::size_t) {
                  const std::optional<AnimationExplorerItem> item = ParseAnimationExplorerKey(key);
                  if (!item)
                      return;
                  if (item->What == AnimationExplorerItem::Kind::Group || choice == ExplorerChoice::Open)
                      SkeletonOutline.Flip(key);
                  else if (item->What == AnimationExplorerItem::Kind::Joint)
                      Workspace->Rig.Navigation.Joint = item->Joint;
              } });
}

void AnimationWorkspace::DefineTransport(ActionCatalog& catalog)
{
    const auto offer = [&](std::string id, std::string label, std::vector<KeyChord> chords, ActionHandler handler) {
        handler.Enabled = [this, enabled = std::move(handler.Enabled)] {
            return !Clock->IsLab() && (!enabled || enabled());
        };
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

void AnimationWorkspace::PublishProblems()
{
    std::vector<AnimDiagnostic> diagnostics = Workspace->Rig.Simulation.Problems();
    diagnostics.insert(diagnostics.end(), Workspace->Rig.LoadProblems.begin(), Workspace->Rig.LoadProblems.end());
    // A working document the preview refused runs on its last valid version.
    std::vector<Problem> kept = Sheet->Problems();
    for (const DataDocument* document : Workspace->Documents.Documents())
        if (const DataResidentState* state = Workspace->Documents.Store().ResidentStateOf(*document);
            state != nullptr && state->Status == DataResidentStatus::KeptLastValid)
            kept.push_back({ .Severity = ProblemSeverity::Warning,
                             .Message = std::format("{}: {}", PathLeaf(document->VirtualPath()),
                                                    AnimationPreviewStatusText(state)),
                             .Document = Workspace->Documents.RefOf(*document) });
    for (const std::string* error : { &SetupProblem, &DeclareError, &Workspace->DocumentError, &Workspace->Rig.Error })
        if (!error->empty())
            kept.push_back({ .Severity = ProblemSeverity::Error, .Message = *error });
    std::string signature;
    for (const Problem& problem : kept)
        signature += problem.Message + problem.Location + "\n";
    for (const AnimDiagnostic& diagnostic : diagnostics)
        signature += diagnostic.AssetPath + diagnostic.FieldPath + diagnostic.Code + diagnostic.Message + "\n";
    if (signature == ProblemSignature)
        return;
    ProblemSignature = std::move(signature);
    // Reads documents, so only when the problems change.
    Undeclared = UndeclaredAnimationNamesOf(Workspace->Rig.Simulation, Workspace->Documents);

    std::vector<Problem> problems = AnimationRigProblems(diagnostics, Undeclared, Workspace->Documents.Store());
    problems.insert(problems.end(), std::make_move_iterator(kept.begin()), std::make_move_iterator(kept.end()));
    Problems.Publish(kProblemSource, std::move(problems));
}

bool AnimationWorkspace::RevealProblem(const Problem& problem)
{
    if (problem.Location == kUndeclaredAnimationNames)
    {
        ShowCard(CardSubject::Stage);
        return true;
    }
    if (!problem.Document || problem.Document->Source != &Workspace->Documents.Store())
        return false;
    return OpenDocument(problem.Document->Key);
}

void AnimationWorkspace::DefineRuns(ActionCatalog& catalog)
{
    Runs = {
        { .Kind = RunKind::Lab, .Action = "animation.run.lab", .Label = "Session Lab", .Title = "Session lab",
          .Source = LabSheet.get() },
        { .Kind = RunKind::Batch, .Action = "animation.run.batch", .Label = "Scenario Batch",
          .Title = "Scenario batch", .Source = Batch.get() },
        { .Kind = RunKind::Migration, .Action = "animation.run.migration", .Label = "Clip Player Migration",
          .Title = "Clip player migration", .Source = Migration.get(),
          .Opened = [this] { Migration->ScanOnce(); } },
        { .Kind = RunKind::Trace, .Action = "animation.run.trace", .Label = "Imported Trace",
          .Title = "Imported trace", .Source = Trace.get() },
    };
    for (const RunView& run : Runs)
        OfferAction(catalog, ActionHandlers,
                    { .Id = std::string(run.Action), .Label = std::string(run.Label), .Category = "Run" },
                    { .Invoke = [this, kind = run.Kind] { ShowRun(kind); } });
}

const AnimationWorkspace::RunView& AnimationWorkspace::RunOf(RunKind kind) const
{
    return *std::ranges::find(Runs, kind, &RunView::Kind);
}

void AnimationWorkspace::ShowRun(RunKind kind)
{
    ShownRun = kind;
    if (const RunView& run = RunOf(kind); run.Opened)
        run.Opened();
    ShowCard(CardSubject::Run);
}

void AnimationWorkspace::DefineLenses(ActionCatalog& catalog)
{
    OfferAction(catalog, ActionHandlers,
                { .Id = std::string(kDeclareAction), .Label = "Declare Names Beside the Rig", .Category = "Resource" },
                { .Invoke = [this] {
                      DeclareError.clear();
                      (void)DeclareUndeclaredAnimationNames(Workspace->Documents, Workspace->Rig.Simulation,
                                                            Workspace->Rig.Path, DeclareError);
                      ProblemSignature.clear();
                  },
                  .Enabled = [this] { return !Undeclared.empty(); } });
    DefineLensViews();
    for (const LensView& lens : LensViews)
        OfferAction(catalog, ActionHandlers,
                    { .Id = std::string(lens.Action), .Label = std::string(lens.Label), .Category = "Lens" },
                    { .Invoke = [this, which = lens.Lens] { ShowLens(which); },
                      .Checked = [this, which = lens.Lens] { return ShownLens == which; } });
}

void AnimationWorkspace::DefineLensViews()
{
    using Card = std::optional<WorkspaceSelectionFacet>;
    const auto simulation = [this]() -> AnimationPreviewSession& { return Workspace->Rig.Simulation; };
    const auto navigation = [this]() -> AnimationNavigation& { return Workspace->Rig.Navigation; };
    const auto onLayer = [navigation](const AnimationLanePick& pick) { navigation().Layer = pick.Layer; };
    LensViews = {
        { .Lens = AnimationLens::Rules, .Action = "animation.lens.rules", .Label = "Rules",
          .Hint = "Inspect a rule to see why it won or lost; the deck's lane shows what won the layer",
          .Card = [this] {
              return Card{ { .Title = RuleCard->Title(),
                             .Sections = { { .Heading = {}, .Source = RuleCard.get() } },
                             .Details = { { .Heading = "This tick", .Source = &RuleCard->Decisions() } } } };
          },
          .Lanes = [=](std::vector<TimelineLane>& lanes) {
              lanes.push_back(AnimationDecisionLane(simulation(), navigation().Layer, navigation().Rule));
          },
          .Picked = [=](const AnimationLanePick& pick) {
              navigation().Layer = pick.Layer;
              navigation().Rule = pick.Index;
          },
          .StepBack = [=] {
              if (navigation().Rule < 0)
                  return false;
              navigation().Rule = -1;
              return true;
          } },
        { .Lens = AnimationLens::Layers, .Action = "animation.lens.layers", .Label = "Layers",
          .Hint = "Inspect a layer for its weight and mask; pick a joint to mask it",
          .Card = [this] {
              return Card{ { .Title = LayerCard->Title(), .Sections = { { .Heading = {}, .Source = LayerCard.get() } } } };
          },
          // What won each layer, as many as the deck has lanes for.
          .Lanes = [=](std::vector<TimelineLane>& lanes) {
              const AnimBoundRig* rig = simulation().Rig();
              for (std::size_t layer = 0; rig != nullptr && layer < rig->Layers.size() && layer < kTimelineLaneLimit;
                   ++layer)
                  lanes.push_back(AnimationDecisionLane(simulation(), layer,
                                                        layer == navigation().Layer ? navigation().Rule : -1));
          },
          .StepBack = [this] { return LayerCard->StepBack(); } },
        { .Lens = AnimationLens::Flow, .Action = "animation.lens.flow", .Label = "Flow",
          .Hint = "The deck's lane shows the sections the layer played; Edit opens the flow as a sheet",
          .Card = [this] {
              return Card{ { .Title = FlowCard->Title(), .Sections = { { .Heading = {}, .Source = FlowCard.get() } } } };
          },
          .Lanes = [=](std::vector<TimelineLane>& lanes) {
              lanes.push_back(AnimationFlowLane(simulation(), navigation().Layer));
          },
          .Picked = onLayer },
        { .Lens = AnimationLens::Blends, .Action = "animation.lens.blends", .Label = "Blends",
          .Hint = "Drag the pad to move a blendspace; the deck's lane shows each blend as long as it ran",
          .Card = [this] {
              return Card{ { .Title = "Blends",
                             .Sections = { { .Heading = {}, .Source = BlendCard.get() } },
                             .Details = { { .Heading = "Overrides and A/B", .Source = &BlendCard->Comparison() } } } };
          },
          .Lanes = [=](std::vector<TimelineLane>& lanes) {
              lanes.push_back(AnimationBlendLane(simulation(), navigation().Layer));
          },
          .Picked = onLayer,
          .Pad = true },
        { .Lens = AnimationLens::RootMotion, .Action = "animation.lens.root_motion", .Label = "Root motion",
          .Hint = "Move the character to see how far its clips carry it; the deck plots carried against achieved",
          .Card = [this] {
              return Card{ { .Title = "Root motion", .Sections = { { .Heading = {}, .Source = RootMotionCard.get() } } } };
          },
          .Lanes = [=](std::vector<TimelineLane>& lanes) {
              for (TimelineLane& lane : AnimationTravelLanes(simulation()))
                  lanes.push_back(std::move(lane));
          } },
        { .Lens = AnimationLens::Facts, .Action = "animation.lens.facts", .Label = "Facts",
          .Hint = "Drive an input fact for the next tick; Plot puts a fact along the deck",
          .Card = [this] { return Card{ { .Title = "Facts", .Sections = { { .Heading = {}, .Source = FactCard.get() } } } }; },
          .Lanes = [this, simulation](std::vector<TimelineLane>& lanes) {
              if (!FactCard->Plotted().empty())
                  lanes.push_back(AnimationFactLane(simulation(), FactCard->Plotted()));
          } },
        { .Lens = AnimationLens::Requests, .Action = "animation.lens.requests", .Label = "Requests",
          .Hint = "Issue a request on the next tick, or cancel a live one; the deck's lane shows each as long as it lived",
          .Card = [this] {
              return Card{ { .Title = "Requests", .Sections = { { .Heading = {}, .Source = RequestCard.get() } } } };
          },
          .Lanes = [=](std::vector<TimelineLane>& lanes) { lanes.push_back(AnimationRequestLane(simulation())); },
          .Picked = onLayer },
        { .Lens = AnimationLens::Events, .Action = "animation.lens.events", .Label = "Events",
          .Hint = "E adds an event at the playhead",
          .Card = [this]() -> Card {
              const AnimationClipEventsDocument* document = AuditionedEvents();
              if (document == nullptr)
                  return std::nullopt;
              const AnimationClipEvent* event = PickedEvent ? document->Find(*PickedEvent) : nullptr;
              return WorkspaceSelectionFacet{
                  .Title = event == nullptr ? Workspace->ClipEvents.DocumentLabel(document->ClipPath())
                                            : EventLabel(*event),
                  .Actions = { "animation.event.add", "edit.delete", "file.save", std::string(kCloseAction) },
                  .Sections = { { .Heading = {}, .Source = EventCard.get() } },
                  .Details = { { .Heading = "Admissions", .Source = Admissions.get() } } };
          },
          .Marks = [this](std::vector<TimelineMark>& marks, double end) {
              if (const AnimationClipEventsDocument* document = AuditionedEvents())
                  marks = AnimationEventMarks(*document, end, PickedEvent);
          },
          .Document = [this]() -> std::optional<DocumentRef> {
              const AnimationClipEventsDocument* document = AuditionedEvents();
              return document != nullptr ? std::optional(Workspace->ClipEvents.RefOf(*document)) : std::nullopt;
          },
          .CanDelete = [this] { return AuditionedEvents() != nullptr && PickedEvent.has_value(); },
          .Delete = [this] {
              if (AnimationClipEventsDocument* document = AuditionedEvents(); document != nullptr && PickedEvent)
                  Workspace->ClipEvents.Remove(*document, *PickedEvent);
              PickedEvent.reset();
          },
          .StepBack = [this] {
              if (!PickedEvent)
                  return false;
              PickedEvent.reset();
              return true;
          } },
    };
}

const AnimationWorkspace::LensView& AnimationWorkspace::ViewOf(AnimationLens lens) const
{
    return *std::ranges::find(LensViews, lens, &LensView::Lens);
}

WorkspaceOperationFacet AnimationWorkspace::Operation()
{
    const LensView& shown = ViewOf(ShownLens);
    WorkspaceOperationFacet facet{ .Name = std::string(shown.Label), .Hint = std::string(shown.Hint) };
    for (const LensView& lens : LensViews)
        facet.Choices.push_back(
            { .Label = std::string(lens.Label), .Action = std::string(lens.Action), .Current = lens.Lens == ShownLens });
    return facet;
}

void AnimationWorkspace::DefineEventActions(ActionCatalog& catalog)
{
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
                      PickedEvent = Workspace->ClipEvents.Add(*document, std::move(event));
                      ShowLens(AnimationLens::Events);
                  },
                  .Enabled = [this] { return !Clock->IsSimulation() && AuditionedEvents() != nullptr; } });
    OfferAction(catalog, ActionHandlers, DeleteAction(),
                { .Invoke = [this] {
                      if (const LensView& lens = ViewOf(ShownLens); Subject == CardSubject::Lens && lens.Delete)
                          lens.Delete();
                  },
                  .Enabled = [this] {
                      const LensView& lens = ViewOf(ShownLens);
                      return Subject == CardSubject::Lens && lens.CanDelete && lens.CanDelete();
                  } });
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
    if (clip.empty() || !MeshClipSourceOf(clip).has_value())
        return;
    CancelRetime();
    Workspace->DocumentError.clear();
    (void)Workspace->ClipEvents.OpenOrFocus(clip, Workspace->DocumentError);
}

void AnimationWorkspace::CancelRetime()
{
    Workspace->ClipEvents.CancelDocumentEdits();
}

WorkspaceTimelineFacet AnimationWorkspace::Timeline()
{
    WorkspaceTimelineFacet facet = Clock->Facet();
    if (Clock->IsLab())
        return facet;
    const LensView& lens = ViewOf(ShownLens);
    if (lens.Lanes && Clock->IsSimulation())
        lens.Lanes(facet.Lanes);
    if (lens.Marks && !Clock->IsSimulation() && facet.End > 0.0)
        lens.Marks(facet.Marks, facet.End);
    return facet;
}

void AnimationWorkspace::PickOnTimeline(std::string_view key)
{
    if (const std::optional<AnimationLanePick> pick = ParseAnimationLaneKey(key))
    {
        if (!pick->Lens)
        {
            Clock->InspectLabTick(static_cast<AnimTick>(std::max(pick->Index, 0)));
            return;
        }
        if (const LensView& lens = ViewOf(*pick->Lens); lens.Picked)
            lens.Picked(*pick);
        ShowLens(*pick->Lens);
        return;
    }
    if (const std::optional<std::uint32_t> event = ParseAnimationEventMarkKey(key))
    {
        PickedEvent = event;
        ShowLens(AnimationLens::Events);
    }
}

void AnimationWorkspace::MoveMark(std::string_view key, double time, TimelineDrag drag)
{
    AnimationClipEventsDocument* document = AuditionedEvents();
    const double duration = Workspace->Audition.Session.Duration();
    const std::optional<std::uint32_t> event = ParseAnimationEventMarkKey(key);
    if (document == nullptr || duration <= 0.0 || !event)
        return;
    switch (drag)
    {
    case TimelineDrag::Begin:
        Workspace->Sources.CancelEdits();
        PickOnTimeline(key);
        break;
    case TimelineDrag::Move:
        if (document->EditingKey() != event)
            return;
        break;
    case TimelineDrag::End:
        if (document->EditingKey() != event)
            return;
        Workspace->ClipEvents.PreviewTime(*document, *event, time / duration);
        Workspace->ClipEvents.CommitEdit(*document);
        return;
    case TimelineDrag::Cancel:
        Workspace->ClipEvents.CancelEdit(*document);
        return;
    }
    Workspace->ClipEvents.PreviewTime(*document, *event, time / duration);
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
    PublishProblems();
    if (Subject == CardSubject::Document)
        Sheet->Update();
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
    if (!CanShowStage(ContentRect))
        return;
    const float width = ContentRect.Width();
    const float height = ContentRect.Height();
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
        Subject == CardSubject::Lens && ViewOf(ShownLens).Pad && Clock->IsSimulation()
            ? AnimationBlendspaceOn(Workspace->Rig.Simulation, Workspace->Rig.Navigation.Layer)
            : std::nullopt;
    if (Pad == nullptr || !space)
    {
        if (Pad != nullptr)
            Pad->Hide();
        ReleasePad(false);
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
        caption += " \xC3\x97 " + space->AxisFact[1];
    Pad->Show(region, marks, Vec2d(static_cast<float>(space->U), static_cast<float>(space->V)), std::move(caption),
              line);
}

void AnimationWorkspace::DragPadTo(Vec2d windowPoint)
{
    const Vec2d at = Pad->ValueAt(windowPoint);
    (void)PlaceAnimationBlendspacePoint(Workspace->Rig.Simulation, Workspace->Rig.Navigation.Layer, at.X, at.Y);
}

void AnimationWorkspace::ReleasePad(bool keep)
{
    if (!PadHeld)
        return;
    PadHeld = false;
    if (keep)
        Workspace->Rig.Scenario.CommitEdit();
    else
        Workspace->Rig.Scenario.CancelEdit();
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
    ReleasePad(false);
    Workspace->Sources.CancelEdits();
    Orbit.Cancel();
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

bool AnimationWorkspace::ClaimPlatformEvent(PlatformEventContext& ctx)
{
    return Subject == CardSubject::Document && Sheet->ClaimPlatformEvent(ctx.Event);
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
    const auto onStage = [&](float x, float y) { return ContentRect.Contains(Vec2d(x, y)); };
    switch (event.type)
    {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button != SDL_BUTTON_LEFT || !onStage(event.button.x, event.button.y))
            break;
        if (Pad != nullptr && Pad->Contains(Vec2d(event.button.x, event.button.y)))
        {
            // The whole drag is one edit to the scenario.
            Workspace->Rig.Scenario.BeginEdit();
            PadHeld = true;
            DragPadTo(Vec2d(event.button.x, event.button.y));
        }
        else
            Orbit.Press(Vec2d(event.button.x, event.button.y));
        ctx.Handled = true;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (PadHeld)
        {
            DragPadTo(Vec2d(event.motion.x, event.motion.y));
            ctx.Handled = true;
        }
        else if (const std::optional<StageOrbitDrag::Turn> turn =
                     Orbit.Move(Vec2d(event.motion.x, event.motion.y), Vec2d(event.motion.xrel, event.motion.yrel)))
        {
            Viewport->Orbit(turn->Yaw, turn->Pitch);
            ctx.Handled = true;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button != SDL_BUTTON_LEFT || (!PadHeld && !Orbit.IsHeld()))
            break;
        if (PadHeld)
            ReleasePad(true);
        else if (Orbit.Release() && ShowJoints)
            PickJointAt(Vec2d(event.button.x, event.button.y));
        ctx.Handled = true;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (onStage(event.wheel.mouse_x, event.wheel.mouse_y))
        {
            Viewport->Zoom(event.wheel.y);
            ctx.Handled = true;
        }
        break;
    default:
        break;
    }
}

std::optional<DocumentRef> AnimationWorkspace::EditedDocument() const
{
    if (Subject == CardSubject::Lens)
        if (const LensView& lens = ViewOf(ShownLens); lens.Document)
            return lens.Document();
    if (Subject == CardSubject::Document)
        return Sheet->ActiveRef();
    if (Subject == CardSubject::Stage && Workspace->Rig.Scenario.IsOpen())
        return Workspace->Rig.Scenario.Ref();
    return std::nullopt;
}

bool AnimationWorkspace::OwnsDocument(const DocumentRef& document) const
{
    return document.Source == &Workspace->ClipEvents || document.Source == &Workspace->Rig.Scenario
        || Sheet->Owns(document);
}

void AnimationWorkspace::RevealDocument(const DocumentRef& document)
{
    ShowDocument(Workspace->DialKeyOf(document));
}

WorkspaceDocumentFacet AnimationWorkspace::Document() const
{
    WorkspaceDocumentFacet facet;
    Workspace->AppendDialDocuments(facet);
    Sheet->AppendOpen(facet);
    if (const std::optional<DocumentRef> edited = EditedDocument())
    {
        const std::string key = Workspace->DialKeyOf(*edited);
        if (const auto open = std::ranges::find(facet.Open, key, &WorkspaceOpenDocument::Key); open != facet.Open.end())
        {
            facet.Label = open->Label;
            facet.Changed = open->Changed;
            facet.Active = key;
        }
    }
    return facet;
}

void AnimationWorkspace::ShowDocument(std::string_view key)
{
    if (key.starts_with(kEventsDialKey))
    {
        const std::string clip(key.substr(kEventsDialKey.size()));
        if (Workspace->Audition.ClipPath != clip)
            (void)Workspace->AuditionClip(clip);
        Workspace->DocumentError.clear();
        (void)Workspace->ClipEvents.OpenOrFocus(clip, Workspace->DocumentError);
        PickedEvent.reset();
        ShowLens(AnimationLens::Events);
        return;
    }
    if (key.starts_with(kScenarioDialKey))
    {
        ShowCard(CardSubject::Stage);
        return;
    }
    if (Sheet->Show(key))
        ShowCard(CardSubject::Document);
}

void AnimationWorkspace::ShowCard(CardSubject subject)
{
    Workspace->Rig.Navigation.Joint = -1;
    Subject = subject;
    Clock->ShowLab(Subject == CardSubject::Run && ShownRun == RunKind::Lab);
}

void AnimationWorkspace::ShowLens(AnimationLens lens)
{
    if (lens != ShownLens)
        CancelRetime();
    ShownLens = lens;
    ShowCard(CardSubject::Lens);
}

bool AnimationWorkspace::OpenAsset(const ProjectFile& file)
{
    if (file.Subtype != "animation.rig")
        return OpenDocument(file.AssetPath);
    return OpenRigAsking(*Workspace, Prompts, file.AssetPath, [this] { ShowCard(CardSubject::Stage); });
}

void AnimationWorkspace::OpenRig(const std::string& path)
{
    (void)OpenRigAsking(*Workspace, Prompts, path, [this] { ShowCard(CardSubject::Stage); });
}

bool AnimationWorkspace::OpenDocument(const std::string& path)
{
    if (!Sheet->Open(path))
        return false;
    ShowCard(CardSubject::Document);
    return true;
}

void AnimationWorkspace::CloseEditedDocument()
{
    const std::optional<DocumentRef> edited = EditedDocument();
    if (!edited)
        return;
    if (edited->Source != &Workspace->ClipEvents)
    {
        Sheet->CloseActive([this](const std::string&) {
            if (Workspace->Documents.Active() == nullptr && Subject == CardSubject::Document)
                ShowCard(CardSubject::Stage);
        });
        return;
    }
    std::vector<DocumentRef> changed;
    for (const DocumentRef& document : Sources.ChangedDocuments())
        if (document == *edited)
            changed.push_back(document);
    AskAboutDocuments(Prompts, "Close " + Workspace->ClipEvents.DocumentLabel(edited->Key), changed,
                      [this, clip = edited->Key](DirtyDisposition disposition) {
                          Workspace->DocumentError.clear();
                          if (!Workspace->ClipEvents.Close(clip, disposition, Workspace->DocumentError))
                              return;
                          PickedEvent.reset();
                          ShowCard(CardSubject::Stage);
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
        label = std::string(PathLeaf(Workspace->Rig.Path));
        if (Workspace->Rig.Simulation.IsOpen())
            label += " \xC2\xB7 " + Workspace->Rig.Simulation.Scenario().Name;
    }
    return { { .Role = "Rig", .Primary = true, .Explorer = "anim_rigs", .Active = { .Label = label } } };
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
    if (Subject == CardSubject::Lens)
        if (std::optional<WorkspaceSelectionFacet> card = ViewOf(ShownLens).Card())
            return *std::move(card);
    if (Subject == CardSubject::Run)
    {
        const RunView& run = RunOf(ShownRun);
        return { .Title = std::string(run.Title), .Sections = { { .Heading = {}, .Source = run.Source } }, .Primary = true };
    }
    if (Subject == CardSubject::NewRig)
        return { .Title = "New rig", .Sections = { { .Heading = {}, .Source = Creation.get() } } };
    if (Subject == CardSubject::NewDocument)
        return { .Title = "New animation document", .Sections = { { .Heading = {}, .Source = NewDocument.get() } } };
    if (const DataDocument* document = Workspace->Documents.Active(); Subject == CardSubject::Document && document)
        return { .Title = std::string(PathLeaf(document->VirtualPath())),
                 .Actions = { "file.save", std::string(kCloseAction) },
                 .Sections = Sheet->Sections() };

    const bool rigOpen = Workspace->Rig.Simulation.IsOpen() && !Workspace->Rig.Path.empty();
    WorkspaceSelectionFacet stage;
    stage.Title = rigOpen                              ? std::string(PathLeaf(Workspace->Rig.Path))
                  : !Workspace->Audition.MeshPath.empty() ? std::string(PathLeaf(Workspace->Audition.MeshPath))
                                                      : std::string("Stage");
    stage.Sections.push_back({ .Heading = "Audition", .Source = Audition.get() });
    if (rigOpen)
    {
        stage.Sections.push_back({ .Heading = "Scenario", .Source = Scenario.get() });
        stage.Actions = { "animation.scenario.save", "animation.scenario.revert" };
        if (!Undeclared.empty())
            stage.Actions.push_back(std::string(kDeclareAction));
        stage.Details.push_back({ .Heading = "Rig report", .Source = RigReport.get() });
    }
    return stage;
}

std::vector<WorkspaceExplorerInfo> AnimationWorkspace::Explorers() const
{
    return ExplorerRows.Infos();
}

ExplorerPage AnimationWorkspace::Explore(const ExplorerQuery& query)
{
    return ExplorerRows.Explore(query);
}

void AnimationWorkspace::ChooseInExplorer(std::string_view explorer, std::string_view key, ExplorerChoice choice,
                                          std::size_t mark)
{
    ExplorerRows.Choose(explorer, key, choice, mark);
}
