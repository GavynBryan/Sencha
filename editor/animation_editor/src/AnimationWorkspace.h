#pragma once

#include "authoring/AnimationJointPicking.h"

#include "actions/ActionContext.h"
#include "ui/WorkspaceView.h"
#include "workspaces/IWorkspace.h"
#include "workspaces/TreeOutline.h"

#include <math/geometry/2d/Rect2d.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class ActionCatalog;
class AnimationAuditionProperties;
class AnimationBoneProperties;
class AnimationPreviewRenderFeature;
class AnimationRigCreation;
class AnimationScenarioProperties;
class AnimationPreviewWorkspace;
class ContentPicture;
class DataDocumentStore;
class DocumentSourceSet;
class Engine;
class Game;
class PromptQueue;
struct ProjectDescriptor;

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
                       DataDocumentStore& store, PromptQueue& prompts, ActionCatalog& catalog);
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
    void ShowDocument(std::string_view key) override;
    [[nodiscard]] WorkspaceViewFacet Viewing() const override;
    [[nodiscard]] std::vector<WorkspaceResourceFacet> Resources() override;
    [[nodiscard]] WorkspaceSelectionFacet Selection() override;
    [[nodiscard]] std::vector<WorkspaceExplorerInfo> Explorers() const override;
    [[nodiscard]] ExplorerPage Explore(const ExplorerQuery& query) override;
    void ChooseInExplorer(std::string_view explorer, std::string_view key, ExplorerChoice choice,
                          std::size_t mark) override;
    WorkspaceView& View() override { return Surface; }

private:
    // A press on the stage orbits once it moves; let go where it was pressed,
    // it picks the joint under it.
    enum class StageDrag : std::uint8_t
    {
        None,
        Pressed,
        Orbiting,
    };
    // What the card is about when no joint is picked.
    enum class CardSubject : std::uint8_t
    {
        Stage,
        NewRig,
    };

    void RegisterCommands();
    void DefineActions(ActionCatalog& catalog);
    // Pausing the preview and dropping an open edit: what a focus loss and a
    // tab sent to the background both mean.
    void Interrupt();
    // Per frame it is seen: the picture placed, the strokes and joints over it.
    void ShowStage();
    void PickJointAt(Vec2d windowPoint);

    Engine& EngineRef;
    DataDocumentStore& Store;
    std::unique_ptr<AnimationPreviewWorkspace> Workspace;
    AnimationPreviewRenderFeature* Viewport = nullptr;
    std::unique_ptr<ContentPicture> Picture;
    std::unique_ptr<AnimationBoneProperties> Bone;
    std::unique_ptr<AnimationAuditionProperties> Audition;
    std::unique_ptr<AnimationScenarioProperties> Scenario;
    std::unique_ptr<AnimationRigCreation> Creation;
    CardSubject Subject = CardSubject::Stage;
    TreeOutline ContentOutline;
    TreeOutline RigOutline;
    TreeOutline SkeletonOutline;
    ActionContext ActionHandlers;
    std::string FramedMesh;
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
