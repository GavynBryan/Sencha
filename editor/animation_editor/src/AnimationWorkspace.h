#pragma once

#include "authoring/AnimationJointPicking.h"

#include "actions/ActionContext.h"
#include "ui/WorkspaceView.h"
#include "workspaces/IWorkspace.h"

#include <math/geometry/2d/Rect2d.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class ActionCatalog;
class AnimationBoneProperties;
class AnimationPreviewRenderFeature;
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
// joints drawn over it and a picked joint on the card. The game module is used
// only for its vocabulary; its game never runs here.
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
    [[nodiscard]] WorkspaceViewFacet Viewing() const override;
    [[nodiscard]] WorkspaceSelectionFacet Selection() override;
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
