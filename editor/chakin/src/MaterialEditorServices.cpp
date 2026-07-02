#include "MaterialEditorServices.h"

#include "MaterialBrowserPanel.h"
#include "MaterialInspectorPanel.h"
#include "MaterialPreviewPanel.h"
#include "MaterialPreviewRenderFeature.h"
#include "TexturesPanel.h"

#include "project/ProjectContentMount.h"
#include "ui/EditorThemeStartup.h"
#include "ui/EditorUiFeature.h"

#include <SDL3/SDL.h>

#include <app/Engine.h>
#include <app/EngineSchedule.h>
#include <app/Game.h>
#include <assets/cook/AssetImporter.h>
#include <assets/cook/TextureCook.h>
#include <assets/cook/TextureImportSettings.h>
#include <assets/hotreload/AssetHotReloader.h>
#include <assets/material/MaterialAssetLoader.h>
#include <core/assets/AssetRegistry.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/Renderer.h>
#include <platform/SdlWindow.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string_view>
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

struct MaterialEditorServices::TextureRecookState
{
    struct RootReloader
    {
        std::string Root;
        AssetHotReloader Reloader;
    };

    PngTextureImporter TextureImporter;
    AssetImporterRegistry Importers;
    std::vector<std::unique_ptr<RootReloader>> Roots;
};

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
    if (Assets)
        for (const auto& tab : Tabs.Tabs())
            if (tab->Handle.IsValid())
                Assets->Assets.ReleaseMaterial(tab->Handle);
    TextureRecook.reset();
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

    TextureRecook = std::make_unique<TextureRecookState>();
    TextureRecook->Importers.Register(TextureRecook->TextureImporter);
    for (const std::string& root : Project->ContentRoots)
        TextureRecook->Roots.push_back(std::unique_ptr<TextureRecookState::RootReloader>(
            new TextureRecookState::RootReloader{
                root,
                AssetHotReloader(engine.Logging(), Assets->Assets, Assets->Registry,
                                 TextureRecook->Importers, engine.Tasks(), root),
            }));
}

void MaterialEditorServices::BuildUi()
{
    Engine& engine = *EnginePtr;
    ApplyEditorThemeFromConsole(engine.Console());

    Renderer& renderer = engine.Graphics().MainRenderer;

    auto preview = std::make_unique<MaterialPreviewRenderFeature>(*Assets);
    Preview = renderer.AddFeature(std::move(preview));

    auto uiFeature = std::make_unique<EditorUiFeature>(
        engine, *Window, engine.Graphics().Instance, engine.Graphics().Frames,
        "chakin.imgui.ini");
    UiFeature = uiFeature.get();
    UiFeature->SetUndoActions(
        [this]() { if (MaterialEditTab* tab = Tabs.Active()) tab->Commands.Undo(); },
        [this]() { if (MaterialEditTab* tab = Tabs.Active()) tab->Commands.Redo(); },
        [this]() { MaterialEditTab* tab = Tabs.Active(); return tab != nullptr && tab->Commands.CanUndo(); },
        [this]() { MaterialEditTab* tab = Tabs.Active(); return tab != nullptr && tab->Commands.CanRedo(); });
    UiFeature->SetFileActions(
        {},
        {},
        [this]() { SaveActiveMaterial(); },
        {});
    UiFeature->SetSaveAllAction([this]() { SaveAllMaterials(); });

    UiFeature->AddPanel(std::make_unique<MaterialBrowserPanel>(
        *Materials, Tabs,
        MaterialBrowserPanel::Actions{
            .Open = [this](const std::string& path) { OpenMaterial(path); },
            .CreateNew = [this](const std::string& name) { CreateMaterial(name, false); },
            .Duplicate = [this](const std::string& name) { CreateMaterial(name, true); },
            .Rename = [this](const std::string& path, const std::string& newRelPath)
            { RenameMaterial(path, newRelPath); },
            .Rescan = [this]() { RescanMaterials(); },
        }));
    UiFeature->AddPanel(std::make_unique<MaterialInspectorPanel>(
        Tabs, Assets->Registry,
        [this](const std::string& virtualPath)
        { if (Textures != nullptr) Textures->SelectTexture(virtualPath); }));

    auto texturesPanel = std::make_unique<TexturesPanel>(
        Assets->Registry,
        Project ? Project->ContentRoots : std::vector<std::string>{},
        [this](const TextureSourceLocation& source, std::string* error)
        { return RecookTexture(source, error); });
    Textures = texturesPanel.get();
    UiFeature->AddPanel(std::move(texturesPanel));
    UiFeature->AddPanel(std::make_unique<MaterialPreviewPanel>(
        *Preview, Tabs, [this](std::size_t index) { CloseTab(index); }));

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
        MaterialEditTab* tab = Tabs.Active();
        switch (ctx.Event.key.scancode)
        {
        case SDL_SCANCODE_S:
            shift ? SaveAllMaterials() : SaveActiveMaterial();
            ctx.Handled = true;
            return;
        case SDL_SCANCODE_Z:
            if (tab != nullptr)
                shift ? tab->Commands.Redo() : tab->Commands.Undo();
            ctx.Handled = true;
            return;
        case SDL_SCANCODE_Y:
            if (tab != nullptr)
                tab->Commands.Redo();
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
    for (const auto& tab : Tabs.Tabs())
        if (tab->Session.Version() != tab->AppliedVersion)
        {
            ApplyWorkingToResident(*tab);
            tab->AppliedVersion = tab->Session.Version();
        }

    // The preview follows the active tab (tab bar clicks change it without
    // going through OpenMaterial).
    MaterialEditTab* active = Tabs.Active();
    if (Preview != nullptr)
        Preview->SetMaterial(active != nullptr ? active->Handle : MaterialHandle{});

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
    const bool existed = Tabs.Find(virtualPath) != nullptr;
    MaterialEditTab* tab = Tabs.OpenOrFocus(virtualPath, record->FilePath, &error);
    if (tab == nullptr)
    {
        std::fprintf(stderr, "[chakin] failed to open '%s': %s\n", virtualPath.c_str(), error.c_str());
        return;
    }

    if (!existed)
    {
        tab->Handle = Assets->Assets.LoadMaterial(virtualPath);
        // The resident material just loaded from disk, which is the saved
        // (and, right after Open, working) state.
        tab->AppliedVersion = tab->Session.Version();
    }
}

void MaterialEditorServices::CloseTab(std::size_t index)
{
    if (index >= Tabs.Tabs().size())
        return;
    if (Assets && Tabs.Tabs()[index]->Handle.IsValid())
        Assets->Assets.ReleaseMaterial(Tabs.Tabs()[index]->Handle);
    Tabs.Close(index);
}

void MaterialEditorServices::SaveActiveMaterial()
{
    MaterialEditTab* tab = Tabs.Active();
    if (tab == nullptr || !tab->Session.HasOpen())
        return;
    std::string error;
    if (!tab->Session.Save(&error))
        std::fprintf(stderr, "[chakin] save failed: %s\n", error.c_str());
}

void MaterialEditorServices::SaveAllMaterials()
{
    std::string error;
    Tabs.SaveAll(&error);
    if (!error.empty())
        std::fprintf(stderr, "[chakin] save all: %s\n", error.c_str());
}

void MaterialEditorServices::CreateMaterial(const std::string& name, bool duplicateOpen)
{
    if (!Assets || !Project || Project->ContentRoots.empty() || name.empty())
        return;
    MaterialEditTab* active = Tabs.Active();
    if (duplicateOpen && (active == nullptr || !active->Session.HasOpen()))
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
    const bool written = duplicateOpen ? active->Session.SaveTo(file.string(), &error)
                                       : MaterialEditSession::CreateNew(file.string(), &error);
    if (!written)
    {
        std::fprintf(stderr, "[chakin] create failed: %s\n", error.c_str());
        return;
    }

    RescanMaterials();
    OpenMaterial("asset://materials/" + name + ".smat");
}

void MaterialEditorServices::RenameMaterial(const std::string& virtualPath,
                                            const std::string& newRelPath)
{
    if (!Assets || !Project || newRelPath.empty())
        return;
    const AssetRecord* record = Assets->Registry.FindByPath(virtualPath);
    if (record == nullptr || record->FilePath.empty())
    {
        std::fprintf(stderr, "[chakin] '%s' is not a renameable material file\n", virtualPath.c_str());
        return;
    }

    // The move stays inside the content root that owns the file, so the new
    // asset:// path is the new root-relative path.
    const std::filesystem::path oldFile(record->FilePath);
    std::filesystem::path owningRoot;
    for (const std::string& root : Project->ContentRoots)
    {
        const auto rel = oldFile.lexically_relative(root);
        if (!rel.empty() && rel.native().rfind("..", 0) != 0)
        {
            owningRoot = root;
            break;
        }
    }
    if (owningRoot.empty())
    {
        std::fprintf(stderr, "[chakin] '%s' is outside every content root\n", record->FilePath.c_str());
        return;
    }

    std::string rel = newRelPath;
    if (rel.size() < 5 || rel.substr(rel.size() - 5) != ".smat")
        rel += ".smat";
    const std::filesystem::path newFile = (owningRoot / rel).lexically_normal();

    std::error_code ec;
    if (std::filesystem::exists(newFile, ec))
    {
        std::fprintf(stderr, "[chakin] '%s' already exists\n", newFile.string().c_str());
        return;
    }
    std::filesystem::create_directories(newFile.parent_path(), ec);
    std::filesystem::rename(oldFile, newFile, ec);
    if (ec)
    {
        std::fprintf(stderr, "[chakin] rename failed: %s\n", ec.message().c_str());
        return;
    }

    const std::string newVirtual = "asset://" + rel;
    // Levels referencing the old path are not rewritten; those faces render
    // as the level default until reassigned. Same policy as deleting a file.
    std::fprintf(stderr, "[chakin] renamed '%s' -> '%s' (level refs are not rewritten)\n",
                 virtualPath.c_str(), newVirtual.c_str());

    if (MaterialEditTab* tab = Tabs.Find(virtualPath))
    {
        tab->Session.RenameTo(newVirtual, newFile.string());
        if (tab->Handle.IsValid())
            Assets->Assets.ReleaseMaterial(tab->Handle);
        tab->Handle = MaterialHandle{};
    }

    RescanMaterials();

    if (MaterialEditTab* tab = Tabs.Find(newVirtual); tab != nullptr && !tab->Handle.IsValid())
    {
        tab->Handle = Assets->Assets.LoadMaterial(newVirtual);
        // Force a re-apply so an unsaved working state survives the move.
        tab->AppliedVersion = 0;
    }
}

bool MaterialEditorServices::RecookTexture(const TextureSourceLocation& source, std::string* error)
{
    if (!TextureRecook)
    {
        if (error != nullptr)
            *error = "no project mounted";
        return false;
    }
    for (const auto& entry : TextureRecook->Roots)
    {
        if (entry->Root != source.Root)
            continue;
        // Recook is synchronous; the resident swap commits at the engine's
        // async drain (the bindless slot repoints, so every material sampling
        // the texture follows within a frame).
        entry->Reloader.ReloadSource(source.RelPath);
        return true;
    }
    if (error != nullptr)
        *error = "'" + source.Root + "' is not a mounted content root";
    return false;
}

void MaterialEditorServices::RescanMaterials()
{
    if (!Assets || !Project)
        return;
    // Registry re-scan picks up files created since startup (this app's New/
    // Duplicate/Rename included), then the pickable list follows.
    for (const std::string& root : Project->ContentRoots)
        ScanAssetsDirectory(root, Assets->Registry);
    Materials->Rescan(Project->ContentRoots);
}

void MaterialEditorServices::ApplyWorkingToResident(MaterialEditTab& tab)
{
    if (!Assets || !tab.Session.HasOpen() || !tab.Handle.IsValid())
        return;
    const AssetRecord* record = Assets->Registry.FindByPath(tab.Session.VirtualPath());
    if (record == nullptr)
        return;

    AssetStaging staging;
    staging.Record = *record;
    staging.Payload = tab.Session.Working();
    (void)Assets->Assets.MaterialLoaderRef().CommitReload(std::move(staging));
}

void MaterialEditorServices::UpdateTitle()
{
    MaterialEditTab* tab = Tabs.Active();
    std::string title = "Chakin - Material Editor";
    if (tab != nullptr && tab->Session.HasOpen())
    {
        title += " - ";
        title += tab->Session.VirtualPath();
        if (tab->Session.IsDirty())
            title += " *";
    }
    if (title != LastWindowTitle && Window != nullptr)
    {
        Window->SetTitle(title);
        LastWindowTitle = title;
    }
}
