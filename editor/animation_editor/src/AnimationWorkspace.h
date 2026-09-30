#pragma once

#include "ui/WorkspaceView.h"
#include "workspaces/IWorkspace.h"

#include <memory>
#include <string>

class AnimationPreviewRenderFeature;
class AnimationPreviewWorkspace;
class DataDocumentStore;
class DocumentSourceSet;
class Engine;
class Game;
struct ProjectDescriptor;

// The animation workspace: rigs, clips and their events authored against a
// live preview. The game module is used only for its vocabulary; its game
// never runs here.
class AnimationWorkspace final : public IWorkspace
{
public:
    AnimationWorkspace(Engine& engine, const ProjectDescriptor& project, Game* module,
                       DocumentSourceSet& sources, DataDocumentStore& store);
    ~AnimationWorkspace() override;

    AnimationWorkspace(const AnimationWorkspace&) = delete;
    AnimationWorkspace& operator=(const AnimationWorkspace&) = delete;

    void Tick(FrameUpdateContext& ctx) override;
    void SetVisible(bool visible) override;
    void HandlePlatformEvent(PlatformEventContext& ctx) override;
    [[nodiscard]] bool OwnsDocument(const DocumentRef& document) const override;
    void RevealDocument(const DocumentRef& document) override;
    WorkspaceView& View() override { return Surface; }

private:
    void RegisterCommands();
    // Pausing the preview and dropping an open edit: what a focus loss and a
    // tab sent to the background both mean.
    void Interrupt();

    Engine& EngineRef;
    DataDocumentStore& Store;
    std::unique_ptr<AnimationPreviewWorkspace> Workspace;
    AnimationPreviewRenderFeature* Viewport = nullptr;
    std::string FramedMesh;
    bool Visible = false;

    // Declared last: its panels reference everything above.
    WorkspaceView Surface;
};
