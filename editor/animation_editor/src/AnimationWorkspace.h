#pragma once

#include "authoring/AnimationJointPicking.h"
#include "authoring/AnimationLens.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "data/DataSubtypeAuthoring.h"
#include "presenters/StageOrbit.h"
#include "prompts/PromptQueue.h"
#include "workspaces/ExplorerTable.h"
#include "workspaces/IWorkspace.h"
#include "workspaces/TreeOutline.h"

#include <math/geometry/2d/Rect2d.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class ActionCatalog;
class AnimationAuditionProperties;
class AnimationBoneProperties;
class AnimationClipEventsDocument;
class AnimationEventProperties;
class AnimationPreviewRenderFeature;
class AnimationRequestProperties;
class AnimationLabProperties;
class AnimationMigrationProperties;
class AnimationRigCreation;
class AnimationScenarioBatchProperties;
class AnimationTraceProperties;
class AnimationRigReportProperties;
class AnimationRootMotionProperties;
class AnimationAdmissionProperties;
class AnimationBlendProperties;
class AnimationFactProperties;
class AnimationFlowProperties;
class AnimationLayerProperties;
class AnimationRuleProperties;
class AnimationScenarioProperties;
class AnimationTimeline;
class AnimationPreviewWorkspace;
class ContentPicture;
class DataCreation;
class DataDocumentSheet;
class DataDocumentStore;
class DocumentSourceSet;
class Engine;
class Game;
class PlanePad;
class ProblemList;
class ProjectFileSteps;
class PromptQueue;
struct AnimationLanePick;
struct Problem;
struct ProjectDescriptor;

// A new rig, its parts asked for first.
[[nodiscard]] ActionDefinition NewRigAction();

// The animation workspace (docs/editor/animation-workspace.md): rigs, clips and
// their events authored against a live preview on a stage that fills the
// window. The game module is used only for its vocabulary; its game never runs.
class AnimationWorkspace final : public IWorkspace
{
public:
    AnimationWorkspace(Engine& engine, const ProjectDescriptor& project, Game* module, DocumentSourceSet& sources,
                       DataDocumentStore& store, ProjectFileSteps* files, PromptQueue& prompts, ActionCatalog& catalog,
                       ProblemList& problems, const DataSubtypeAuthoringSetup& authorings);
    ~AnimationWorkspace() override;

    AnimationWorkspace(const AnimationWorkspace&) = delete;
    AnimationWorkspace& operator=(const AnimationWorkspace&) = delete;

    void Tick(FrameUpdateContext& ctx) override;
    void SetVisible(bool visible) override;
    [[nodiscard]] bool ClaimPlatformEvent(PlatformEventContext& ctx) override;
    void HandlePlatformEvent(PlatformEventContext& ctx) override;
    void MoveTo(const WorkspaceWindow& window) override;
    void SetContentRect(const Rect2d& content) override { ContentRect = content; }
    [[nodiscard]] bool OwnsDocument(const DocumentRef& document) const override;
    void RevealDocument(const DocumentRef& document) override;
    [[nodiscard]] ActionContext* Actions() override { return &ActionHandlers; }
    [[nodiscard]] WorkspaceDocumentFacet Document() const override;
    [[nodiscard]] WorkspaceOperationFacet Operation() override;
    void ShowDocument(std::string_view key) override;
    bool OpenAsset(const ProjectFile& file) override;
    [[nodiscard]] WorkspaceViewFacet Viewing() const override;
    [[nodiscard]] std::vector<WorkspaceResourceFacet> Resources() override;
    [[nodiscard]] WorkspaceSelectionFacet Selection() override;
    [[nodiscard]] WorkspaceTimelineFacet Timeline() override;
    bool RevealProblem(const Problem& problem) override;
    void Scrub(double time, TimelineDrag drag) override;
    void MoveMark(std::string_view key, double time, TimelineDrag drag) override;
    void PickOnTimeline(std::string_view key) override;
    [[nodiscard]] std::vector<WorkspaceExplorerInfo> Explorers() const override;
    [[nodiscard]] ExplorerPage Explore(const ExplorerQuery& query) override;
    void ChooseInExplorer(std::string_view explorer, std::string_view key, ExplorerChoice choice,
                          std::size_t mark) override;

private:
    // What the card is about when no joint is picked.
    enum class CardSubject : std::uint8_t
    {
        Stage,
        NewRig,
        Document,
        NewDocument,
        Lens,
        Run,
    };
    // A run the workspace does over the project or the rig, shown as a sheet.
    enum class RunKind : std::uint8_t
    {
        Lab,
        Batch,
        Migration,
        Trace,
    };
    // What a lens shows: its card when it is up (none falls back to the
    // stage's), the deck's lanes and marks, what a pick on its lane names, what
    // Save and Delete act on, and a step back within its card before Escape.
    struct LensView
    {
        AnimationLens Lens;
        std::string_view Action;
        std::string_view Label;
        std::string_view Hint;
        std::function<std::optional<WorkspaceSelectionFacet>()> Card;
        std::function<void(std::vector<TimelineLane>&)> Lanes = {};
        std::function<void(std::vector<TimelineMark>&, double end)> Marks = {};
        std::function<void(const AnimationLanePick&)> Picked = {};
        std::function<std::optional<DocumentRef>()> Document = {};
        std::function<bool()> CanDelete = {};
        std::function<void()> Delete = {};
        std::function<bool()> StepBack = {};
        bool Pad = false;
    };
    struct RunView
    {
        RunKind Kind;
        std::string_view Action;
        std::string_view Label;
        std::string_view Title;
        IPropertySource* Source = nullptr;
        std::function<void()> Opened = {};
    };

    void RegisterCommands();
    void DefineActions(ActionCatalog& catalog);
    void DefineDocumentActions(ActionCatalog& catalog);
    void DefineExplorers(ActionCatalog& catalog);
    void DefineTransport(ActionCatalog& catalog);
    void DefineEventActions(ActionCatalog& catalog);
    void DefineLenses(ActionCatalog& catalog);
    void DefineLensViews();
    void DefineRuns(ActionCatalog& catalog);
    // Pausing the preview and dropping an open edit: what a focus loss and a
    // tab sent to the background both mean.
    void Interrupt();
    // Per frame it is seen: the picture placed, the strokes and joints over it.
    void ShowStage();
    void PickJointAt(Vec2d windowPoint);
    void ShowPad();
    void DragPadTo(Vec2d windowPoint);
    void ReleasePad(bool keep);
    // The card about `subject`, with no joint picked.
    void ShowCard(CardSubject subject);
    void ShowLens(AnimationLens lens);
    void ShowRun(RunKind kind);
    [[nodiscard]] const LensView& ViewOf(AnimationLens lens) const;
    [[nodiscard]] const RunView& RunOf(RunKind kind) const;
    // A retime on the deck ends with nothing kept when its clock or lens goes.
    void CancelRetime();
    bool OpenDocument(const std::string& path);
    void OpenRig(const std::string& path);
    // The document Save, Close and the dial's current mark are about: the
    // lens's, the card's data document, or the open rig's scenario.
    [[nodiscard]] std::optional<DocumentRef> EditedDocument() const;
    void CloseEditedDocument();
    // The auditioned clip's events open as its clip changes.
    void FollowAuditionedClip();
    [[nodiscard]] AnimationClipEventsDocument* AuditionedEvents() const;
    // The rig's and the scenario's problems, republished when they change.
    void PublishProblems();

    Engine& EngineRef;
    DocumentSourceSet& Sources;
    DataDocumentStore& Store;
    std::unique_ptr<AnimationPreviewWorkspace> Workspace;
    AnimationPreviewRenderFeature* Viewport = nullptr;
    std::string SetupProblem;
    std::unique_ptr<ContentPicture> Picture;
    // The blendspace the selected layer plays, over the stage while Blends is the lens.
    std::unique_ptr<PlanePad> Pad;
    std::unique_ptr<AnimationBoneProperties> Bone;
    std::unique_ptr<AnimationRuleProperties> RuleCard;
    std::unique_ptr<AnimationLayerProperties> LayerCard;
    std::unique_ptr<AnimationFlowProperties> FlowCard;
    std::unique_ptr<AnimationBlendProperties> BlendCard;
    std::unique_ptr<AnimationRootMotionProperties> RootMotionCard;
    std::unique_ptr<AnimationFactProperties> FactCard;
    std::unique_ptr<AnimationRequestProperties> RequestCard;
    std::unique_ptr<AnimationAdmissionProperties> Admissions;
    std::unique_ptr<AnimationRigReportProperties> RigReport;
    std::unique_ptr<AnimationScenarioBatchProperties> Batch;
    std::unique_ptr<AnimationMigrationProperties> Migration;
    std::unique_ptr<AnimationTraceProperties> Trace;
    std::unique_ptr<AnimationLabProperties> LabSheet;
    RunKind ShownRun = RunKind::Batch;
    std::vector<RunView> Runs;
    ProblemList& Problems;
    std::string ProblemSignature;
    // Names the rig's content uses that nothing declares, as of ProblemSignature.
    std::vector<std::string> Undeclared;
    std::string DeclareError;
    std::unique_ptr<AnimationTimeline> Clock;
    std::unique_ptr<AnimationEventProperties> EventCard;
    std::optional<std::uint32_t> PickedEvent;
    std::string FollowedClip;
    std::unique_ptr<AnimationAuditionProperties> Audition;
    std::unique_ptr<AnimationScenarioProperties> Scenario;
    std::unique_ptr<AnimationRigCreation> Creation;
    std::unique_ptr<DataDocumentSheet> Sheet;
    std::unique_ptr<DataCreation> NewDocument;
    PromptQueue::Asker Prompts;
    CardSubject Subject = CardSubject::Stage;
    AnimationLens ShownLens = AnimationLens::Rules;
    std::vector<LensView> LensViews;
    TreeOutline ContentOutline;
    TreeOutline RigOutline;
    TreeOutline SkeletonOutline;
    ActionContext ActionHandlers;
    ExplorerTable ExplorerRows;
    std::string FramedSubject;
    Rect2d ContentRect{};
    bool Visible = false;
    bool ShowJoints = true;
    bool ShowPath = true;
    StageOrbitDrag Orbit;
    bool PadHeld = false;
    // The joints where they were drawn last, for picking.
    std::vector<AnimationJointMarker> Joints;
};
