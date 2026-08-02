#include "DataEditorServices.h"

#include "DataEditorPanels.h"

#include "project/ProjectContentMount.h"
#include "ui/EditorThemeStartup.h"
#include "ui/EditorUiFeature.h"

#include <SDL3/SDL.h>

#include <app/Engine.h>
#include <app/EngineSchedule.h>
#include <app/Game.h>
#include <app/GameDataAssets.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/Renderer.h>
#include <platform/SdlWindow.h>

#include <cstdio>
#include <functional>
#include <memory>
#include <utility>

namespace
{
    class FrameHook
    {
    public:
        explicit FrameHook(std::function<void()> fn)
            : Fn(std::move(fn))
        {
        }

        void FrameUpdate(FrameUpdateContext&)
        {
            if (Fn)
                Fn();
        }

    private:
        std::function<void()> Fn;
    };
}

DataEditorServices::DataEditorServices(Engine& engine,
                                       SdlWindow& window,
                                       const EngineConfig&,
                                       std::optional<std::string> projectPath,
                                       std::optional<std::string> initialAsset)
    : EnginePtr(&engine)
    , Window(&window)
    , ProjectPath(std::move(projectPath))
    , InitialAsset(std::move(initialAsset))
{
    LoadProject();
    InitAssets();
    BuildUi();

    if (Workspace && InitialAsset)
    {
        std::string error;
        if (!Workspace->Open(*InitialAsset, &error))
        {
            std::fprintf(stderr, "[data_editor] failed to open '%s': %s\n",
                         InitialAsset->c_str(), error.c_str());
        }
    }
    UpdateTitle();
}

DataEditorServices::~DataEditorServices()
{
    Workspace.reset();

    if (ProjectModule.IsValid() && Assets)
    {
        UnregisterGameDataAssets(*ProjectModule.Instance, *Assets);
        ModuleLoader.Unload(ProjectModule);
    }
    Assets.reset();
}

void DataEditorServices::LoadProject()
{
    if (!ProjectPath)
    {
        std::fprintf(stderr,
            "[data_editor] no project: pass --project <path.senchaproj> or set SENCHA_PROJECT\n");
        return;
    }

    ProjectDescriptor descriptor;
    std::string error;
    if (!ProjectDescriptor::Load(*ProjectPath, descriptor, &error))
    {
        std::fprintf(stderr, "[data_editor] failed to open project '%s': %s\n",
                     ProjectPath->c_str(), error.c_str());
        return;
    }
    Project = std::move(descriptor);
}

void DataEditorServices::InitAssets()
{
    Engine& engine = *EnginePtr;
    GraphicsServices& graphics = engine.Graphics();
    Assets.emplace(engine.Logging(), graphics.Buffers, graphics.Images,
                   graphics.Descriptors, graphics.Samplers);

    if (!Project)
        return;

    if (!Project->GameModulePath.empty())
    {
        std::string error;
        ProjectModule = ModuleLoader.Load(Project->GameModulePath, &error);
        if (!ProjectModule.IsValid())
        {
            std::fprintf(stderr, "[data_editor] failed to load project module '%s': %s\n",
                         Project->GameModulePath.c_str(), error.c_str());
        }
        else
        {
            ProjectModule.Instance->AttachEngine(engine);
            RegisterGameDataAssets(*ProjectModule.Instance, *Assets);
        }
    }

    MountProjectContent(*Project, *Assets, engine.Logging(), &engine.Jobs());
    Workspace = std::make_unique<DataEditorWorkspace>(*Assets, *Project);
}

void DataEditorServices::BuildUi()
{
    Engine& engine = *EnginePtr;
    ApplyEditorThemeFromConsole(engine.Console(), "Data Editor");

    auto ui = std::make_unique<EditorUiFeature>(
        engine, *Window, engine.Graphics().Instance, engine.Graphics().Frames,
        "data_editor.imgui.ini",
        DockLayoutRatios{
            .Bottom = 0.18f,
            .Left = 0.23f,
            .Right = 0.26f,
            .CenterBottom = 0.30f,
        });
    UiFeature = ui.get();

    if (Workspace)
    {
        UiFeature->SetUndoActions(
            [this]() { if (DataDocument* document = Workspace->Active()) document->Undo(); },
            [this]() { if (DataDocument* document = Workspace->Active()) document->Redo(); },
            [this]() { const DataDocument* document = Workspace->Active(); return document && document->CanUndo(); },
            [this]() { const DataDocument* document = Workspace->Active(); return document && document->CanRedo(); });
        UiFeature->SetFileActions(
            {},
            {},
            [this]()
            {
                std::string error;
                if (!Workspace->SaveActive(&error) && !error.empty())
                    std::fprintf(stderr, "[data_editor] save failed: %s\n", error.c_str());
            },
            {});
        UiFeature->SetSaveAllAction([this]() { Workspace->SaveAll(); });

        UiFeature->AddPanel(std::make_unique<DataAssetBrowserPanel>(*Workspace));
        UiFeature->AddPanel(std::make_unique<DataFormPanel>(*Workspace));
        UiFeature->AddPanel(std::make_unique<DataDocumentationPanel>(*Workspace));
        UiFeature->AddPanel(std::make_unique<DataValidationPanel>(*Workspace));
        UiFeature->AddPanel(std::make_unique<DataRawJsonPanel>(*Workspace));
    }

    engine.Graphics().MainRenderer.AddFeature(std::move(ui));
}

void DataEditorServices::RegisterSystems(EngineSchedule& schedule)
{
    schedule.Register<FrameHook>([this] { ProcessFrame(); });
}

void DataEditorServices::HandlePlatformEvent(PlatformEventContext& ctx)
{
    if (UiFeature)
        UiFeature->ProcessSdlEvent(ctx.Event);
}

void DataEditorServices::ProcessFrame()
{
    UpdateTitle();
}

void DataEditorServices::UpdateTitle()
{
    std::string title = "Data Editor";
    if (Project)
        title += " - " + Project->Name;
    if (Workspace)
    {
        if (const DataDocument* document = Workspace->Active())
        {
            title += " - " + document->VirtualPath();
            if (document->IsDirty())
                title += " *";
        }
    }

    if (title != LastWindowTitle)
    {
        LastWindowTitle = title;
        Window->SetTitle(title);
    }
}
