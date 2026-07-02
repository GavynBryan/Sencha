#include "MaterialEditorServices.h"

#include "MaterialBrowserPanel.h"
#include "MaterialInspectorPanel.h"
#include "MaterialPreviewPanel.h"
#include "MaterialPreviewRenderFeature.h"

#include "project/ProjectContentMount.h"
#include "ui/EditorThemeStartup.h"
#include "ui/EditorUiFeature.h"

#include <SDL3/SDL.h>

#include <app/Engine.h>
#include <app/EngineSchedule.h>
#include <app/Game.h>
#include <assets/material/MaterialAssetLoader.h>
#include <core/assets/AssetRegistry.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/Renderer.h>
#include <platform/SdlWindow.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <utility>

namespace
{
    // Minimal frame hook (same shape as the level editor's): one callback per
    // frame for deferred work off the event path.
    class FrameHook
    {
    public:
        explicit FrameHook(std::function<void()> fn) : Fn(std::move(fn)) {}
        void FrameUpdate(FrameUpdateContext&) { if (Fn) Fn(); }

    private:
        std::function<void()> Fn;
    };
}

MaterialEditorServices::MaterialEditorServices(Engine& engine,
                                               SdlWindow& window,
                                               const EngineConfig&,
                                               std::optional<std::string> projectPath)
    : ProjectPath(std::move(projectPath))
{
    EnginePtr = &engine;
    Window = &window;

    LoadProject();
    InitAssets();
    BuildUi();
}

MaterialEditorServices::~MaterialEditorServices()
{
    // Release GPU refs while the caches live: the render features themselves
    // tear down later, in ~Renderer.
    if (Preview != nullptr)
        Preview->ReleaseResources();
    if (Assets && OpenMaterialHandle.IsValid())
        Assets->Assets.ReleaseMaterial(OpenMaterialHandle);
    Assets.reset();
}

void MaterialEditorServices::LoadProject()
{
    if (!ProjectPath)
    {
        std::fprintf(stderr, "[chakin] no project: pass --project <path.senchaproj> or set SENCHA_PROJECT\n");
        return;
    }

    ProjectDescriptor descriptor;
    std::string error;
    if (!ProjectDescriptor::Load(*ProjectPath, descriptor, &error))
    {
        std::fprintf(stderr, "[chakin] failed to open project '%s': %s\n",
                     ProjectPath->c_str(), error.c_str());
        return;
    }
    Project = std::move(descriptor);
    std::fprintf(stderr, "[chakin] opened project '%s' (%s)\n",
                 Project->Name.c_str(), ProjectPath->c_str());
}

void MaterialEditorServices::InitAssets()
{
    Engine& engine = *EnginePtr;
    GraphicsServices& graphics = engine.Graphics();

    Assets.emplace(engine.Logging(), graphics.Buffers, graphics.Images,
                   graphics.Descriptors, graphics.Samplers);
    Materials = std::make_unique<MaterialLibrary>(engine.Logging());
    if (!Project)
        return;

    MountProjectContent(*Project, *Assets, engine.Logging());
    Materials->Rescan(Project->ContentRoots);
}

void MaterialEditorServices::BuildUi()
{
    Engine& engine = *EnginePtr;
    ApplyEditorThemeFromConsole(engine.Console());

    Renderer& renderer = engine.Graphics().MainRenderer;

    auto preview = std::make_unique<MaterialPreviewRenderFeature>(*Assets);
    Preview = renderer.AddFeature(std::move(preview));

    auto uiFeature = std::make_unique<EditorUiFeature>(
        engine, *Window, engine.Graphics().Instance, engine.Graphics().Frames);
    UiFeature = uiFeature.get();
    UiFeature->SetUndoActions(
        [this]() { Commands.Undo(); },
        [this]() { Commands.Redo(); },
        [this]() { return Commands.CanUndo(); },
        [this]() { return Commands.CanRedo(); });
    UiFeature->SetFileActions(
        {},
        {},
        [this]() { SaveOpenMaterial(); },
        {});

    UiFeature->AddPanel(std::make_unique<MaterialBrowserPanel>(
        *Materials, Session,
        MaterialBrowserPanel::Actions{
            .Open = [this](const std::string& path) { OpenMaterial(path); },
            .CreateNew = [this](const std::string& name) { CreateMaterial(name, false); },
            .Duplicate = [this](const std::string& name) { CreateMaterial(name, true); },
            .Rescan = [this]() { RescanMaterials(); },
        }));
    UiFeature->AddPanel(std::make_unique<MaterialInspectorPanel>(Session, Commands, Assets->Registry));
    UiFeature->AddPanel(std::make_unique<MaterialPreviewPanel>(*Preview));

    renderer.AddFeature(std::move(uiFeature));
}

void MaterialEditorServices::RegisterSystems(EngineSchedule& schedule)
{
    schedule.Register<FrameHook>([this] { ProcessFrame(); });
}

void MaterialEditorServices::HandlePlatformEvent(PlatformEventContext& ctx)
{
    if (ctx.Event.type == SDL_EVENT_KEY_DOWN && !ctx.Event.key.repeat
        && (ctx.Event.key.mod & SDL_KMOD_CTRL) != 0)
    {
        const bool shift = (ctx.Event.key.mod & SDL_KMOD_SHIFT) != 0;
        switch (ctx.Event.key.scancode)
        {
        case SDL_SCANCODE_S:
            SaveOpenMaterial();
            ctx.Handled = true;
            return;
        case SDL_SCANCODE_Z:
            shift ? Commands.Redo() : Commands.Undo();
            ctx.Handled = true;
            return;
        case SDL_SCANCODE_Y:
            Commands.Redo();
            ctx.Handled = true;
            return;
        default:
            break;
        }
    }

    if (UiFeature != nullptr)
        UiFeature->ProcessSdlEvent(ctx.Event);
}

void MaterialEditorServices::ProcessFrame()
{
    if (Session.Version() != AppliedSessionVersion)
    {
        ApplyWorkingToResident();
        AppliedSessionVersion = Session.Version();
    }
    UpdateTitle();
}

void MaterialEditorServices::OpenMaterial(const std::string& virtualPath)
{
    if (!Assets)
        return;
    const AssetRecord* record = Assets->Registry.FindByPath(virtualPath);
    if (record == nullptr || record->FilePath.empty())
    {
        std::fprintf(stderr, "[chakin] '%s' is not an editable material file\n", virtualPath.c_str());
        return;
    }

    std::string error;
    if (!Session.Open(virtualPath, record->FilePath, &error))
    {
        std::fprintf(stderr, "[chakin] failed to open '%s': %s\n", virtualPath.c_str(), error.c_str());
        return;
    }

    // Undo history belongs to one material: a stale command applied to a
    // different open file would silently edit the wrong asset.
    Commands.Clear();

    if (OpenMaterialHandle.IsValid())
        Assets->Assets.ReleaseMaterial(OpenMaterialHandle);
    OpenMaterialHandle = Assets->Assets.LoadMaterial(virtualPath);
    Preview->SetMaterial(OpenMaterialHandle);

    // The resident material just loaded from disk, which is the saved (and,
    // right after Open, working) state.
    AppliedSessionVersion = Session.Version();
}

void MaterialEditorServices::SaveOpenMaterial()
{
    if (!Session.HasOpen())
        return;
    std::string error;
    if (!Session.Save(&error))
        std::fprintf(stderr, "[chakin] save failed: %s\n", error.c_str());
}

void MaterialEditorServices::CreateMaterial(const std::string& name, bool duplicateOpen)
{
    if (!Assets || !Project || Project->ContentRoots.empty() || name.empty())
        return;
    if (duplicateOpen && !Session.HasOpen())
        return;

    const std::filesystem::path root(Project->ContentRoots.front());
    const std::filesystem::path file = root / "materials" / (name + ".smat");
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    if (std::filesystem::exists(file, ec))
    {
        std::fprintf(stderr, "[chakin] '%s' already exists\n", file.string().c_str());
        return;
    }

    std::string error;
    const bool written = duplicateOpen ? Session.SaveTo(file.string(), &error)
                                       : MaterialEditSession::CreateNew(file.string(), &error);
    if (!written)
    {
        std::fprintf(stderr, "[chakin] create failed: %s\n", error.c_str());
        return;
    }

    RescanMaterials();
    OpenMaterial("asset://materials/" + name + ".smat");
}

void MaterialEditorServices::RescanMaterials()
{
    if (!Assets || !Project)
        return;
    // Registry re-scan picks up files created since startup (this app's New/
    // Duplicate included), then the pickable list follows.
    for (const std::string& root : Project->ContentRoots)
        ScanAssetsDirectory(root, Assets->Registry);
    Materials->Rescan(Project->ContentRoots);
}

void MaterialEditorServices::ApplyWorkingToResident()
{
    if (!Assets || !Session.HasOpen() || !OpenMaterialHandle.IsValid())
        return;
    const AssetRecord* record = Assets->Registry.FindByPath(Session.VirtualPath());
    if (record == nullptr)
        return;

    AssetStaging staging;
    staging.Record = *record;
    staging.Payload = Session.Working();
    (void)Assets->Assets.MaterialLoaderRef().CommitReload(std::move(staging));
}

void MaterialEditorServices::UpdateTitle()
{
    std::string title = "Chakin - Material Editor";
    if (Session.HasOpen())
    {
        title += " - ";
        title += Session.VirtualPath();
        if (Session.IsDirty())
            title += " *";
    }
    if (title != LastWindowTitle && Window != nullptr)
    {
        Window->SetTitle(title);
        LastWindowTitle = title;
    }
}
