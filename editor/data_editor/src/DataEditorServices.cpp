#include "DataEditorServices.h"

#include "DataEditorPanels.h"
#include "movement/MovementProfileForm.h"
#include "movement/MovementResolvePanel.h"
#include "movement/MovementResponsePanel.h"

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
            .Right = 0.30f,
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
        UiFeature->SetFileActions({}, {}, [this]() { SaveActive(); }, {});
        UiFeature->SetSaveAllAction([this]() { Workspace->SaveAll(); });

        UiFeature->AddPanel(std::make_unique<DataAssetBrowserPanel>(*Workspace));
        UiFeature->AddPanel(std::make_unique<DataFormPanel>(*Workspace, Preview));
        UiFeature->AddPanel(std::make_unique<DataDocumentationPanel>(*Workspace));
        UiFeature->AddPanel(std::make_unique<DataValidationPanel>(*Workspace));
        UiFeature->AddPanel(std::make_unique<DataRawJsonPanel>(*Workspace));

        // Registered unconditionally: documents open and close at runtime, so
        // these self-gate on the active subtype rather than being added later.
        UiFeature->AddPanel(std::make_unique<MovementResolvePanel>(*Workspace, Preview));
        UiFeature->AddPanel(std::make_unique<MovementResponsePanel>(*Workspace, Preview));

        BuildShortcuts();
    }

    engine.Graphics().MainRenderer.AddFeature(std::move(ui));
}

void DataEditorServices::BuildShortcuts()
{
    Shortcuts.Register("data.save", SDLK_S, ModifierFlags{ .Ctrl = true },
                       [this] { SaveActive(); });
    Shortcuts.Register("data.save_all", SDLK_S, ModifierFlags{ .Ctrl = true, .Shift = true },
                       [this] { Workspace->SaveAll(); });
    Shortcuts.Register("data.undo", SDLK_Z, ModifierFlags{ .Ctrl = true },
                       [this] { if (DataDocument* d = Workspace->Active()) d->Undo(); });
    Shortcuts.Register("data.redo", SDLK_Y, ModifierFlags{ .Ctrl = true },
                       [this] { if (DataDocument* d = Workspace->Active()) d->Redo(); });
    Shortcuts.Register("data.redo_alt", SDLK_Z, ModifierFlags{ .Ctrl = true, .Shift = true },
                       [this] { if (DataDocument* d = Workspace->Active()) d->Redo(); });
}

void DataEditorServices::SaveActive()
{
    if (!Workspace)
        return;
    std::string error;
    if (!Workspace->SaveActive(&error) && !error.empty())
        std::fprintf(stderr, "[data_editor] save failed: %s\n", error.c_str());
}

void DataEditorServices::RegisterSystems(EngineSchedule& schedule)
{
    schedule.Register<FrameHook>([this] { ProcessFrame(); });
}

void DataEditorServices::HandlePlatformEvent(PlatformEventContext& ctx)
{
    if (UiFeature)
        UiFeature->ProcessSdlEvent(ctx.Event);

    // Shortcuts run only when a text field is not consuming the keyboard, so
    // Ctrl+S while renaming an asset reaches the field, not the workspace.
    if (ctx.Event.type != SDL_EVENT_KEY_DOWN || Workspace == nullptr)
        return;
    if (UiFeature && UiFeature->GetInputCapture().Keyboard)
        return;

    const SDL_Keymod mods = SDL_GetModState();
    const KeyDownEvent key{
        .Key = ctx.Event.key.key,
        .Modifiers = {
            .Ctrl = (mods & SDL_KMOD_CTRL) != 0,
            .Shift = (mods & SDL_KMOD_SHIFT) != 0,
            .Alt = (mods & SDL_KMOD_ALT) != 0,
        },
    };
    (void)Shortcuts.OnInput(InputEvent{ key });
}

void DataEditorServices::ProcessFrame()
{
    // One update point for the shared preview, so it stays current whether or
    // not the panels that read it happen to be visible.
    if (Workspace && Assets)
    {
        if (const DataDocument* document = Workspace->Active();
            document != nullptr && document->Subtype() == MovementProfileSubtype())
        {
            Preview.Update(*document, Assets->DataTypes);
        }
    }
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
