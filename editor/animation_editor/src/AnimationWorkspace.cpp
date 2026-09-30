#include "AnimationWorkspace.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "render/AnimationPreviewRenderFeature.h"
#include "ui/AnimationPreviewPanels.h"

#include "data/DataDocumentStore.h"
#include "documents/DocumentSourceSet.h"
#include "project/Project.h"
#include "render/RenderFeatureDetach.h"

#include <app/Engine.h>
#include <app/Game.h>
#include <app/GameContexts.h>
#include <app/RuntimeContent.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/console/ConsoleTypes.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/Renderer.h>

#include <SDL3/SDL.h>

#include <array>
#include <cstdio>
#include <functional>
#include <span>
#include <vector>

namespace
{
// The preview's teardown releases ImGui texture bindings through the backend
// the window's UI feature owns.
constexpr std::array<std::string_view, 1> kPreviewDependsOn{ "editor_ui" };
constexpr std::string_view kCommandOwner = "editor.animation";
}

AnimationWorkspace::AnimationWorkspace(Engine& engine, const ProjectDescriptor& project, Game* module,
                                       DocumentSourceSet& sources, DataDocumentStore& store)
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

    Surface.File.Save = [this, &sources] {
        if (const DataDocument* active = Workspace->Documents.Active())
            (void)sources.Save(Workspace->Documents.RefOf(*active));
    };
    Surface.Status = [this] {
        const DataDocument* active = Workspace->Documents.Active();
        if (active == nullptr)
            return std::string{};
        return active->VirtualPath() + (active->IsDirty() ? " *" : "");
    };
    AddAnimationPreviewPanels(Surface, *Workspace, Viewport);
    RegisterCommands();
}

AnimationWorkspace::~AnimationWorkspace()
{
    EngineRef.Console().Registry().UnregisterOwner(kCommandOwner);
    Workspace->Sources.CancelEdits();
    // The preview borrows the scene the workspace extracts into.
    DetachRenderFeature(EngineRef, Viewport);
    Surface = WorkspaceView{};
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
}

void AnimationWorkspace::Interrupt()
{
    Workspace->Audition.Session.Pause();
    Workspace->Sources.CancelEdits();
}

void AnimationWorkspace::SetVisible(bool visible)
{
    Visible = visible;
    if (!visible)
        Interrupt();
}

void AnimationWorkspace::HandlePlatformEvent(PlatformEventContext& ctx)
{
    if (ctx.Event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
        Interrupt();
    else if (ctx.Event.type == SDL_EVENT_KEY_DOWN && ctx.Event.key.key == SDLK_ESCAPE)
        Workspace->Sources.CancelEdits();
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
