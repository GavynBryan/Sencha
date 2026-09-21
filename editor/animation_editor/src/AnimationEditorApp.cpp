#include "AnimationEditorApp.h"

#include "AnimationPreviewWorkspace.h"
#include "render/AnimationPreviewRenderFeature.h"
#include "ui/AnimationPreviewPanels.h"

#include "project/Project.h"
#include "project/ProjectContentMount.h"
#include "ui/EditorThemeStartup.h"
#include "ui/EditorUiFeature.h"

#include <app/Engine.h>
#include <app/EngineSchedule.h>
#include <assets/runtime/RuntimeAssets.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <platform/PlatformServices.h>
#include <platform/SdlWindow.h>

#include <SDL3/SDL.h>

#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace
{
constexpr std::array<std::string_view, 1> PreviewDependencies{ "animation_editor_ui" };

class AnimationPreviewFrame
{
public:
    explicit AnimationPreviewFrame(std::function<void(double)> frame) : Frame(std::move(frame)) {}
    void FrameUpdate(FrameUpdateContext& context) { Frame(context.WallDeltaSeconds); }
private:
    std::function<void(double)> Frame;
};
}

// The host owns the asset stack longer than the session and render features
// borrowing it. No game module is activated in this content audition process.
class AnimationEditorHost
{
public:
    AnimationEditorHost(Engine& engine, SdlWindow& window,
                        const std::optional<std::string>& projectPath,
                        const std::optional<std::string>& meshPath,
                        const std::optional<std::string>& clipPath)
        : EngineRef(engine)
    {
        auto& graphics = engine.Graphics();
        Assets = std::make_unique<RuntimeAssets>(engine.Logging(), graphics.Buffers,
            graphics.Images, graphics.Descriptors, graphics.Samplers, engine.SceneSerializers());
        std::string error;
        if (projectPath)
        {
            ProjectDescriptor project;
            if (ProjectDescriptor::Load(*projectPath, project, &error))
                MountProjectContent(project, *Assets, engine.Logging(), &engine.Jobs());
        }
        else
            error = "Pass --project <path.senchaproj> to mount preview content.";
        Workspace = std::make_unique<AnimationPreviewWorkspace>(*Assets);
        Workspace->Error = std::move(error);
        if (Workspace->Error.empty())
        {
            const bool meshReady = !meshPath || Workspace->SelectMesh(*meshPath);
            if (meshReady && clipPath) Workspace->SelectClip(*clipPath);
        }
        ApplyEditorThemeFromConsole(engine.Console(), "Animation Editor");
        auto& renderer = graphics.MainRenderer;
        auto ui = std::make_unique<EditorUiFeature>(engine, window, graphics.Instance,
            graphics.Frames, "animation_editor.imgui.ini");
        Ui = renderer.StageFeature(std::move(ui), { .Id = "animation_editor_ui" });
        Viewport = renderer.StageFeature(
            std::make_unique<AnimationPreviewRenderFeature>(*Assets, Workspace->Scene),
            { .Id = "animation_editor_preview", .DependsOn = PreviewDependencies });
        if (Ui)
            AddAnimationPreviewPanels(*Ui, *Workspace, Viewport);
        std::vector<std::string_view> failed;
        if (!renderer.CommitStagedFeatures(&failed))
        {
            Ui = nullptr;
            Viewport = nullptr;
            std::fprintf(stderr, "Animation editor feature registration failed.\n");
        }
        if (std::find(failed.begin(), failed.end(), "animation_editor_ui") != failed.end()) Ui = nullptr;
        if (std::find(failed.begin(), failed.end(), "animation_editor_preview") != failed.end()) Viewport = nullptr;
    }

    ~AnimationEditorHost()
    {
        // A refused removal means a feature still borrows our state. Fail
        // closed rather than let teardown continue with dangling references.
        auto& renderer = EngineRef.Graphics().MainRenderer;
        if ((Viewport && !renderer.RemoveFeature(Viewport)) || (Ui && !renderer.RemoveFeature(Ui)))
        {
            std::fprintf(stderr, "Animation editor render features could not be detached.\n");
            std::abort();
        }
        Workspace.reset();
        Assets.reset();
    }

    void Frame(double seconds)
    {
        Workspace->Frame(seconds);
        if (Viewport && FramedMesh != Workspace->MeshPath)
        {
            Viewport->FrameSubject();
            FramedMesh = Workspace->MeshPath;
        }
    }

    void Event(PlatformEventContext& context)
    {
        if (context.Event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
            Workspace->Session.Pause();
        if (Ui) Ui->ProcessSdlEvent(context.Event);
    }

private:
    Engine& EngineRef;
    std::unique_ptr<RuntimeAssets> Assets;
    std::unique_ptr<AnimationPreviewWorkspace> Workspace;
    EditorUiFeature* Ui = nullptr;
    AnimationPreviewRenderFeature* Viewport = nullptr;
    std::string FramedMesh;
};

AnimationEditorApp::AnimationEditorApp(std::optional<std::string> projectPath,
                                     std::optional<std::string> meshPath,
                                     std::optional<std::string> clipPath)
    : ProjectPath(std::move(projectPath)), MeshPath(std::move(meshPath)), ClipPath(std::move(clipPath))
{
}

AnimationEditorApp::~AnimationEditorApp() = default;

void AnimationEditorApp::OnConfigure(GameConfigureContext& context)
{
    context.Config.Window.Title = "Animation Editor";
    context.Config.Window.ClientDecorations = true;
    context.Config.Console.UiEnabled = false;
    context.Config.Runtime.ApplicationShell = false;
    context.Config.Runtime.ContentRoots.clear();
}

void AnimationEditorApp::OnStart(GameStartupContext&)
{
    auto& engine = GetEngine();
    if (auto* window = engine.Platform().Windows.GetPrimaryWindow())
        Host = std::make_unique<AnimationEditorHost>(engine, *window, ProjectPath, MeshPath, ClipPath);
}

void AnimationEditorApp::OnRegisterSystems(SystemRegisterContext& context)
{
    if (Host)
        context.Schedule.Register<AnimationPreviewFrame>([this](double seconds) { Host->Frame(seconds); });
}

void AnimationEditorApp::OnPlatformEvent(PlatformEventContext& context)
{
    if (Host) Host->Event(context);
}

void AnimationEditorApp::OnShutdown(GameShutdownContext&)
{
    if (auto* graphics = GetEngine().TryGraphics()) graphics->WaitIdle();
    Host.reset();
}
