#pragma once

#include "authoring/AnimationJointPicking.h"

#include "actions/ActionContext.h"
#include "data/DataSubtypeAuthoring.h"
#include "prompts/PromptQueue.h"
#include "ui/WorkspaceView.h"
#include "workspaces/IWorkspace.h"
#include "workspaces/TreeOutline.h"

#include <math/geometry/2d/Rect2d.h>

#include <cstdint>
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
class AnimationRigCreation;
class AnimationRootMotionProperties;
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
class DataProperties;
class DataDocumentStore;
class DocumentSourceSet;
class Engine;
class Game;
class PlanePad;
class PromptQueue;
struct ProjectDescriptor;

// What the card, the drawer and the deck's lanes are about when nothing more
// particular is picked: one of the workspace's lenses.
enum class AnimationLens : std::uint8_t
{
    Rules,
    Layers,
    Flow,
    Blends,
    RootMotion,
    Facts,
    Events,
};

// The animation workspace: rigs, clips and their events authored against a
// live preview. The rig turns on a stage that fills the window, its bones and
// joints drawn over it; the card is the picked joint, or what is auditioned
// and the open rig's scenario. Content, rigs and the skeleton are explorers,
// and the rig is the cartridge. The game module is used only for its
// vocabulary; its game never runs here.
class AnimationWorkspace final : public IWorkspace
{
public:
    AnimationWorkspace(Engine& engine, const ProjectDescriptor& project, Game* module, DocumentSourceSet& sources,
                       DataDocumentStore& store, PromptQueue& prompts, ActionCatalog& catalog,
                       const DataSubtypeAuthoringSetup& authorings);
    ~AnimationWorkspace() override;

    AnimationWorkspace(const AnimationWorkspace&) = delete;
    AnimationWorkspace& operator=(const AnimationWorkspace&) = delete;

    void Tick(FrameUpdateContext& ctx) override;
    void SetVisible(bool visible) override;
    void HandlePlatformEvent(PlatformEventContext& ctx) override;
    [[nodiscard]] bool HoldsPointer() const override { return Drag != StageDrag::None; }
    void MoveTo(const WorkspaceWindow& window) override;
    void SetContentRect(const Rect2d& content) override { ContentRect = content; }
    [[nodiscard]] bool OwnsDocument(const DocumentRef& document) const override;
    void RevealDocument(const DocumentRef& document) override;
    [[nodiscard]] ActionContext* Actions() override { return &ActionHandlers; }
    [[nodiscard]] WorkspaceDocumentFacet Document() const override;
    [[nodiscard]] WorkspaceOperationFacet Operation() override;
    void ShowDocument(std::string_view key) override;
    [[nodiscard]] WorkspaceViewFacet Viewing() const override;
    [[nodiscard]] std::vector<WorkspaceResourceFacet> Resources() override;
    [[nodiscard]] WorkspaceSelectionFacet Selection() override;
    [[nodiscard]] WorkspaceTimelineFacet Timeline() override;
    void Scrub(double time, TimelineDrag drag) override;
    void MoveMark(std::string_view key, double time, TimelineDrag drag) override;
    void PickOnTimeline(std::string_view key) override;
    [[nodiscard]] std::vector<WorkspaceExplorerInfo> Explorers() const override;
    [[nodiscard]] ExplorerPage Explore(const ExplorerQuery& query) override;
    void ChooseInExplorer(std::string_view explorer, std::string_view key, ExplorerChoice choice,
                          std::size_t mark) override;
    WorkspaceView& View() override { return Surface; }

private:
    // A press on the stage orbits once it moves; let go where it was pressed,
    // it picks the joint under it. A press on the pad drags its point.
    enum class StageDrag : std::uint8_t
    {
        None,
        Pressed,
        Orbiting,
        Pad,
    };
    // What the card is about when no joint is picked.
    enum class CardSubject : std::uint8_t
    {
        Stage,
        NewRig,
        Document,
        NewDocument,
        Lens,
    };

    void RegisterCommands();
    void DefineActions(ActionCatalog& catalog);
    // Pausing the preview and dropping an open edit: what a focus loss and a
    // tab sent to the background both mean.
    void Interrupt();
    // Per frame it is seen: the picture placed, the strokes and joints over it.
    void ShowStage();
    void PickJointAt(Vec2d windowPoint);
    void ShowPad();
    void DragPadTo(Vec2d windowPoint);
    // The card about `subject`, with no joint picked.
    void ShowCard(CardSubject subject);
    void ShowLens(AnimationLens lens);
    [[nodiscard]] bool ShowingLens(AnimationLens lens) const { return Subject == CardSubject::Lens && ShownLens == lens; }
    void DefineLenses(ActionCatalog& catalog);
    void OpenDocument(const std::string& path);
    void DefineTransport(ActionCatalog& catalog);
    void DefineEventActions(ActionCatalog& catalog);
    // The auditioned clip's events open as its clip changes.
    void FollowAuditionedClip();
    [[nodiscard]] AnimationClipEventsDocument* AuditionedEvents() const;
    void CloseActiveDocument();

    Engine& EngineRef;
    DataDocumentStore& Store;
    std::unique_ptr<AnimationPreviewWorkspace> Workspace;
    AnimationPreviewRenderFeature* Viewport = nullptr;
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
    std::unique_ptr<AnimationTimeline> Clock;
    std::unique_ptr<AnimationEventProperties> EventCard;
    std::optional<std::uint32_t> PickedEvent;
    std::string FollowedClip;
    std::unique_ptr<AnimationAuditionProperties> Audition;
    std::unique_ptr<AnimationScenarioProperties> Scenario;
    std::unique_ptr<AnimationRigCreation> Creation;
    // What animation subtypes add to their documents' sheets.
    DataSubtypeAuthoringRegistry Authoring;
    std::unique_ptr<DataProperties> DocumentSheet;
    std::unique_ptr<DataCreation> NewDocument;
    PromptQueue::Asker Prompts;
    CardSubject Subject = CardSubject::Stage;
    AnimationLens ShownLens = AnimationLens::Rules;
    TreeOutline ContentOutline;
    TreeOutline RigOutline;
    TreeOutline SkeletonOutline;
    ActionContext ActionHandlers;
    std::string FramedSubject;
    Rect2d ContentRect{};
    bool Visible = false;
    bool ShowJoints = true;
    bool ShowPath = true;
    StageDrag Drag = StageDrag::None;
    Vec2d PressedAt{};
    // The joints where they were drawn last, for picking.
    std::vector<AnimationJointMarker> Joints;

    // Declared last: its panels reference everything above.
    WorkspaceView Surface;
};
